# Boot 1964 and the port together, drive both from one abpad timeline, and
# screenshot both windows on one clock (capture.ps1). Compare with sheet.py.
# Needs: the real pad off (abpad must own XInput slot 0), a port exe with
# GE_INPUTSCRIPT_MS, and a ROM name without spaces in 1964's ROM directory.
param([string]$Timeline, [int]$QuitFrame = 1500, [int]$Seconds = 24, [double]$Step = 1.0,
      [int]$PadOffset = 2000,
      [string]$Emu = (Join-Path $PSScriptRoot '..\..\..\ge-port-reference\1964_GEPD_Edition\1964'),
      [string]$Rom = 'GoldenEyeUSA.n64',
      [string]$Out = (Join-Path $env:TEMP 'abpad-run'))
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$emu  = (Resolve-Path $Emu).Path
Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Remove-Item Env:GE_PCDUMP -ErrorAction SilentlyContinue
$ab = Start-Process -FilePath python -ArgumentList @("$repo\tools_pc\abpad\abpad.py", "`"$Timeline`"",
        '--quitframe', "$QuitFrame", '--port-cwd', $repo, '--linger-ms', '6000', '--pad-offset-ms', "$PadOffset") `
      -WorkingDirectory $repo -PassThru -NoNewWindow `
      -RedirectStandardOutput "$Out\abpad.out" -RedirectStandardError "$Out\abpad.err"
Start-Sleep -Milliseconds 1200   # the virtual pad exists before NRage initialises
$emuProc = Start-Process -FilePath "$emu\1964.exe" -ArgumentList @('-c', 'NRage_Input_V2.dll', '-g', $Rom) `
      -WorkingDirectory $emu -PassThru
& powershell -NoProfile -File "$PSScriptRoot\capture.ps1" -OutDir $Out -Seconds $Seconds -Step $Step
$ab.WaitForExit(60000) | Out-Null
"abpad exit: $($ab.ExitCode)" | Out-File "$Out\run.log" -Encoding ascii
if (-not $emuProc.HasExited) {
  $emuProc.CloseMainWindow() | Out-Null
  if (-not $emuProc.WaitForExit(8000)) { "1964 did not close; left running" | Out-File "$Out\run.log" -Append -Encoding ascii }
  else { "1964 closed" | Out-File "$Out\run.log" -Append -Encoding ascii }
}
"output: $Out" | Write-Output
