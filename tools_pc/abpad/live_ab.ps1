# Live side-by-side A/B: 1964 GEPD (left) and the GE007 PC port (right), both
# driven by ONE real gamepad. The port reads the pad even unfocused via
# GE_PAD_BACKGROUND=1; 1964 gets focus when you click it (PauseWhenInactive 0
# is already set in 1964.cfg).
#
# 1964 starts ~0.8 s slower than the port (measured 2026-10-02: 1964 launched
# 1.2 s after the port ran ~2 s behind), so 1964 launches first and the port
# PortDelayMs later. Tune -PortDelayMs if the intros drift.
#
# The real pad must be ON (no virtual pad / abpad). The NRage profile should
# match the port's pad layout. Does not wait for the games to exit.
param([int]$PortDelayMs = 800,
      [string]$Emu = (Join-Path $PSScriptRoot '..\..\..\ge-port-reference\1964_GEPD_Edition\1964'),
      [string]$Rom = 'GoldenEyeUSA.n64',
      [string]$PortExe = (Join-Path $PSScriptRoot '..\..\build-pc\ge007.x86_64.exe'),
      [int]$HoldSec = 30,   # keep re-applying the positions this long (both games move themselves at startup)
      [switch]$NoArrange, [switch]$DryRun)

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$emu  = (Resolve-Path $Emu).Path
$PortExe = (Resolve-Path $PortExe).Path
if (-not (Test-Path "$emu\1964.exe")) { Write-Error "1964 not found: $emu\1964.exe"; exit 1 }
if (-not (Test-Path $PortExe))       { Write-Error "port exe not found: $PortExe"; exit 1 }

$emuLine = "Start-Process -FilePath `"$emu\1964.exe`" -ArgumentList @('-c','NRage_Input_V2.dll','-g','$Rom') -WorkingDirectory '$emu'"
$portLine = "Start-Process -FilePath '$PortExe' -WorkingDirectory '$repo'   (env GE_PAD_BACKGROUND=1)"

if ($DryRun) {
  Write-Output "1964 : $emuLine"
  Write-Output "port : $portLine"
  Write-Output "delay: port launches ${PortDelayMs} ms after 1964"
  Write-Output "arrange: $(if ($NoArrange) {'off'} else {'on'})"
  exit 0
}

$emuProc = Start-Process -FilePath "$emu\1964.exe" -ArgumentList @('-c','NRage_Input_V2.dll','-g',$Rom) `
      -WorkingDirectory $emu -PassThru
Start-Sleep -Milliseconds $PortDelayMs
$env:GE_PAD_BACKGROUND = '1'
$portProc = Start-Process -FilePath $PortExe -WorkingDirectory $repo -PassThru

# Wait up to 15 s (200 ms polls) for both windows to appear.
$deadline = (Get-Date).AddSeconds(15)
do {
  $emuProc.Refresh(); $portProc.Refresh()
  Start-Sleep -Milliseconds 200
} while (((($emuProc.MainWindowHandle -eq 0) -or ($portProc.MainWindowHandle -eq 0)) -and (Get-Date) -lt $deadline))
$emuProc.Refresh(); $portProc.Refresh()
if (($emuProc.MainWindowHandle -eq 0) -or ($portProc.MainWindowHandle -eq 0)) {
  Write-Warning "one or both windows not up after 15 s; continuing"
}

if (-not $NoArrange) {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -Namespace Win32 -Name User32 -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int X, int Y, int W, int H, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
'@
  # Position only, never resize: GLideN64 renders 1964 at its own windowed
  # size (plugin/GLideN64.ini video\windowedWidth/Height = 1280x720) and draws
  # into a corner if the window is resized; the port takes its size from
  # data/ge007.ini [Window] (1280x720). 1964 goes top-left, the port to its
  # right. Re-applied for HoldSec since both reposition themselves at startup.
  $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
  $SWP = 0x0001 -bor 0x0004   # NOSIZE | NOZORDER
  $end = (Get-Date).AddSeconds($HoldSec)
  Write-Output "READY: start pad input once both show the same screen (holding positions for $HoldSec s)."
  while ((Get-Date) -lt $end) {
    $emuProc.Refresh(); $portProc.Refresh()
    $x = $wa.X
    foreach ($p in @($emuProc, $portProc)) {
      if ($p.HasExited -or $p.MainWindowHandle -eq 0) { continue }
      $r = New-Object Win32.User32+RECT
      [Win32.User32]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
      if ($r.L -ne $x -or $r.T -ne $wa.Y) {
        [Win32.User32]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $x, $wa.Y, 0, 0, $SWP) | Out-Null
      }
      $x += ($r.R - $r.L)
    }
    Start-Sleep -Milliseconds 250
  }
}

if ($NoArrange) { Write-Output "READY: start pad input once both show the same screen." }
Write-Output "1964 pid : $($emuProc.Id)"
Write-Output "port pid : $($portProc.Id)"
