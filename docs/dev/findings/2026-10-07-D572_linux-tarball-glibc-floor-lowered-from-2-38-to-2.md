# D572

<!-- from the old section-F/H table row -->
| D572 | **Linux tarball glibc floor lowered from 2.38 to 2.31: CI builds in the Steam Runtime sniper container with SDL2 2.30.0 from source** — full `## D572` entry at file tail | CHANGED 2026-10-07 (maintainer request; packaging only; X220 glibc 2.36 fresh install PASS) |

## D572: Linux tarball glibc floor 2.38 -> 2.31 (Steam Runtime sniper build) — CHANGED 2026-10-07 (maintainer request)

The `linux-build` CI job built on `ubuntu-24.04`, so the game and its bundled
SDL linked GCC's C23 `__isoc23_strtol`/`__isoc23_sscanf` redirects
(`GLIBC_2.38`): the release tarball refused to start on Ubuntu 22.04, Debian 12,
Fedora 38 and older (seen on the X220, Fedora 37 / glibc 2.36). The job now runs
in the Steam Runtime 3 "sniper" SDK container (Debian 11, glibc 2.31), pinned by
version + digest, with the SDK's gcc-14 (same compiler as before). Two SDK
gaps: its `libSDL2-2.0.so.0` is sdl2-compat (aborts with "Failed loading SDL3
library" unless SDL3 is shipped too), so SDL2 2.30.0 — the version the 24.04
build bundled and every Linux/Deck test ran — is built from the hash-pinned
upstream tarball (X11, Wayland+libdecor, PipeWire, Pulse, ALSA, udev, HIDAPI,
D-Bus/IBus all compiled in, dynamically loaded); and Debian 11's security archive
no longer serves `python3-pip`, so pip comes from a hash-pinned `get-pip.py`
(PyInstaller 6.22.3 supports the SDK's Python 3.9). Result: game `GLIBC_2.29`,
SDL `GLIBC_2.4`, `ge007-convert` built against 2.31. Verified: the container
build run by hand on the X220 (podman) — fresh install there (ROM only ->
converter -> boot -> clean quit, exit 0). No source changes; packaging only.

CI-built tarball (run 37623073542, c45dece7, the v0.5.0 packaging): floor
`GLIBC_2.29`, bundled SDL identifies as `SDL-release-2.30.0`. Fresh install
(empty folder + ROM -> converter -> boot -> clean quit, exit 0) passed on the
X220 (Fedora 37, glibc 2.36: the box the 24.04 tarball refused to start on) and
the Steam Deck; the 21-level pixel gate ran on that binary on both, 21/21 PASS
each. Shipped in v0.5.0.
