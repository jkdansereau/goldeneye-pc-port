# D554

<!-- from the old section-F/H table row -->
| D554 | **F10 settings menu: wording/casing pass + two-line tips** — display text only, config keys unchanged; tips wrap instead of ".." — full `## D554` entry at file tail | DONE 2026-10-06 (port/src/optionsoverlay.c; maintainer by-eye check owed) |

## D554: F10 settings menu wording/casing pass + two-line tips — DONE 2026-10-06

**What.** Display text only in `port/src/optionsoverlay.c` (config keys, the
kRowHelp lookup keys and every value array's order/count are untouched; values
are stored as indices, never as display text). No `src/game` change.

**Style rules.** American spelling. Page titles and hub entries Title Case
(hub actions now "Quit to Desktop", "Restart Game"); row labels and value
names sentence case ("3-point"); parenthesised tags lower case ("(per
profile)", "(no profile)", "(restart)"); tips sentence case, no trailing
period, plain third-person English, never repeating the label; control hints
are `Key: Action` pairs, three spaces between pairs, `/` between alternative
keys with no spaces ("A/Enter: Select", "B/Esc/F10: Close"); slashes inside
labels have no spaces ("Pause/start"). "Original" = the stock N64 behaviour
wherever a player picks it; "N64" only names hardware; "PC" is never a value;
"Extended" is the opposite of Original. "deg" kept for FOV (the font has no
degree sign).

**Maintainer decisions applied.** Aim style / Aim range values are now
Original / Centered and Original / Extended. The "(EXPERIMENTAL)" tag is
removed from Skip intro and All unlocked, with the red-ink path
(`strstr(label,"EXPERIMENTAL")`, `PD_EXP_*`) deleted so nothing depends on the
tag. "Controller style" is now "Original layout". Binding action names: the
pad row "Use / action" is renamed to match the keyboard's "Use/back". "Ext"
is now "Extended" (Control scheme), "Velocity (legacy)" is "Velocity (old)",
"(in a mission)" is "Not in a mission" (the value shows when no live player
exists), the ini warning reads "n unknown settings in the ini, see the log".
All 44 changed tips follow the plan's table; a tip was added for
`Video.CrosshairHide`.

**Code facts confirmed.** Esc closes the hub: `optionsOverlayBack()` sets
`s_backPending`, and the update loop calls `optionsOverlayToggle()` when no
page is open (so the hub hint is "B/Esc/F10: Close"). "Show crosshair"
(`Video.CrosshairHide`, portCrosshairHide) is a port-level override:
`gunDrawSight` returns early when it is set, regardless of "Sight on screen"
(`Bond.Sight`, the game's own per-profile sight-visible reason). The tip says
exactly that.

**Wrap.** Tips no longer go through a 64-byte buffer with ".." chopping.
`tipWrap()` measures with the overlay's own `bodyWidth()` against
`cardW - 8`; if too wide it splits at the last space that fits and the second
centred line is drawn in the hint panel. The panel was already sized for it
(`hintLines*12+2`; 2 tip + 2 key-hint lines = 50 <= `OV_HINT_H` 53), so no
layout constant changed; the key hints move down by the actual tip line count.
A line 2 that still overflows is chopped with ".." (cannot happen for current
tips). `tipSelfCheck()` runs once on the first emit that has fonts and logs
a warning by key for any tip needing more than 2 lines at HUD 150 (budget
189), plus one summary line.

**Review fixes (same day, from the lead's HUD 150 screenshots).**
(1) Control hints clipped at HUD 150. Every hint line is now a `{full, compact}`
pair (`kHint[]`); the compact form is used only when the full one exceeds the
card budget (189 at HUD 150), then the Xbox letters as a last resort. Compact
forms: "A: Select   L/R: Change", "B: Back   F10: Close", "A: Bind   L/R: Slot
  Y: Clear", "Keys/mouse only   L/R: Slot", "Enter: Bind   B: Back", "Press a
button   Hold B: Bind B", "Tap B: Cancel   Hold BACK: Clear", "Press key or
mouse 1-5", "B: Cancel   Del: Clear"; the hub lines already fit. The one-time
check now also logs any hint whose full form (Xbox names) exceeds 296 or whose
compact form exceeds 189.
(2) Game page tip showed only "Reverse makes pushing up look" at HUD 150.
Root cause was not the page: `drawBody` passed `textRender` the raw canvas
width/height as its clip limits, but `textRender` clips in unscaled glyph space
while the string is drawn through the 78% span scale about its own origin.
A line that starts right of centre and is wide enough (here a 187-unit
one-line tip, which `tipWrap` correctly kept on one line) ran past 213 in
unscaled space and lost its tail. `drawBody` now maps the limits back into
unscaled space (`x + (ovW - x) * 100 / 78`, same for y). This affects every
overlay string, none for the worse.

**Owed.** By-eye contact sheet of every page at HUD 100 and 150. Not done:
the slider-bar shrink at HUD 150 (labels still ellipsise there).
