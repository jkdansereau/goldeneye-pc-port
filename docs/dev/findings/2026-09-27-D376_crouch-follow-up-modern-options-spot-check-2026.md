# D376

<!-- from the old section-F/H table row -->
| D376 | **Crouch follow-up + modern-options spot check (2026-09-27).** full `## D376` entry at file tail | **CLOSED (2026-09-28).** Shipped in v0.4.0; accepted in the user's final review / campaign playtest. |

## D376 — Crouch follow-up: control-style axis and pad mapping (2026-09-27)

**Diagnosis:** The 1-controller branch in `bondview2.c:5579` reads C-down
for BOTH 1.1/HONEY and 1.2/SOLITARE. The stick-Y crouch at 5318 is the
**two-controller** branch; it does not apply to the port's single-controller
layout. The existing gamepad B/Y mapping in `port/src/input.c` still sent B
in stages (never crouch). Also D375's keyboard C-down was emitted in menus
and outside aim, where it can navigate or move backwards instead.

**Fix (port only):** keyboard Ctrl and pad B/Y feed one hold/toggle latch;
in-stage, while aiming, send C-down for both single-controller schemes.
Do not emit crouch/navigation input in menus or outside aim; B/Y retain
B/cancel in menus. Drop the latch at menu/overlay transitions. No game-code
edits. The engine still *requires aim* for crouching; releasing aim before
releasing crouch may leave the posture down until aim resumes (N64
constraint, not a promise that Ctrl alone crouches). Some weapons disable
crouch and use down for zoom instead.

**Spot check of M2/M3/M5:** M2 hide/tint hook defaults to the original white
render and keeps the non-PORT branch unchanged (`gunfire.c:gunDrawSight`);
M3 E and R both send context-sensitive B, **not** a guaranteed
interaction-free reload (deferred v2 reload gate in the plan); D374
preset layering treats N64-default strings as unset, which also means an
explicit custom binding *equal to* the N64 default cannot override GEPD;
for the default-empty Reload/Crouch keys, an empty string likewise cannot
express an intentional unbind of the GEPD preset (the ini writer stores
empty for both "unset" and "explicitly unbound"). This needs a separate
config migration/presence design before promising full rebind semantics.
M5 low-end row writes 30fps/MSAA 1 on selection and restores 60/2 on
deselection (does not remember custom values); the individual settings
are stored separately, so `LowEndMode` describes the last preset selection,
not an enforced invariant after subsequent ini edits. No other code fix
made in this spot check.

**Verification:** touched object compiles (two pre-existing warnings);
linked alternate `build-pc/ge007.review.exe` because the normal exe was
locked by a running process. Scripted `-level_33` boot via
`GE_INPUTSCRIPT='60:RHOLD;90:CDOWN'` with `GE_INPUTLOG=1` observed
`btn=0014` (R+C-down) during pulses, normal self-quit at frame 140.
This tests native pad delivery, **not** live SDL keyboard or physical
pad input. **Superseded by D377:** user confirmed GEPD freely crouches
without aim; the aim gate here was wrong for the GEPD preset and prevented
the requested feature from working as advertised.
