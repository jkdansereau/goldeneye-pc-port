@echo off
rem ---------------------------------------------------------------------------
rem ci-shell.cmd -- CI launcher for the box's own MSYS2 MINGW64 toolchain.
rem
rem Used as a GitHub Actions custom shell on the self-hosted Windows runner:
rem     defaults.run.shell: C:\msys64\ci-shell.cmd {0}
rem
rem It reproduces, headlessly, what the "MSYS2 MINGW64" Start Menu terminal
rem gives you locally -- MSYSTEM=MINGW64 with /mingw64/bin on PATH -- but as a
rem NON-login bash so it does NOT cd to $HOME (a login shell would abandon the
rem workspace cwd and break every relative path in the workflow), and with
rem -e -o pipefail so a failing command fails the step like GitHub's built-in
rem bash shell does.
rem
rem Kept in the repo as the source of truth; each job copies it into C:\msys64
rem (a fixed absolute path, identical on every box that follows the rollout
rem plan) via a bootstrap step, so there is no per-run install and no drift.
rem ---------------------------------------------------------------------------
setlocal
set "MSYSTEM=MINGW64"
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
C:\msys64\usr\bin\bash.exe --noprofile --norc -e -o pipefail %*
exit /b %ERRORLEVEL%
