# D374

<!-- from the old section-F/H table row -->
| D374 | **GEPD becomes the default key layout; preset layering fix (v0.4.0 M3 playtest feedback, 2026-09-27).** full `## D374` entry at file tail | FIXED (headless + user live-tested): GEPD playtest found Ctrl fired the gun (stale persisted [Input.Bind] Fire=Left Ctrl masked the preset) and the FPS default made no sense for a mouse FPS. D374: GEPD is now Input.Layout=0 (the default; N64 layout = 1); only EXPLICIT per-key overrides beat the preset. One-off ini flip: pre-D374 `Layout = 1` (ex-GEPD) is now 0. |

## D374 — GEPD becomes the default key layout; preset layering fix (v0.4.0 M3 playtest feedback, 2026-09-27)

**Trigger:** first GEPD playtest (user, 2026-09-27): "Crouch is broken,
in GEPD mode ctrl shoots the gun" + "why is FPS different than GEPD? GE
is a first person shooter ... make [GEPD] the default and call it
default."

**Diagnosis (two distinct bugs, one user-visible symptom):**
1. The M3 "preset as effective default" only applied to EMPTY
   `g_bindStr`. But the config migration writes EVERY bind key into the
   ini on clean exit, so any pre-existing ini carried the full set of
   persisted N64-layout defaults (`Fire = Left Ctrl`, `Action =
   Space,Z,E`, ...) — all non-empty — and the preset was silently dead
   for real users. Switching to GEPD left `Fire = Left Ctrl` in force
   while the preset added `Crouch = Left Ctrl` → **Ctrl fired and
   crouched at once**.
2. Naming/expectation: in the GEPD scheme (DEEPDIVE §7.1) Q = A button
   (accept/next weapon), E = B button (use/cancel/crouch/reload). The
   stale ini's `Action = Space,Z,E` kept E on the A button (weapon
   cycle), not B (use) — the user correctly expected E = USE.

**Fix (user decision 2026-09-27; port-layer only, zero game-logic
change):**
- **GEPD is now the DEFAULT layout** (`Input.Layout`: 0 = GEPD — the
  natural mouse/FPS default; the N64 never had a keyboard, so the old
  "FPS" default was always a port artifact — 1 = the N64-era keyboard
  binds, byte-identical to the pre-wave set, one click away). F10 row
  now reads "Default" / "N64".
- **True effective-default semantics** (`inputRebuildBinds`, D374
  precedence): an EXPLICIT per-key `[Input.Bind]` value (non-empty and
  ≠ the N64 default, or an explicit unbind of a default-bound action)
  wins; a persisted value that merely equals the N64 default counts as
  UNSET, so the layout preset fills it. With this, the user's existing
  ini (full of persisted defaults) gets the clean GEPD preset:
  Fire="" (LMB/RMB fire/aim), Crouch=Left Ctrl, Reload=R, Action=Q
  (A), Cancel=E (B=use) — no Ctrl conflict.
- Boot log line: `input: layout GEPD (default)/N64, crouch mode ...`
  (verifiable in `ge007.log` without an F10 screenshot).
- **One-off semantics flip:** an ini with the pre-D374 `Layout = 1`
  (which meant GEPD) now means N64. No in-code migration marker exists
  (the value is ambiguous); the dev ini was flipped to 0 by hand —
  note it in the v0.4.0 release notes for users who had selected GEPD.

**Verification:** user ini (stale binds, `Layout = 0`): boot log
"layout GEPD (default), crouch mode hold", no "no valid keys" warnings,
clean exit persists `Layout = 0`. `Layout = 1`: "layout N64". Ctrl no
longers fires (Fire unset under the preset); crouch/reload/E-use per
preset. **Owed:** user re-playtest (Ctrl crouch, E use, Q cycle) before
the wave closes.
