# D597

<!-- from the old section-F/H table row -->
| D597 | **GE_INPUTSCRIPT gains `MLB<n>`/`MRB<n>` (sustained mouse-button tokens, slot 0): the D337 harness could script mouse motion (MDX/MDY) but no mouse *button*, so the D595-class "fire at an AI" regression had no headless recipe and the D594 unblock (LMB reaches `IA_FIRE` in a forced-level start) was unverifiable headlessly. Port-only: the bit ORs into the polled `SDL_GetMouseState` mask in `inputComputePadSlot`'s harness block; the game's own mouse bindings drive it; no change when the env is unset; the `GE_KEYLOG` probe now logs the effective mask.** — full `## D597` entry at file tail | **FIXED 2026-10-08** (macos branch; Windows menu + forced-level keylog checks; Mac sweep-fire no-crash run + no-env no-op run; the live E1 LMB fire is the struck-animation ground truth; cmd-wrapper gotcha: direct PE launch from this MSYS shell drops `GE_*` env vars — wrap in `cmd.exe //c`) |

## D597: GE_INPUTSCRIPT gains `MLB<n>`/`MRB<n>` (sustained mouse-button tokens, slot 0) — headless scripted fire; automates the D595-class regression — port-only test-harness addition, 2026-10-08

**Motivation.** D595 (the arm64 SIGSEGV on an AI's struck animation) was
reproduced only with a *human* at the mouse (aim + LMB on a guard). The
`GE_INPUTSCRIPT` harness could script mouse *motion* (MDX/MDY, D337) and
pad/keyboard buttons, but no mouse *button* — so the D595-class crash (and
any future "fire at an AI" check) had no headless recipe. It is also what
makes the D594 unblock *verifiable* headlessly (LMB now reaches the game's
`IA_FIRE` binding in a forced-level start).

**Fix (port-only, test harness):** two sustained tokens, slot 0 only
(slots 1-3 have no mouse path — ignored with the existing one-per-script
warning, like MDX/MDY):

- `MLB<n>` / `MRB<n>` — sustain LMB/RMB: `n=1` press, `0` release; latest
  entry wins (same "latest entry" semantics as MDX/MDY).
- Injection: OR'd into the polled `SDL_GetMouseState` mask in
  `inputComputePadSlot`'s D337 harness block (a headless window has no real
  mouse state), so the game's own mouse-button bindings (LMB = `IA_FIRE` in
  the mouse scheme) drive it exactly like a real press. No game-code change;
  no change when `GE_INPUTSCRIPT` is unset (the OR sits inside the
  `scriptIsActive(0)` block; the `D594` `GE_KEYLOG` probe now logs the
  *effective* mask, scripted bits included).

**Headless D595-regression recipe (Mac, `macos` tip):**
`GE_INPUTSCRIPT="<intro>:START;<aim-frame>:MDX<n>,MLB1;<release>:MLB0"`
aimed at a Dam guard — pre-D595 the same script SIGSEGVs (D595 repro);
post-D595 the guard plays its struck animation and the run completes clean.
Also usable as a runtime smoke check in #131's self-hosted sweep.

**Verification (2026-10-08, Windows, `macos` tip + this change):**
- Menu path: `GE_INPUTSCRIPT="200:MLB1;600:MLB0,MRB1;900:MRB0" GE_KEYLOG=1`
  — `ge_keylog.txt` shows the poll log stepping `LMB=1` (200–600),
  `RMB=1` (600–900), both 0 after; `GE_INPUTSCRIPT: 2 entries parsed` in
  the run log.
- In-level path (the D594 unblock): same-recipe forced `-level_09` run —
  script parsed, `LMB=1` in the poll log for the scripted window, frames
  advance, no crash.
- **Mac D595-regression run (2026-10-08, `macos` tip + this change, M3
  Mac):** `GE_INPUTSCRIPT="400:MDX60,MLB1;1000:MDX-60;1600:MDX0,MLB0"`
  (sweep right then left *while* firing) on Dam 33, `GE_PCDUMP=900-1800:300`,
  `GE_QUITFRAME=1802` — rc=0 clean `GE_QUITFRAME` exit, **no
  `ge007.crash.log`**, 4/4 frames (each a full 921 615-byte P6), and
  `ge_keylog.txt` shows `LMB=1` for the scripted window then 0 — the
  D597 injection reaches the Mac scheduler poll. A no-env
  `GE_PCDUMP=900-1500:300` run alongside: 3/3 frames, clean, no keylog
  file (the env-unset path is a no-op, as required). *Nuance:* the headless
  log cannot confirm the crosshair actually hit a guard, so the
  struck-animation ground truth is the maintainer's live LMB fire (the
  checklist's E1); the sweep-fire run is the standing automatable
  no-crash-while-firing gate (and the #131 runtime smoke check).

**Gotcha (cost two runs + two probe rebuilds):** this MSYS2 shell drops
*custom* `GE_*` env vars on a **direct PE launch** (`GE_...=1 ./build-pc/
ge007.x86_64.exe` — the native child sees a stripped env; `getenv`
returns null; same class as the documented TMP/TEMP drop, but it hit
`GE_INPUTSCRIPT`/`GE_KEYLOG` too, which looked like the script harness
wasn't polling at all). **Wrap the game in `cmd.exe //c "timeout 90
.<exe> ..."`** and every env var passes intact (verified:
`FOO=1 cmd.exe //c "if defined FOO (echo SEEN)"` → SEEN). The Mac (zsh)
and Deck launches are unaffected — native shells pass env natively.

**Status: FIXED 2026-10-08 (macos branch; Windows + Mac verified; no
game-code change, no behavior change when the env is unset; the live
E1 LMB-fire is the struck-animation ground truth).**
