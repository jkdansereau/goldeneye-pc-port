# D364

<!-- from the old section-F/H table row -->
| D364 | **Settings UI feedback: replace P legend; F10 slider click band misses drawn bar; input defaults visually off-centre (2026-09-27).** — full `## D364` entry at file tail | FIXED (user mostly accepted; D365 follows up on annotation size and click targets). |

## D364 — Profile scope, F10 slider hitbox and calibrated input displays (2026-09-27)

**Reported:** user likes D363's palette but not `P` / `P = PROFILE`;
controls legend reads like setting text; clicking the visible F10 slider
requires aiming above the bar; Input defaults such as deadzone and trigger
show ~23% rather than a visually centred 50/100 or 5/10.

**Diagnosis:** the F10 bar was at `rowY + 10..12` (knob `+8..14`), but
`overlayRowAtY` assigns the row only within `rowY - 8..+8`: the bar/knob
landed in the NEXT row's hit band. The input values were physically correct
(raw `MouseSensitivity=100` of 1..500; `PadDeadzone=7000` of 0..30000,
~21% full-stick travel; `PadTriggerPct=23` of 1..99), but the UI treated
them as linear full-range sliders rather than calibrated defaults. AUDIO's
`Bond.Music`/`Bond.FX` rows were watch-backed but lacked `.saveScoped`, so
the promised `(Profile N)` title was missing despite every Audio row being
per-profile.

**Fix (port-only):** move F10 slider bar to `rowY+3..5` and knob to
`rowY+1..7`, both entirely inside the row's `-8..+8` click band. The one
shared slider-bar geometry still drives dragging. Give the control legend
its own dark inset strip with muted-grey Zurich text (Bank Gothic remains
for SETTINGS); likewise use a separate font/colour for scope notes. Remove
`P`/legend. Tag each saved Gameplay toggle `(profile)` beside its label
when measured space permits; Audio is entirely per-profile, so its title
now carries `(Profile N)` (both UIs) and no per-row tags. The front dossier
also suppresses redundant inline Audio tags. Input's three calibrated
sliders now display 0–100, mapping each *unchanged* raw default to 50/100
via a two-segment monotonic map preserving raw minimum and maximum; bars,
mouse drags and the shared front options screen use that same map. A
`(50 = original)` note explains the scale. Other sliders (audio %, texture
filter x, FOV/HUD %) keep their physical units and authored defaults.
Reset still writes the original raw defaults, config/ini values are unchanged,
and input sensitivity/deadzone/trigger behavior is untouched.

**Verification:** NTSC build/link clean. Native-Windows `GE_OPTIONSOVERLAY=2`
+ `-level_09` stage screenshot at 640x480 and orderly `GE_QUITFRAME=180`
pass, with the user ini/EEPROM restored byte-identically after the run.
Mouse click/drag on real 4:3/16:9 front/stage, reset and visible tooltip
fit still need an interactive user pass. User accepted most of D364 but
requested smaller profile annotation, a `Controls:` heading with smaller
instructions, and a visible mouse-change target; see D365.
