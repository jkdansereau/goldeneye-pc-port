#!/usr/bin/env bash
#
# Build the GoldenEye 007 PC port.
#
# Usage:
#   ./build-pc.sh [ntsc-final|pal-final|jpn-final]
#
# Dependencies (see docs/building.md):
#   - CMake >= 3.16
#   - SDL2 dev
#   - zlib dev
#   - OpenGL dev (opengl32 on Windows, GL on Linux, OpenGL.framework on macOS)
#
# Example (Linux):
#   sudo apt install cmake libsdl2-dev zlib1g-dev libgl1-mesa-dev
# Example (macOS):
#   brew install cmake libsdl2-dev
# Example (Windows/MSYS2):
#   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 \
#             mingw-w64-x86_64-zlib mingw-w64-x86_64-cmake
#
set -euo pipefail

ROMID="${1:-ntsc-final}"
BUILD_DIR="${BUILD_DIR:-build-pc}"

# ---------------------------------------------------------------------------
# Windows toolchain env guard (see AGENTS.md "Build" — the recurring
# "Cannot create temporary file in C:\Windows\: Permission denied" link
# failure).
#
# The PE toolchain (ninja -> cmd -> gcc/ld) creates temp files under the
# child's TMP/TEMP. A full MSYS2 MINGW64 login shell arranges that for
# native children; other shells (agent harnesses, non-login shells) do not
# -- the msys->native env conversion silently drops/breaks TMP and the
# linker falls back to C:\Windows\. Probe what a native child actually
# sees (via a file: msys console emulation makes piped cmd.exe stdout
# unreliable), and if broken, re-run the cmake+build steps under
# PowerShell. Crucially the temp vars are set INSIDE PowerShell: the broken
# boundary is msys->native, while native->native (powershell -> cmake ->
# ninja) passes env vars through intact.
# GE_PC_BUILD_VIA_NATIVE=1 prevents a re-exec loop.
# ---------------------------------------------------------------------------
# Locate the MSYS2 MinGW64 toolchain dir (cmake.exe + gcc.exe, inside an
# MSYS2 install). Whatever `cmake`/`gcc` happen to be first on PATH is not
# trustworthy: Git Bash's /mingw64 has no toolchain, and a pip-installed
# cmake in a Python Scripts dir can shadow the MSYS2 one. GE_MSYS2_ROOT
# (e.g. C:\msys64) overrides the search.
_ge_find_mingw_bin() {
    local c root
    root="${GE_MSYS2_ROOT:-}"
    [ -n "${root}" ] && root="$(cygpath -u "${root}" 2>/dev/null || echo "${root}")"
    for c in \
        ${root:+"${root}/mingw64/bin"} \
        "$(dirname "$(command -v gcc 2>/dev/null || echo /nonexistent/gcc)")" \
        "$(dirname "$(command -v cmake 2>/dev/null || echo /nonexistent/cmake)")" \
        "/mingw64/bin" \
        "/c/msys64/mingw64/bin"; do
        if [ -f "${c}/cmake.exe" ] && [ -f "${c}/gcc.exe" ] && [ -f "${c}/../../usr/bin/msys-2.0.dll" ]; then
            (cd "${c}" && pwd)
            return 0
        fi
    done
    return 1
}

