<#
  perf.ps1 - sister script to debug.ps1: launch the PC port under an ETW
  trace (xperf, part of the Windows Performance Toolkit) so a bad-fps
  playtest always leaves a CPU profile, not just a "felt slow" report.

  Requires an ELEVATED PowerShell/cmd (xperf needs SeSystemProfilePrivilege).
  Re-launch this script itself as admin if it complains.

  Usage (from repo root, elevated):
    .\tools_pc\perf.ps1                        # front-end / menu playtest
    .\tools_pc\perf.ps1 -level_33               # boot straight into a level (Dam)
    .\tools_pc\perf.ps1 -level_33 -Seconds 30   # auto-stop after 30s instead of waiting for exit
    .\tools_pc\perf.ps1 -NoBuild -NoSymbols     # skip rebuild + skip MS symbol download (faster)

  Produces, under perf_captures\<timestamp>\:
    trace.etl            - raw ETW trace (open in WPA for the full GUI view)
    cswitch_thread.csv    - per-thread CPU time (xperf -a cswitch -thread -process)
    profile_detail.csv    - per-module CPU sample weight, per process
    summary.txt           - tracerpt event-count summary
  and prints a quick top-offenders readout (busiest thread, busiest modules)
  straight to the console so most sessions won't need WPA at all.

  Symbol resolution (-Symbols, off by default): downloads Microsoft's public
  PDBs for system DLLs (msvcrt.dll, ntdll.dll, ...) so hot functions inside
  them resolve by name instead of just module -- this is what caught the
  D250 getenv() bug. Slow on first run (network), fast after (local cache
  under perf_captures\symcache\). Our own ge007.x86_64.exe uses DWARF debug
  info (GCC -g), which xperf/WPA cannot read regardless of -Symbols -- that
  part of any stack always shows as the raw module name, not a function.
#>
[CmdletBinding()]
param(
    [int]$Seconds = 0,                 # 0 = wait for the game to exit on its own
    [switch]$NoBuild,
    [switch]$Symbols,
    [string]$Xperf = "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArgs
)

$ErrorActionPreference = "Stop"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "perf.ps1 needs an elevated shell (xperf requires SeSystemProfilePrivilege) - re-run from an admin PowerShell/cmd." }
if (-not (Test-Path $Xperf)) { throw "xperf not found at $Xperf (pass -Xperf <path>; it ships with the Windows Performance Toolkit / Windows SDK)" }

# The exe links MSYS2 mingw64 DLLs (SDL2, libgcc, libstdc++, libwinpthread,
# zlib). An elevated PowerShell doesn't have these on PATH and the game dies
# at startup with 0xc0000135 (DLL not found) -- same fix as debug.ps1.
$MingwBin = "C:\msys64\mingw64\bin"
if ((Test-Path $MingwBin) -and (($env:PATH -split ';') -notcontains $MingwBin)) {
    $env:PATH = "$MingwBin;$env:PATH"
}

$exe = "build-pc\ge007.x86_64.exe"
if (-not $NoBuild) {
    Write-Host "building (bash build-pc.sh ntsc-final) - pass -NoBuild to skip" -ForegroundColor DarkGray
    & "C:\msys64\usr\bin\bash.exe" -lc "cd '$($PWD -replace '\\','/')' && export PATH=/c/msys64/mingw64/bin:`$PATH && ./build-pc.sh ntsc-final"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not (Test-Path $exe)) { throw "no exe - build the port: bash build-pc.sh ntsc-final" }

$stamp   = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir  = "perf_captures\$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$etl     = Join-Path $outDir "trace.etl"

Write-Host "starting xperf trace (CPU sampling + context switches)" -ForegroundColor Cyan
& $Xperf -on PROC_THREAD+LOADER+CSWITCH+DISPATCHER+PROFILE -stackwalk PROFILE
if ($LASTEXITCODE -ne 0) { throw "xperf -start failed" }

try {
    Write-Host "launching $exe $GameArgs" -ForegroundColor Green
    $proc = Start-Process -FilePath $exe -ArgumentList $GameArgs -PassThru

    if ($Seconds -gt 0) {
        Write-Host "capturing for $Seconds s (play now) ..." -ForegroundColor Yellow
        Start-Sleep -Seconds $Seconds
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    } else {
        Write-Host "capturing until the game exits (close the window when done) ..." -ForegroundColor Yellow
        $proc.WaitForExit()
    }
} finally {
    Write-Host "stopping xperf trace -> $etl" -ForegroundColor Cyan
    & $Xperf -stop -d $etl
}

if (-not (Test-Path $etl)) { throw "trace file was not written - xperf -stop likely errored above" }

$symArgs = @()
if ($Symbols) {
    $symCache = "perf_captures\symcache"
    New-Item -ItemType Directory -Force -Path $symCache | Out-Null
    $env:_NT_SYMBOL_PATH  = "srv*$symCache*https://msdl.microsoft.com/download/symbols"
    $env:_NT_SYMCACHE_PATH = $symCache
    $symArgs = @("-symbols")
    Write-Host "resolving symbols against Microsoft's public server (first run is slow)" -ForegroundColor DarkGray
}

Write-Host "`nanalyzing trace ..." -ForegroundColor Cyan
& "C:\Windows\System32\tracerpt.exe" $etl -summary (Join-Path $outDir "summary.txt") -y | Out-Null
& $Xperf -i $etl @symArgs -o (Join-Path $outDir "cswitch_thread.csv") -a cswitch -thread -process
& $Xperf -i $etl @symArgs -o (Join-Path $outDir "profile_detail.csv") -a profile -detail

# ---- Quick top-offenders readout -------------------------------------------
$procRow = Select-String -Path (Join-Path $outDir "cswitch_thread.csv") -Pattern "ge007\.x86_64\.exe" | Select-Object -First 1
Write-Host "`n===================== RESULT =====================" -ForegroundColor Yellow
if ($procRow) { Write-Host "process total CPU time: $($procRow.Line.Trim())" }

Write-Host "`n-- busiest ge007 threads (CPU us) --" -ForegroundColor Red
Get-Content (Join-Path $outDir "cswitch_thread.csv") |
    Select-String -Pattern "ge007\.x86_64\.exe.*,\s*\d+\s*$" |
    ForEach-Object { $_.Line.Trim() } |
    Sort-Object { [int]($_ -replace ',.*$', '') } -Descending |
    Select-Object -First 8

Write-Host "`n-- busiest modules inside ge007 (sample weight) --" -ForegroundColor Red
Get-Content (Join-Path $outDir "profile_detail.csv") |
    Select-String -Pattern "^ge007\.x86_64\.exe" |
    ForEach-Object {
        $f = $_.Line -split ","
        [PSCustomObject]@{ Weight = [long]($f[1].Trim()); Pct = $f[2].Trim(); Module = $f[3].Trim() }
    } | Sort-Object Weight -Descending | Select-Object -First 12 |
    Format-Table -AutoSize

Write-Host "`nraw data: $outDir\  (open trace.etl in WPA for the full GUI view)" -ForegroundColor DarkGray
if (-not $Symbols) {
    Write-Host "re-run with -Symbols to resolve system-DLL function names (e.g. this is how D250's getenv() bug was found)" -ForegroundColor DarkGray
}
