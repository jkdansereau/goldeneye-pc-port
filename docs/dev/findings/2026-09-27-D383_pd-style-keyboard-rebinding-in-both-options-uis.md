# D383

<!-- from the old section-F/H table row -->
| D383 | **PD-style keyboard rebinding in both options UIs (2026-09-27).** full `## D383` entry at file tail | **CLOSED (2026-09-28 bookkeeping: keyboard/mouse rebinding shipped + accepted in v0.4.0; pad rebinding is tracked by the v0.5.0 controller wave (D394, PR #109)).** Earlier: **PARTIAL (2026-09-28).** Keyboard/mouse rebinding shipped + accepted in v0.4.0; pad-rebinding UI is the v0.5.0 controller-options wave (CONTROLLER-INPUT-PLAN). |

## D383 — PD-style keyboard rebind capture in both options UIs (2026-09-27)

**Status (2026-09-28 bookkeeping):** **CLOSED** — keyboard/mouse rebinding shipped + accepted in v0.4.0; pad rebinding is tracked by the v0.5.0 controller wave (D394, PR #109). Previous: **Status: PARTIAL (built and isolated automated checks; live keyboard-menu
acceptance and the release golden gate owed).** User explicitly requested
PD-style keyboard rebinding after the D380 single-PC-layout migration.
PD analogue: `../pd_port/port/src/optionsmenu.c:1694–1885` offers a per-action
slot dropdown, next-key capture, ESC cancel, DEL clear, and reset. GE's
`Input.Bind.*` string parser already supported four keys per action (D214)
but neither the F10 overlay nor the front options screen exposed them.
No `src/game` code changes or N64 build changes were needed.

**UI:** shared `ROW_BIND` entries in `port/src/optionsoverlay.c`, with
**MOVEMENT KEYS** (six actions) and **ACTION KEYS** (eight actions) as two
functional pages adjacent to INPUT; each fits the front menu's 14-row cap
and has a two-step **Reset to defaults** row. `port/src/frontoptions.c`
uses the same row/slot/capture API; F10 has a modal footer, the front
options screen has a modal prompt. Left/Right chooses slot **1–4** on an
action; Enter/A/click begins capture; Escape cancels, Delete clears the
selected slot; moving/clicking other controls is suppressed until the key
is released. `NONE` is displayed for empty slots. After user feedback the
ambiguous `1/4` display became **Primary / Secondary / Extra 1 / Extra 2**:
all four keys remain accessible, including older hand-edited extras. The
user's Turok hierarchy reference puts Bindings beneath INPUT; the two
standalone binding categories here are an interim layout, not final menu
acceptance. Do not add fake Weapons/Automap/Menu rows without actual separate
GE actions/input sources. The front-end `Profile`
chooser remains on every page by its pre-existing design; key bindings
are machine-wide, not attached to a save file. Mouse LMB/RMB fire/aim,
mouse wheel and gamepad controls remain hardwired and are NOT advertised
as rebindable; the on-screen instruction explicitly says mouse/pad fixed.

**Implementation:** `port/src/video.c:videoPumpEvents` consumes the first
SDL_KEYDOWN while capture is active *before* F10/ESC shortcuts; F10/F12 and
Alt+F4 are reserved, and repeats ignored. An SDL atomic flag and spinlock
pass one scancode to the scheduler/front UI thread, which alone serializes
the bind, calls `inputBindingsApply()`, and commits with `configSave()`.
Capture ignores navigation, wheel and click-to-lock; the actual captured
key is suppressed through key release. `inputBindingSetSlot()` preserves
all four existing slots (rather than discarding older hand-edited third/
fourth keys), prevents duplicate keys within an action, and never truncates
the 64-byte ini value. An explicit `NONE` token is a positional empty slot
for sparse lists (e.g. `NONE,NONE,F1`); the sole `NONE` string still unbinds
the entire action. SDL key names containing a comma or lacking a name use
`SC:<number>` in the ini, so the delimiter cannot corrupt a key. Section
reset restores the GEPD one-layout defaults.
An opt-in, restoring `GE_BINDPROBE` tests the runtime string round-trip;
`GE_BINDCAPTUREPROBE=F2|ESC|DEL` exercises the actual host-event/modal
handoff only against an isolated test ini.

**Verification:** MSYS2 MINGW64 NTSC build linked. With an isolated
`/Temp/ge-d381/data/ge007.ini`, `GE_BINDPROBE=1` logged PASS for sparse
slots 3/4 (including keypad comma), deleting slot 1 while preserving 3,
clearing all, then restoring the original strings in memory. `GE_OPTIONSOVERLAY=4` action-keys page
booted with no missing options. With `GE_BINDCAPTUREPROBE=F2` the host SDL
event was consumed and the menu committed `Input.Bind.Fire` slot 1 = F2;
`Fire = F2` appeared under `[Input.Bind]`. Repeating with ESC left F2
unchanged; DEL committed `Fire = NONE`. The real user's ini and running
process were not modified. User physical-key capture feel-check, front
options page traversal/reset, mouse/pad input after capture, other regions,
and 21-level default-render golden comparison are still owed.
