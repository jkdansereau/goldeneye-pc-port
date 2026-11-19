# D377

<!-- from the old section-F/H table row -->
| D377 | **GEPD independent crouch (2026-09-27).** full `## D377` entry at file tail | **FIXED (2026-09-28 bookkeeping: residual live-check notes waived by the user; shipped in v0.4.0, no reports).** Earlier: FIXED (scripted stance verified; user says live crouch was good; pad/hold-vs-toggle specifics owed): Ctrl/B/Y now drive game stance from the port with no aim requirement. |

## D377 — GEPD independent crouch via native player stance (2026-09-27)

**User correction:** GEPD Ctrl crouches freely without aim. D375/D376
mistook N64's controller chord (aim + C-down) for the GEPD *input injector's*
dedicated crouch behavior. `docs/dev/GEPD-INPUT-PLAN.md` WI-6 had already
identified the GEPD mechanism: port-side stance flag write, not a pad bit.
The source is no longer vendored here (GPL stub in `reference/mouse-injector/`),
so no injector code is copied into the port.

**Diagnosis:** `bondview2.c:5579` gates native C-down crouch on
`insightaimmode`; `5969–5975` updates `g_CurrentPlayer->crouchpos` via
`currentPlayerAdjustCrouchPos`; `bondview.c:1543–1560` clamps it to
`CROUCH_SQUAT..CROUCH_STAND`; `bondview2.c:7038–7074` interpolates
`ducking_height_offset` from `currentPlayerGetCrouchPos()` (minimum of
`crouchpos` and `autocrouchpos`). No pad bit can request independent crouch
without the game's aim gate. This is a PC control feature, not an N64
game-code divergence: implement in the port, no rule-2 game edit.

**Fix (`port/src/input.c`):** the Ctrl and gamepad B/Y hold/toggle latch
now writes `g_CurrentPlayer->crouchpos = CROUCH_SQUAT` while active and
restores `CROUCH_STAND` on release; the game continues to animate the
height offset itself and automatic crouch still works via its separate
`autocrouchpos`. While already aiming, the port also feeds native C-down
so the game's per-tick `crouchUp` branch cannot undo the held stance; without
aim, no C-down is needed. The adapter does not force stand after a native
C-down crouch, and the native game path remains unchanged. No effect in
menus, watch, pause, death, tank, scripted camera,
or on weapons whose flags disable crouching; no B action is emitted in a
stage from pad B/Y. `GE_INPUTSCRIPT` gained CHOLD/CREL for headless stance
checks. Holding a bind through a menu/overlay drops the latch.

**Verification:** compiled and linked `build-pc/ge007.review.exe` (normal
exe still locked). On `-level_09`, `GE_INPUTSCRIPT='30:START;300:CHOLD;600:CREL'`
with `GE_INPUTLOG=1 GE_QUITFRAME=780` logged **free crouch applied
(aim=0)** and **free crouch released**, with normal self-quit. The same
run with simultaneous RHOLD/CHOLD also applied and released the stance.
A Dam `-level_33` control run kept the scripted-camera gate closed during
its intro, as intended. User later said crouch was good in live testing; which of hold/toggle and
pad B/Y were tried was not specified, so those specific checks remain open.
