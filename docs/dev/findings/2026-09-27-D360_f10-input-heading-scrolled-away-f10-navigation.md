# D360

<!-- from the old section-F/H table row -->
| D360 | **F10 INPUT heading scrolled away; F10 navigation and distance defaults (2026-09-27).** — full `## D360` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D360 — F10 category navigation and modern-display distance defaults (2026-09-27)

**Reported:** F10's INPUT header was offscreen above Mouse sensitivity when
scrolling; the long flat list was hard to navigate. Draw/LOD distance auto
switches obscured the controls and N64-scale pop-in looked like a port bug.
4x default MSAA was too expensive for low-end hardware.

**Fix (port-only):** F10 opens on a five-category list, and entering a
category shows its header (with Back) and settings together. Keyboard,
controller, mouse and wheel all use the same per-page list. ESC backs out of
a section, then closes from the category list; F10/Start/X still close.
Front-end PC Options already had category pages and shares the row table.
Removed AutoFov controls from both menus (ini keys remain for legacy files),
and exposed both distance sliders at all times. Stored values remain percent
of authored distance; the UI displays 0–100, where 50/100 maps to 250%.
Fresh and SteamOS defaults are 250% draw and LOD distance, 2x MSAA. Reset
uses the same new defaults. Existing ini settings are preserved; old LOD
values below the menu minimum (100%) can still be edited via ini. Note:
these higher distances increase visible geometry/pool demand; test on
open levels and low-end GPUs before claiming performance parity.

**Verification:** `git diff --check` clean. `build-pc.sh ntsc-final`
initially failed because GCC could not create temporary files in
`C:\Windows\`; configuring C/C++ with `-pipe` built all 248 objects.
The existing executable was locked by a running `ge007.x86_64` process,
so the same linker response file was linked to
`scratch/cc-tmp/ge007-check.exe` successfully. No live game was interrupted.
Manual mouse/pad/ESC, reset, and 50/100 rendering/perf checks are owed.
