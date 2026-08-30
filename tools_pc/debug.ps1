<#
  debug.ps1 - launch the PC port under gdb so a playtest crash always leaves
  a backtrace, even when it is an abort()/sysFatalError (no ge007.crash.log)
  or a silent exit.

  Usage (from repo root):
    .\tools_pc\debug.ps1                       # front-end / menu playtest
    .\tools_pc\debug.ps1 -level_09             # boot straight into a level
    .\tools_pc\debug.ps1 -Menu 13              # GE_STARTMENU=13 (mission-complete)
    .\tools_pc\debug.ps1 -Menu 13 -Page 1 -Diff 0
    .\tools_pc\debug.ps1 -InputLog             # also GE_INPUTLOG=1

  Everything gdb prints goes to gdb.txt. When the game exits (crash or
  clean) the script prints the FATAL line, the backtrace, and the crash
  log if one was written.
#>
[CmdletBinding()]
param(
    [int]$Menu = -1,
    [int]$Page = 1,
    [int]$Diff = 0,
    [switch]$InputLog,
    [string]$Gdb = "C:\msys64\mingw64\bin\gdb.exe",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArgs
)

$ErrorActionPreference = "Stop"
$exe = "build-pc\ge007.x86_64.exe"
if (-not (Test-Path $exe)) { throw "build the port first: bash build-pc.sh ntsc-final" }
if (-not (Test-Path $Gdb)) { throw "gdb not found at $Gdb (pass -Gdb <path>)" }

Remove-Item -ErrorAction SilentlyContinue ge007.crash.log, gdb.txt

if ($Menu -ge 0) {
    $env:GE_STARTMENU      = "$Menu"
    $env:GE_STARTMENU_PAGE = "$Page"
    $env:GE_STARTMENU_DIFF = "$Diff"
    Write-Host "GE_STARTMENU=$Menu page=$Page diff=$Diff" -ForegroundColor Cyan
} else {
    Remove-Item -ErrorAction SilentlyContinue Env:\GE_STARTMENU, Env:\GE_STARTMENU_PAGE, Env:\GE_STARTMENU_DIFF
}
if ($InputLog) { $env:GE_INPUTLOG = "1" } else { Remove-Item -ErrorAction SilentlyContinue Env:\GE_INPUTLOG }

$gdbArgs = @(
    "-batch",
    "-ex", "set pagination off",
    "-ex", "set confirm off",
    "-ex", "run",
    "-ex", "echo `n==== BACKTRACE ====`n",
    "-ex", "bt full",
    "-ex", "echo `n==== REGISTERS ====`n",
    "-ex", "info registers",
    "-ex", "echo `n==== ALL THREADS ====`n",
    "-ex", "thread apply all bt",
    "--args", $exe
) + $GameArgs

Write-Host "launching under gdb - play until it crashes, output -> gdb.txt" -ForegroundColor Green
& $Gdb @gdbArgs 2>&1 | Tee-Object -FilePath gdb.txt | Out-Null

Write-Host "`n===================== RESULT =====================" -ForegroundColor Yellow
$fatal = Select-String -Path gdb.txt -Pattern "FATAL|Unknown GBI|assertion|abort\(\)" | Select-Object -First 5
if ($fatal) { Write-Host "-- fatal / abort --" -ForegroundColor Red; $fatal | ForEach-Object { $_.Line } }

Write-Host "`n-- backtrace --" -ForegroundColor Red
$bt = Get-Content gdb.txt
$i = ($bt | Select-String -Pattern "==== BACKTRACE ====" | Select-Object -First 1).LineNumber
if ($i) { $bt[$i..([Math]::Min($i + 45, $bt.Count - 1))] | Where-Object { $_ -match '\S' } }
else    { $bt | Select-String -Pattern "^#\d+|\.c:\d|\.cpp:\d|SIG|exited with code" | Select-Object -Last 40 | ForEach-Object { $_.Line } }

if (Test-Path ge007.crash.log) {
    Write-Host "`n-- ge007.crash.log --" -ForegroundColor Red
    Get-Content ge007.crash.log
} else {
    Write-Host "`n(no ge007.crash.log - abort()/fatal or clean exit; see backtrace above)" -ForegroundColor DarkGray
}
Write-Host "`nfull gdb output: gdb.txt" -ForegroundColor DarkGray
