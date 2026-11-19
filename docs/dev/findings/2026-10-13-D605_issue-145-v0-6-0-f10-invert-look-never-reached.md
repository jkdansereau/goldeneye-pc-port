# D605

<!-- from the old section-F/H table row -->
| D605 | **Issue #145 (v0.6.0): F10 "Invert look" never reached the look stick in the HONEY role (game styles 1.1/1.3 -- left stick looks, right stick is the digital movement C-button source), so it was inert under the game's own original styles; and the D564 pin of the game's own watch "Look up/down" stayed on in Original mode, locking the player's native in-game control** — full `## D605` entry at file tail | FIXED 2026-10-09 (v0.6.0 line; port-only: the HONEY branch inverts the left (look) channel instead of the right, the D564 pin is lifted in the Original scheme so the watch row is live again; build green, the #145 maintainer/user live pad pass owed) |

## D605: #145 "Invert look" inert under the game's own original styles (v0.6.0) -- FIXED 2026-10-09 (port-only; live pad pass owed)

**Status: FIXED 2026-10-09 (v0.6.0 line, branch `fix/v060-pad-look-invert`).
Two port-only changes; build green (Windows, v0.5.1 base). The issue
reporter's live pad pass (Classic controls + 1.1, both invert states,
watch row) is owed; the face-button/menu-role questions of the issue are
the separate maintainer decision on the v0.6.0 ROADMAP row.**

**Bug (GitHub #145, reported on v0.5.0):** with F10 "Control style"
= Classic/Original + controller style 1.1, toggling F10 "Invert look"
(`Input.PadLookInvertY`) had no in-game effect on vertical aim, and the
game's own in-watch "Look up/down" row looked locked (it is the game's
own UI, but D564 pins the value to its factory default every tick, so
any change reverts).

**Part (a) -- the port's row missed the look stick.** In the HONEY role
(game styles 1.1/1.3; the port presets 1.2 Christmas / 1.3 Frost run it
too, the latter with the D498/D518 physical stick swap) the LEFT stick
is the analog look stick and the right stick feeds the digital
C-button movement (E/D/C/F thresholds) -- the reverse of the SOLITARE
role (1.2/1.4, where the right stick looks and is inverted on its
scaled channel already). `inputPoll` inverted the right-stick raw in the
HONEY branch, i.e. the *movement* C-button source, while the look
channel passed through untouched. Fix (`port/src/input.c`): invert the
left (post-swap look) channel `py` there, and stop inverting `ry`. The
SOLITARE branch already inverted its look channel; menus are untouched
(menu navigation always sources the left stick before this logic).
Semantics: `Input.PadLookInvertY` now inverts the stick that LOOKS in
the active role, in both schemes.

**Part (d) -- the D564 pin now respects the scheme.** D564 removed the
port UI row for the game's own "Look up/down" (`Bond.Look`) and pinned
`cur_player_look_vertical_inverted` to 0 so a stale value could not flip
the pad stick on the game's pitch path. In the Modern scheme that stays
correct (the port writes the view directly on the mouse/centred-pad
paths). In the ORIGINAL scheme the game honours the setting on the N64
stick-pitch path (`bondview2.c` builds `moveData.invertPitch` from it),
so pinning it removes the player's native control -- exactly the
"locked" watch row the issue reports. Fix (`port/src/watchsettings.c`):
gate the pin on `inputControlScheme() == 0` (new accessor over the D513
`Input.ControlScheme`); in Original mode the in-watch row is the live
control again, as on the N64, and the F10 rows still apply on top (a
player who sets both inverts twice -- their choice, as on any system
with two inversion layers).

**Verification owed (maintainer / issue reporter):** Classic + 1.1 with
a pad: F10 Invert look on/off flips vertical aim; the in-game watch
"Look up/down" row accepts its toggle and takes effect; Modern scheme
unchanged (pin still on, F10 rows as before). Build-checked only so far
(the invert path is a sign flip in the pad poll; no headless probe
covers it -- `GE_PADMAPTEST` exercises the action table, not this).
