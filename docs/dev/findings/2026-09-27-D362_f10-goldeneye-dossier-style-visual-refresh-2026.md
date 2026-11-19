# D362

<!-- from the old section-F/H table row -->
| D362 | **F10 GoldenEye dossier-style visual refresh (2026-09-27).** — full `## D362` entry at file tail | CLOSED (superseded by D363 after user playtest): paper/Zurich/gold look rejected. The pinned section heading and shared layout/hit geometry remain. |

## D362 — F10 GoldenEye dossier-style visual refresh (2026-09-27)

**Request:** restyle F10 to match GoldenEye, following
`docs/dev/F10-GE-STYLE-PLAN.md`. Existing front Options already uses the
in-game dossier; in-stage F10 needs a lighter port-owned 2D equivalent.

**Implementation (port-only):** replace the blue/black panel with a dark
scene scrim and warm paper card/shadow/rules, Zurich Bold menu ink (Bank
Gothic fallback during early font loading), MI6 gold masthead, dark-red
active values and slider fills/knobs, numbered GE-style category labels,
faint black selected-row highlights. The active section's Back/title/Profile
band stays pinned while content scrolls, so INPUT never disappears above
Mouse sensitivity. A short `P` gutter marker and footer legend retain
per-profile scoping without the old `(per profile)` label/value collision
at 320px. One `OvLayout` drives render, max rows, slider/drag coordinates,
mouse row-hit bands and close box. No config, input binding, save format,
N64 game-code, or F10 audio queue changes. `GE_OPTIONSOVERLAY=2..6`
optionally auto-opens a particular section for deterministic screenshots
(`=1` remains the category root).

**Verification:** NTSC compile/link clean; native-Windows `GE_PCDUMP`
front-end root and GRAPHICS capture plus 320x240 stage GRAPHICS capture
(no crash/overflow; orderly `GE_QUITFRAME` exit). Captures:
`scratch/d362-test/` (local, not distributed). User ini and EEPROM restored
byte-identically after each run. A final tiny title/footer polish rebuild
compiled, but the running playtest held `build-pc/ge007.x86_64.exe` open;
linked identical objects to `scratch/d362-test/ge007-d362.exe`, and the
local `scratch/playtest_v040.bat` installed that binary on the next
closed-game launch (guards against a concurrent game process; installed exe
size/timestamp match the staged build). Live human
review of palette, label fit, mouse clicks/drag/wheel, controller focus,
back/close, longest GRAPHICS labels and the F10 Audio regression is owed.
**User follow-up:** dossier/paper styling rejected; see D363 for the dark,
watch-green successor. D362 is retained as the design/iteration record.
