# D564

<!-- from the old section-F/H table row -->
| D564 | **F10: game's own "Look up/down" row removed (dead for mouse and centred pad, stacked with Invert look); value pinned to factory; tip polish** — full `## D564` entry at file tail | FIXED 2026-10-06 (port only; Mouse / Controller "Invert look" are the inversion controls) |

## D564: F10 "Look up/down" removed; game value pinned to factory; tip polish — FIXED 2026-10-06 (maintainer decision)

**Report:** maintainer: changing Look up/down (in-level F10 and front end) had
no visible effect. **Trace (code, file:line in the D562/D563 session report):**
the row wrote the game's `OPTION_INVERTLOOK` (`set_cur_player_look_vertical_inverted`),
read only at `src/game/bondview2.c:4938` (`invertPitch`) on the game's own
stick/C-button pitch path. The port's mouse look (D300 `hipDirectCompute`,
aim models) and centred-aim pad look (`padDirectCompute`) write `vv_verta`
directly and never consult it; only pad look in the N64 aim style passes
through it, where it stacks with Controller "Invert look". Perfect Dark's port
instead routes mouse and pad through its single "Reverse Pitch" option; GE's
port already has per-device Invert look rows (the usual PC model), so the
maintainer chose removal.

**Fix (port only):** removed the `Bond.Look` row and its value names
(`port/src/optionsoverlay.c`); `port/src/watchsettings.c` pins the game's
value to its factory default 0 each stage tick (blank profiles load 0:
`DEFAULT_OPTIONS` has no `OPTION_INVERTLOOK`, `src/game/file2.c:1357`), so
every default profile behaves exactly as before and a value saved by an older
build or set in the in-game watch menu cannot silently flip pad pitch. Mouse /
Controller "Invert look" are the only inversion controls. Same pass, tip
polish: All unlocked "Unlocks all levels, 007 mode and every cheat.", Show
crosshair, FOV and Aim style tips rewritten as plain sentences. Verified: both
TUs compile (link was blocked by a running game; full build + re-cut follow).