if [ -n "${MSYSTEM:-}" ] && [ -z "${GE_PC_BUILD_VIA_NATIVE:-}" ]; then
    _mingw_bin="$(_ge_find_mingw_bin)" || {
        echo "ERROR: could not find the MSYS2 MinGW64 toolchain (mingw64/bin with cmake.exe + gcc.exe)." >&2
        echo "Install it (see docs/building.md) or point GE_MSYS2_ROOT at the MSYS2 root, e.g. GE_MSYS2_ROOT='C:\msys64'." >&2
        exit 1
    }
    # Failure mode 3 (AGENTS.md): the toolchain's DLL dir must be on PATH,
    # and ahead of any other cmake/gcc. Only mingw64/bin — NOT usr/bin:
    # from a Git Bash shell that would swap in MSYS2's cygpath/mktemp,
    # whose /tmp is a different Windows dir than this bash's /tmp.
    export PATH="${_mingw_bin}:${PATH}"
    # Every path that crosses to a native process lives in the build dir and
    # is converted with bash's own `pwd -W` — never /tmp + cygpath, which
    # silently disagree when two msys runtimes are on PATH (Git Bash + MSYS2:
    # the .ps1 was written to one /tmp and powershell was pointed at the
    # other; the TMP probe likewise always read "unusable").
    mkdir -p "${BUILD_DIR}"
    _bd_win="$(cd "${BUILD_DIR}" && pwd -W)"
    _bd_win="${_bd_win//\//\\}"
    _probe="${BUILD_DIR}/.ge007-tmpprobe"
    rm -rf "${_probe}"; mkdir -p "${_probe}"
    # The probe is a .cmd FILE: an inline `cmd //c "... \"path\" ..."` never
    # worked — the msys argv conversion re-escapes embedded quotes as \"
    # (which cmd does not understand), so neither ok.txt nor bad.txt was
    # ever written and every build took the re-exec. It also tests that TMP
    # is WRITABLE, not merely that it exists (the failure mode is TMP =
    # C:\Windows\, which exists). %~dp0 = the probe's own dir: no paths
    # cross the boundary. CRLF: cmd's goto/label scan is unreliable on LF.
    sed 's/$/\r/' > "${_probe}/probe.cmd" <<'CMD'
@echo off
if "%TMP%"=="" goto bad
(echo x> "%TMP%\ge007-tmpprobe.tmp") 2>nul || goto bad
del "%TMP%\ge007-tmpprobe.tmp" 2>nul
echo ok> "%~dp0ok.txt"
exit /b 0
:bad
echo bad> "%~dp0bad.txt"
CMD
    /c/Windows/system32/cmd.exe //c "${_bd_win}\\.ge007-tmpprobe\\probe.cmd" >/dev/null 2>&1 || true
    if [ ! -s "${_probe}/ok.txt" ]; then
        command -v powershell >/dev/null 2>&1 || {
            echo "ERROR: native TMP is unusable from this shell and powershell.exe was not found for the fallback." >&2
            echo "Manual fix: run cmake+ninja from cmd/PowerShell with C:\\msys64\\mingw64\\bin on PATH, or" >&2
            echo "set the user env vars (setx TMP \"C:\\msys64\\tmp\"; setx TEMP \"C:\\msys64\\tmp\") and re-open the shell." >&2
            rm -rf "${_probe}"; exit 1
        }
        echo "==> native TMP unusable from this shell; re-running cmake+build via PowerShell"
        export GE_PC_BUILD_VIA_NATIVE=1
        _here="$(pwd -W)"
        # Windows-side toolchain paths: from the validated MSYS2 mingw64/bin
        # found above (NOT blindly from `command -v cmake`, which in a Git
        # Bash / agent shell resolves to e.g. a pip-installed cmake and fed
        # the .ps1 a bin dir with no MSYS2 toolchain — the "re-exec fails
        # in-script, works by hand with explicit paths" bug). usr/bin first
        # on the .ps1's PATH: mingw tools may need msys-2.0.dll from there.
        _msys_root="$(cd "${_mingw_bin}/../.." && pwd -W)"      # msys install root (bin -> mingw64 -> root)
        _bin_w="$(cd "${_mingw_bin}" && pwd -W)"
        _bin_w="${_bin_w//\//\\}"
        _usrbin_w="${_msys_root//\//\\}\\usr\\bin"
        # The PowerShell script is written to a .ps1 FILE, not passed via
        # -Command: the msys->native argv conversion mangles $-bearing
        # strings (a '\$LASTEXITCODE' check arrived as an empty token and a
        # failed cmake would have exited 0 silently). A file write is
        # conversion-free; only plain path/word arguments cross the boundary.
        _ps1="${BUILD_DIR}/ge007-native-reexec.ps1"
        _ps1_win="${_bd_win}\\ge007-native-reexec.ps1"
        cat > "${_ps1}" <<'PS1'
