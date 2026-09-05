<#
  audiodebug.ps1 - drive the PC port's audio probes (D202 / D204) and
  summarise the result, instead of hand-assembling env vars + an input
  script + a grep pipeline every time.

  Companion to debug.ps1 (which is for CRASHES - run that one when you want a
  gdb backtrace). This one is for AUDIO BEHAVIOUR: is the pipeline keeping up,
  is the queue starving or pinning, which soundIndexes are being requested.

  Usage (from repo root):
    .\tools_pc\audiodebug.ps1                          # 60s headless, level_09, health only
    .\tools_pc\audiodebug.ps1 -Level 33 -Seconds 120   # a different level, longer
    .\tools_pc\audiodebug.ps1 -Fire                    # + scripted PPK fire pulses
    .\tools_pc\audiodebug.ps1 -Fire -Trace             # + per-sndPlaySfx trace + index histogram
    .\tools_pc\audiodebug.ps1 -Fire -Trace -Dump       # + raw mixed PCM to audiodump.raw
    .\tools_pc\audiodebug.ps1 -AB -Fire                # run BOTH modes, print before/after table
    .\tools_pc\audiodebug.ps1 -Play                    # INTERACTIVE playtest, no timeout (D202)
    .\tools_pc\audiodebug.ps1 -Soak                    # 5-minute stability soak

  The interesting output is the health line, one per 5 s (GE_D204):

    D204 audio health t=65s rt=1.000 q=432/8192 drop=0 max=3136/3156
                              |          |          |        |
      produced audio / wall --+          |          |        +-- largest block
      1.000 = healthy. Below 1 means the |          |            vs its allocation
      30 Hz audio retrace is starving    |          +-- blocks discarded because
      and the device is padding with     |              the queue was full
      silence (D204).                    |
                                         +-- SDL queue depth / QueueLimit.
                                             Should sit shallow and NEVER hit 0
                                             (starvation) or pin at the limit
                                             (overproduction -> the "eventually
                                             no audio except a stuck loop" state).

  See docs/dev/findings.md D204 and docs/dev/GE-ENV-PROBES.md.
#>
[CmdletBinding()]
param(
    # What to run
    [string]$Level    = "09",       # -level_NN ; "" or "menu" boots the front end
    [int]$Seconds     = 60,         # wall-clock cap; ignored by -Play
    [switch]$Play,                  # interactive playtest: no input script, no timeout
    [switch]$Soak,                  # 300 s stability run
    [switch]$Fire,                  # scripted fire pulses (headless repro)
    [int]$FireEvery   = 45,         # frames between pulses (45 = 0.75 s @60)

    # Probes
    [switch]$Trace,                 # GE_AUDIOTRACE=1 -> audiotrace.log
    [switch]$Dump,                  # GE_AUDIODUMP=1  -> audiodump.raw
    [switch]$MixerTrace,            # GE_MIXERTRACE=1 -- SLOW, see warning below
    [switch]$Old,                   # GE_D204_OLD=1: pre-D204 behaviour
    [switch]$AB,                    # run twice (old, then new) and compare

    # Plumbing
    [switch]$NoBuild,
    [switch]$SyncData,              # mirror ./data into build-pc/data first
    [string]$MingwBin = "C:\msys64\mingw64\bin",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArgs
)

$ErrorActionPreference = "Stop"
$repo = $PWD.Path
$buildDir = Join-Path $repo "build-pc"
$exe = Join-Path $buildDir "ge007.x86_64.exe"

if ($Soak -and -not $PSBoundParameters.ContainsKey('Seconds')) { $Seconds = 300 }

# Same DLL-resolution problem as debug.ps1: the exe links MSYS2 mingw64 SDL2 /
# libgcc / libstdc++ / libwinpthread / zlib. Without these on PATH it dies at
# startup with 0xc0000135 before any audio code runs.
if ((Test-Path $MingwBin) -and (($env:PATH -split ';') -notcontains $MingwBin)) {
    $env:PATH = "$MingwBin;$env:PATH"
}

# ---------------------------------------------------------------- build ----
if (-not $NoBuild) {
    Write-Host "building (bash build-pc.sh ntsc-final) - pass -NoBuild to skip" -ForegroundColor DarkGray
    & "C:\msys64\usr\bin\bash.exe" -lc "cd '$($repo -replace '\\','/')' && export PATH=/c/msys64/mingw64/bin:`$PATH && ./build-pc.sh ntsc-final"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not (Test-Path $exe)) { throw "no exe - build the port: bash build-pc.sh ntsc-final" }

# ------------------------------------------------------------ data check ----
# M-63 burned a whole session on this: with the ROM present but the converted
# sidecar dirs missing, obInit() falls back to reading raw big-endian ROM data
# and -level_09 hard-crashes in load_bg_file almost immediately. It looks
# exactly like a scary new engine bug and is purely a test-setup gap. The
# "[WARN] pccg.bin not found" lines are printed right there in the log.
$needed = @("ge007.ntsc-final.z64", "pccg-ntsc-final", "pcmodels-ntsc-final")
$missing = @($needed | Where-Object { -not (Test-Path (Join-Path $buildDir "data\$_")) })
if ($missing.Count -gt 0) {
    if ($SyncData) {
        Write-Host "mirroring ./data -> build-pc/data" -ForegroundColor Cyan
        New-Item -ItemType Directory -Force -Path (Join-Path $buildDir "data") | Out-Null
        Copy-Item -Recurse -Force (Join-Path $repo "data\*") (Join-Path $buildDir "data")
    } else {
        Write-Host "WARNING: build-pc/data is missing: $($missing -join ', ')" -ForegroundColor Red
        Write-Host "         Levels will crash in load_bg_file and it will look like an engine bug." -ForegroundColor Red
        Write-Host "         Re-run with -SyncData to mirror ./data automatically." -ForegroundColor Red
    }
}

# --------------------------------------------------------- input script ----
# Scripted fire pulses: R (aim) + Z (trigger) every $FireEvery frames. Frame
# 600 is roughly where a -level_NN boot is interactive.
$inputScript = ""
if ($Fire -and -not $Play) {
    $last = 600 + [int](($Seconds - 10) * 60)
    $sb = [System.Text.StringBuilder]::new()
    for ($f = 600; $f -le $last; $f += $FireEvery) { [void]$sb.Append("${f}:R,Z;") }
    $inputScript = $sb.ToString()
}

# ---------------------------------------------------------------- args -----
$gameArgv = @()
if ($Level -and $Level -ne "menu") { $gameArgv += "-level_$Level" }
if ($GameArgs) { $gameArgv += @($GameArgs | Where-Object { $_ }) }

# ----------------------------------------------------------- run one -------
function Invoke-AudioRun {
    param([string]$Tag, [bool]$OldMode)

    $stdout = Join-Path $buildDir "audiodebug_$Tag.log"
    Remove-Item -ErrorAction SilentlyContinue $stdout,
        (Join-Path $buildDir "audiodump.raw"),
        (Join-Path $buildDir "audiotrace.log"),
        (Join-Path $buildDir "mixertrace.log")

    # Set the probes on THIS process; cmd.exe and the game both inherit them.
    $env:GE_D204 = "1"
    if ($OldMode)     { $env:GE_D204_OLD = "1" }   else { Remove-Item -EA SilentlyContinue Env:\GE_D204_OLD }
    if ($Trace)       { $env:GE_AUDIOTRACE = "1" } else { Remove-Item -EA SilentlyContinue Env:\GE_AUDIOTRACE }
    if ($Dump)        { $env:GE_AUDIODUMP = "1" }  else { Remove-Item -EA SilentlyContinue Env:\GE_AUDIODUMP }
    if ($MixerTrace)  { $env:GE_MIXERTRACE = "1" } else { Remove-Item -EA SilentlyContinue Env:\GE_MIXERTRACE }
    if ($inputScript) { $env:GE_INPUTSCRIPT = $inputScript } else { Remove-Item -EA SilentlyContinue Env:\GE_INPUTSCRIPT }

    $mode = if ($OldMode) { "GE_D204_OLD (pre-fix)" } else { "current build" }
    $what = if ($Play) { "interactive - play until you close the window" } else { "$Seconds s" }
    Write-Host "`n>>> run [$Tag] $mode - $what" -ForegroundColor Green
    if ($inputScript) {
        Write-Host "    GE_INPUTSCRIPT: $((($inputScript -split ';').Count - 1)) fire pulses every $FireEvery frames" -ForegroundColor DarkGray
    }
    if ($MixerTrace) {
        Write-Host "    WARNING: GE_MIXERTRACE writes ~25 MB/min UNBUFFERED and slows the process" -ForegroundColor Red
        Write-Host "             enough to starve the audio thread. rt= will read low for that" -ForegroundColor Red
        Write-Host "             reason alone - it is NOT a real measurement. (findings.md D204)" -ForegroundColor Red
    }

    # Launch through MSYS2 bash, not Start-Process -RedirectStandardOutput.
    # CMakeLists sets WIN32_EXECUTABLE (i.e. -mwindows), so the game is a GUI-
    # subsystem binary with no console of its own; PowerShell's redirect does
    # not capture its stdout (the log comes out empty and everything lands on
    # the parent console instead). A shell that opens the file itself before
    # CreateProcess does work, and gives us coreutils `timeout` for the cap.
    # debug.ps1 already shells out to this bash for the build.
    # Launch through a generated .bat run by cmd.exe. Two reasons, both learned
    # the hard way:
    #  - CMakeLists sets WIN32_EXECUTABLE (-mwindows), so the game is a GUI-
    #    subsystem binary with no console. PowerShell's
    #    Start-Process -RedirectStandardOutput does NOT capture its stdout (the
    #    log comes out empty and everything lands on the parent console). A
    #    shell that opens the file itself before CreateProcess does work.
    #  - MSYS2 bash was the obvious such shell, but bash.exe launched from
    #    PowerShell arrives with a stripped environment: neither vars inherited
    #    from PowerShell nor bash's own `export` reach the child, so every
    #    GE_* probe silently read as unset. cmd.exe inherits normally.
    # The .bat is left on disk so a failed run can be re-run by hand.
    $runBat = Join-Path $buildDir "audiodebug_$Tag.bat"
    $argStr = ($gameArgv | ForEach-Object { "`"$_`"" }) -join ' '
    $bat = @("@echo off",
             "cd /d `"$buildDir`"",
             "`"$exe`" $argStr > `"$stdout`" 2>&1")
    [IO.File]::WriteAllLines($runBat, $bat, (New-Object Text.ASCIIEncoding))

    $p = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "`"$runBat`"" `
                       -WorkingDirectory $buildDir -PassThru -NoNewWindow
    if ($Play) {
        $p.WaitForExit()
        $rc = $p.ExitCode
    } elseif ($p.WaitForExit($Seconds * 1000)) {
        $rc = $p.ExitCode
    } else {
        Write-Host "    (time limit reached - stopping)" -ForegroundColor DarkGray
        # Kill the tree: killing cmd alone would orphan the game process and
        # leave it holding ge007.x86_64.exe, which then breaks the next build.
        & taskkill /T /F /PID $p.Id 2>&1 | Out-Null
        $p.WaitForExit(5000) | Out-Null
        $rc = 0
    }
    Start-Sleep -Milliseconds 300
    if ($rc -ne 0) { Write-Host "    (game exited with code $rc - check $stdout)" -ForegroundColor Red }
    return $stdout
}

# ------------------------------------------------------------ summarise ----
function Get-AudioSummary {
    param([string]$LogPath, [string]$Tag)

    $lines = @(Get-Content $LogPath -EA SilentlyContinue)
    $health = @($lines | Select-String -Pattern 'D204 audio health t=(\d+)s rt=([\d.]+) q=(\d+)/(\d+) drop=(\d+) max=(\d+)/(\d+)')

    $res = [ordered]@{
        Tag = $Tag; Samples = $health.Count
        RtLast = $null; RtMin = $null; QMin = $null; QMax = $null
        QLimit = $null; Drop = 0; MaxBlock = $null; Alloc = $null
    }
    if ($health.Count -gt 0) {
        $rt = @(); $q = @()
        foreach ($h in $health) {
            $m = $h.Matches[0].Groups
            $rt += [double]$m[2].Value
            $q  += [int]$m[3].Value
            $res.QLimit   = [int]$m[4].Value
            $res.Drop     = [int]$m[5].Value
            $res.MaxBlock = [int]$m[6].Value
            $res.Alloc    = [int]$m[7].Value
        }
        # Skip the first sample: it covers process start-up and reads high.
        $rtSteady = if ($rt.Count -gt 1) { $rt[1..($rt.Count - 1)] } else { $rt }
        $res.RtLast = $rt[-1]
        $res.RtMin  = ($rtSteady | Measure-Object -Minimum).Minimum
        $res.QMin   = ($q | Measure-Object -Minimum).Minimum
        $res.QMax   = ($q | Measure-Object -Maximum).Maximum
    }
    $res.Oversize = @($lines | Select-String -Pattern 'oversized audio block').Count
    $res.Warns    = @($lines | Select-String -Pattern '\[WARN\].*(pccg|pcmodels)').Count
    return [pscustomobject]$res
}

function Show-AudioVerdict {
    param($S)
    Write-Host "`n----- [$($S.Tag)] audio health -----" -ForegroundColor Yellow
    if ($S.Samples -eq 0) {
        Write-Host "  no health lines - the run died before 5 s, or GE_D204 was not honoured." -ForegroundColor Red
        return
    }
    Write-Host ("  rt (real-time ratio)  last={0:N3}  min={1:N3}   <- 1.000 is healthy" -f $S.RtLast, $S.RtMin)
    Write-Host ("  queue depth           min={0}  max={1}  limit={2}" -f $S.QMin, $S.QMax, $S.QLimit)
    Write-Host ("  dropped blocks        {0}" -f $S.Drop)
    Write-Host ("  largest block         {0} / {1} allocated" -f $S.MaxBlock, $S.Alloc)

    if ($S.RtMin -lt 0.995) {
        Write-Host "  FAIL starving: audio is produced slower than it plays; the device is" -ForegroundColor Red
        Write-Host "       padding the difference with silence (D204 symptom)." -ForegroundColor Red
    } else { Write-Host "  OK   keeping up with real time" -ForegroundColor Green }

    if ($S.QMin -eq 0) {
        Write-Host "  FAIL queue reached 0 - the DAC ran dry, audible gaps." -ForegroundColor Red
    } else { Write-Host "  OK   queue never starved" -ForegroundColor Green }

    if ($S.Drop -gt 0 -or $S.QMax -ge $S.QLimit) {
        Write-Host "  FAIL queue pinned at the limit and blocks are being discarded -" -ForegroundColor Red
        Write-Host "       this is the 'eventually no audio except a stuck loop' state (D202)." -ForegroundColor Red
    } else { Write-Host "  OK   no overproduction / no dropped blocks" -ForegroundColor Green }

    if ($S.Oversize -gt 0) {
        Write-Host "  FAIL $($S.Oversize) oversized blocks - audi.c frameSamples escaped its clamp;" -ForegroundColor Red
        Write-Host "       info->data has already been overrun. This should be impossible." -ForegroundColor Red
    }
    if ($S.Warns -gt 0) {
        Write-Host "  NOTE pccg/pcmodels warnings present - build-pc/data is incomplete;" -ForegroundColor Red
        Write-Host "       re-run with -SyncData before believing any of the above." -ForegroundColor Red
    }
}

function Show-TraceSummary {
    $tl = Join-Path $buildDir "audiotrace.log"
    if (-not (Test-Path $tl)) { return }
    $t = Get-Content $tl
    # Each sndPlaySfx logs TWO lines (resolution + returned state), so counting
    # raw "soundIndex=" matches double-counts every call. Count resolution
    # lines only -- M-63's "109 fired 94 times" was this artifact.
    $plays = @($t | Select-String -Pattern 'sndPlaySfx: bank=.*soundIndex=(-?\d+)')
    $deact = @($t | Select-String -Pattern 'sndDeactivate').Count
    Write-Host "`n----- sndPlaySfx trace -----" -ForegroundColor Yellow
    Write-Host ("  {0} sndPlaySfx calls, {1} sndDeactivate calls" -f $plays.Count, $deact)
    if ($plays.Count -eq 0) { return }
    Write-Host "  most-requested soundIndexes:"
    $plays | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } |
        Group-Object | Sort-Object Count -Descending | Select-Object -First 10 |
        ForEach-Object { Write-Host ("    idx {0,-5} x{1}" -f $_.Name, $_.Count) }

    $dump = Join-Path $buildDir "audiodump.raw"
    if (Test-Path $dump) {
        $secs = (Get-Item $dump).Length / 88200.0   # s16 stereo @ 22050 Hz
        Write-Host ("`n  audiodump.raw: {0:N1} s of audio ({1:N0} bytes)" -f $secs, (Get-Item $dump).Length)
        Write-Host "  play it with:  ffplay -f s16le -ar 22050 -ch_layout stereo build-pc\audiodump.raw" -ForegroundColor DarkGray
    }
}

# ------------------------------------------------------------------ go -----
if ($AB) {
    $oldLog = Invoke-AudioRun -Tag "old" -OldMode $true
    $newLog = Invoke-AudioRun -Tag "new" -OldMode $false
    $a = Get-AudioSummary -LogPath $oldLog -Tag "old"
    $b = Get-AudioSummary -LogPath $newLog -Tag "new"
    Show-AudioVerdict $a
    Show-AudioVerdict $b
    Write-Host "`n===================== A/B =====================" -ForegroundColor Yellow
    Write-Host ("  {0,-24} {1,12} {2,12}" -f "", "GE_D204_OLD", "current")
    Write-Host ("  {0,-24} {1,12:N3} {2,12:N3}" -f "rt (min, steady)", $a.RtMin,  $b.RtMin)
    Write-Host ("  {0,-24} {1,12} {2,12}"      -f "queue min",        $a.QMin,   $b.QMin)
    Write-Host ("  {0,-24} {1,12} {2,12}"      -f "queue max",        $a.QMax,   $b.QMax)
    Write-Host ("  {0,-24} {1,12} {2,12}"      -f "dropped blocks",   $a.Drop,   $b.Drop)
    Write-Host ("  {0,-24} {1,12} {2,12}"      -f "largest block",    $a.MaxBlock, $b.MaxBlock)
    Write-Host "`n  Both runs used the SAME binary (GE_D204_OLD switches behaviour at" -ForegroundColor DarkGray
    Write-Host "  runtime), so this is free of build-to-build variance." -ForegroundColor DarkGray
} else {
    $log = Invoke-AudioRun -Tag "run" -OldMode ([bool]$Old)
    Show-AudioVerdict (Get-AudioSummary -LogPath $log -Tag "run")
}
if ($Trace) { Show-TraceSummary }

Write-Host "`nlogs: build-pc\audiodebug_*.log" -ForegroundColor DarkGray
if ($Trace)      { Write-Host "      build-pc\audiotrace.log" -ForegroundColor DarkGray }
if ($Dump)       { Write-Host "      build-pc\audiodump.raw"  -ForegroundColor DarkGray }
if ($MixerTrace) { Write-Host "      build-pc\mixertrace.log (huge - grep it, do not open it)" -ForegroundColor DarkGray }
