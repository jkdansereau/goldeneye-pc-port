param([string]$OutDir, [int]$Seconds = 24, [double]$Step = 1.0)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
"@
[W]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$t0 = Get-Date
$log = Join-Path $OutDir "cap.log"
"start $($t0.ToString('HH:mm:ss.fff'))" | Out-File $log -Encoding ascii
function Find-Win($name) {
  $p = Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
  if ($p) { return $p.MainWindowHandle } else { return [IntPtr]::Zero }
}
function Snap($h, $path) {
  if ($h -eq [IntPtr]::Zero) { return $false }
  $r = New-Object W+RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  if ($w -le 0 -or $hh -le 0) { return $false }
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [W]::PrintWindow($h, $hdc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
  $g.ReleaseHdc($hdc)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  return $true
}
$n = [int]([math]::Floor($Seconds / $Step))
for ($i = 1; $i -le $n; $i++) {
  $t = $i * $Step
  while (((Get-Date) - $t0).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 5 }
  $tag = "{0:D5}" -f [int]($t * 1000)
  $a = Snap (Find-Win '1964') (Join-Path $OutDir "1964_$tag.png")
  $b = Snap (Find-Win 'ge007.x86_64') (Join-Path $OutDir "port_$tag.png")
  "t=$t 1964=$a port=$b" | Out-File $log -Append -Encoding ascii
}
"done" | Out-File $log -Append -Encoding ascii