param([string]$UsrBin, [string]$Bin, [string]$MsysRoot, [string]$Here, [string]$BuildDir, [string]$RomId)
# Self-logging: a headless shell loses this process's console output, so
# every fact that matters goes to diag/log files in the build dir.
# Resolve BuildDir against the repo root, not the inherited cwd.
Set-Location $Here
$diag = Join-Path $BuildDir "ge007-native-reexec-diag.log"
$cmakeLog = Join-Path $BuildDir "ge007-native-reexec-cmake.log"
$buildLog = Join-Path $BuildDir "ge007-native-reexec-build.log"
# The inherited native PATH may be mangled/dropped by the broken msys->native
# env conversion, so build it explicitly: the toolchain only needs the two
# msys bin dirs plus the usual system dirs. (usr/bin first: mingw gcc/cmake
# need msys-2.0.dll from there.)
$env:PATH = "$UsrBin;$Bin;C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
# A PowerShell launched from an MSYS2 bash (the agent-shell case: MSYS2's
# usr/bin first on PATH, so `env bash` is MSYS2's bash) inherits a stripped
# ~13-var env with PATHEXT=".CPL" and no ComSpec. With that PATHEXT,
# `& cmake.exe` silently does NOTHING: no process, no output, $LASTEXITCODE
# stays $null (the rc=2 "did not execute" regression). Restore them.
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
if (-not $env:ComSpec) { $env:ComSpec = 'C:\Windows\System32\cmd.exe' }
# D515: same class of bug as PATHEXT/ComSpec above — a stripped re-exec env
# can also drop PROCESSOR_ARCHITECTURE, which CMake reads to fill
# CMAKE_HOST_SYSTEM_PROCESSOR; empty value -> empty TARGET_ARCH -> "ge007..".
# Restore it from the machine environment (fallback 'AMD64'), and restore
# SystemRoot/windir the same way (standard Windows vars native tools expect).
if (-not $env:PROCESSOR_ARCHITECTURE) {
    $env:PROCESSOR_ARCHITECTURE = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE', 'Machine')
    if (-not $env:PROCESSOR_ARCHITECTURE) { $env:PROCESSOR_ARCHITECTURE = 'AMD64' }
}
if (-not $env:SystemRoot) {
    $env:SystemRoot = [Environment]::GetEnvironmentVariable('SystemRoot', 'Machine')
    if (-not $env:SystemRoot) { $env:SystemRoot = 'C:\Windows' }
}
if (-not $env:windir) {
    $env:windir = [Environment]::GetEnvironmentVariable('windir', 'Machine')
    if (-not $env:windir) { $env:windir = 'C:\Windows' }
}
# Writable temp for the native toolchain. [System.IO.Path]::GetTempPath()
# CANNOT be used here: it honours the inherited (broken) TMP/TEMP env vars
# and returned C:\Windows\. Pick the first writable candidate instead.
$tmpdir = $null
foreach ($cand in @((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'), (Join-Path $MsysRoot 'tmp'))) {
    try {
        if (-not (Test-Path $cand)) { New-Item -ItemType Directory -Force -Path $cand -ErrorAction Stop | Out-Null }
        $probe = Join-Path $cand 'ge007-tmp-write-test.tmp'
        'x' | Out-File -FilePath $probe -ErrorAction Stop
        Remove-Item $probe -ErrorAction SilentlyContinue
        $tmpdir = $cand
        break
    } catch { }
}
if ($null -eq $tmpdir) { "no writable temp dir found" | Out-File $diag -Encoding ascii; Write-Host 'no writable temp dir for the native toolchain'; exit 4 }
$env:TMP = $tmpdir
$env:TEMP = $tmpdir
"start UsrBin=[$UsrBin] Bin=[$Bin] MsysRoot=[$MsysRoot] Here=[$Here] BuildDir=[$BuildDir] RomId=[$RomId] tmp=[$tmpdir]" | Out-File $diag -Encoding ascii
# D515 diag: the stripped re-exec env can also drop PROCESSOR_ARCHITECTURE,
# which CMake reads to fill CMAKE_HOST_SYSTEM_PROCESSOR (empty value ->
# empty TARGET_ARCH -> "ge007.."). One line, cheap, useful next time.
"env PROCESSOR_ARCHITECTURE=[$env:PROCESSOR_ARCHITECTURE] PROCESSOR_ARCHITEW6432=[$env:PROCESSOR_ARCHITEW6432] SystemRoot=[$env:SystemRoot] windir=[$env:windir]" | Out-File $diag -Append -Encoding ascii
$exe = "$Bin\cmake.exe"
"exe=[$exe] exists=[$(Test-Path -LiteralPath $exe)]" | Out-File $diag -Append -Encoding ascii
# A missing exe makes `& $exe` throw (caught below) and leaves LASTEXITCODE
# null, which used to surface only as an opaque "did not execute".
if (-not (Test-Path -LiteralPath $exe)) { Write-Host "cmake.exe not found at [$exe] (diag: $diag)"; exit 5 }
Set-Location $Here
"cwd=[$([System.IO.Directory]::GetCurrentDirectory())]" | Out-File $diag -Append -Encoding ascii
try {
    # -DROMID must be a DOUBLE-QUOTED string: a bare `-DROMID=$RomId` token
    # does NOT expand the variable in PowerShell (cmake got the literal
    # "$RomId" and failed with "Unknown ROMID").
    $out = & $exe -S . -B $BuildDir "-DROMID=$RomId" 2>&1
    "cmake ran LASTEXITCODE=[$LASTEXITCODE]" | Out-File $diag -Append -Encoding ascii
    $out | Out-File -FilePath $cmakeLog -Encoding ascii
} catch {
    "cmake TERMINAL ERROR: $($_.Exception.Message)" | Out-File $diag -Append -Encoding ascii
}
if ($null -eq $LASTEXITCODE) { Write-Host "cmake.exe did not execute (diag: $diag)"; exit 2 }
if ($LASTEXITCODE -ne 0) { Write-Host "cmake re-configure failed (rc=$LASTEXITCODE; log: $cmakeLog)"; exit $LASTEXITCODE }
Write-Host "cmake reconfigured (TMP=$($env:TMP))"
try {
    $out = & $exe --build $BuildDir -j 2>&1
    "build ran LASTEXITCODE=[$LASTEXITCODE]" | Out-File $diag -Append -Encoding ascii
    $out | Out-File -FilePath $buildLog -Encoding ascii
} catch {
    "build TERMINAL ERROR: $($_.Exception.Message)" | Out-File $diag -Append -Encoding ascii
}
if ($null -eq $LASTEXITCODE) { Write-Host "cmake --build did not execute (diag: $diag)"; exit 3 }
if ($LASTEXITCODE -ne 0) { Write-Host "build failed (rc=$LASTEXITCODE; log: $buildLog)"; exit $LASTEXITCODE }
"done" | Out-File $diag -Append -Encoding ascii
Write-Host "cmake + build ok (native re-exec, TMP=$($env:TMP))"
PS1
        rm -rf "${_probe}"
        rc=0
        # NOTE: do NOT launch powershell via `env -i` (tried 2026-09-28): a
        # native child started with a scrubbed msys env cannot launch
        # cmake.exe (CreateProcess fails silently; LASTEXITCODE stays null).
        powershell -NoProfile -ExecutionPolicy Bypass -File "${_ps1_win}" "${_usrbin_w}" "${_bin_w}" "${_msys_root}" "${_here}" "${BUILD_DIR}" "${ROMID}" || rc=$?
        rm -f "${_ps1}"
        if [ "${rc}" -ne 0 ]; then
            # Surface the .ps1's own diagnostics (console output is lost in a
            # headless shell) plus whatever the failing step wrote.
            for _lg in "${BUILD_DIR}/ge007-native-reexec-diag.log" "${BUILD_DIR}/ge007-native-reexec-cmake.log" "${BUILD_DIR}/ge007-native-reexec-build.log"; do
                if [ -s "${_lg}" ]; then
                    echo "---- ${_lg} (last 40 lines) ----" >&2
                    tail -40 "${_lg}" >&2
                fi
            done
        fi
        exit "${rc}"
    fi
    rm -rf "${_probe}"
fi

echo "==> Configuring PC port (ROMID=${ROMID})"
cmake -S . -B "${BUILD_DIR}" -DROMID="${ROMID}"

echo "==> Building"
cmake --build "${BUILD_DIR}" -j

echo "==> Done."
echo "    Binary: ${BUILD_DIR}/ge007.*"
echo "    Put your ROM in ./data/ (see README) and run the binary."
