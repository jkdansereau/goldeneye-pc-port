#!/usr/bin/env bash
#
# Verify the self-hosted runner's MSYS2 MINGW64 toolchain is present. Does
# NOT install anything — the self-hosted workflows (ci.yml windows-build's
# push/dispatch path, selfhosted.yml) assume MSYS2 + these packages are
# maintained by hand on the box (docs/dev/notes/SELFHOSTED-RUNNER-PLAN.md),
# not installed per run. Exits fast (0) when everything's already there;
# fails loudly with exactly what to install otherwise, instead of letting a
# missing package surface later as a confusing build/link error.
#
# Run from an MSYS2 MINGW64 shell:
#     tools_pc/check-selfhosted-deps.sh
#
set -euo pipefail

need_pkgs=(
  mingw-w64-x86_64-toolchain
  mingw-w64-x86_64-cmake
  mingw-w64-x86_64-SDL2
  mingw-w64-x86_64-zlib
  mingw-w64-x86_64-python
  mingw-w64-x86_64-ccache
  zip
  unzip
)

missing=()
for p in "${need_pkgs[@]}"; do
  pacman -Qi "$p" >/dev/null 2>&1 || missing+=("$p")
done

# mingw-w64-x86_64-toolchain is a meta-package; pacman -Qi on the group name
# can report "not found" even when its constituents (gcc, etc.) are present.
# Fall back to checking gcc directly for that one entry before failing on it.
if command -v gcc >/dev/null 2>&1; then
  filtered=()
  for p in "${missing[@]}"; do
    [ "$p" = mingw-w64-x86_64-toolchain ] || filtered+=("$p")
  done
  missing=("${filtered[@]}")
fi

if [ ${#missing[@]} -eq 0 ]; then
  echo "self-hosted deps OK: MSYS2 MINGW64 toolchain, cmake, SDL2, zlib, python, ccache, zip, unzip all present"
  exit 0
fi

{
  echo "Missing MSYS2/mingw64 packages on this runner:"
  for p in "${missing[@]}"; do echo "  - $p"; done
  echo
  echo "This box is expected to have these installed by hand — self-hosted"
  echo "workflows do not auto-install (docs/dev/notes/SELFHOSTED-RUNNER-PLAN.md)."
  echo "From an MSYS2 MINGW64 shell:"
  echo "  pacman -S --needed ${missing[*]}"
} >&2
exit 2
