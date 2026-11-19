# D371

<!-- from the old section-F/H table row -->
| D371 | **GEPD key-layout preset + reload/crouch bindings, v0.4.0 modern options wave M3 (2026-09-27).** full `## D371` entry at file tail | FIXED (headless-verified; awaiting user live accept): `Input.Layout` (0=FPS, 1=GEPD) + `Input.CrouchMode` (hold/toggle) in F10 INPUT; GEPD preset acts as per-key effective default under `[Input.Bind]` overrides; new IA_RELOAD/IA_CROUCH emit B (unbound by default). MODERN section dropped per D356 Turok-standard rule (rows land in functional INPUT). Boot/ini-round-trip/F10-page verified; in-game feel-check owed. |

## D371 — GEPD key-layout preset + reload/crouch bindings (v0.4.0 modern options wave, M3) (2026-09-27)

**Spec:** `docs/dev/notes/MODERN-OPTIONS-PLAN.md` M3. **Deviation from plan
(user decision, 2026-09-27):** the planned "MODERN" F10 section is DROPPED —
it is a provenance bucket, not a functional category, which conflicts with
the D356 Turok-standard "every section is functional, baseline names" rule
(the same rule that retired the D353 BOND FILE section). The rows land in
the functional section (INPUT); no new category, no `__ResetModern` — the
existing per-section reset rows cover the new keys via `kResetDefaults`.

**Implementation (port-only; `port/src/input.c`, `port/include/input.h`,
`port/src/optionsoverlay.c`):**
- New actions `IA_RELOAD` / `IA_CROUCH` (emits `GE_CONT_B` both; on N64 the
  single B button does reload-tap vs crouch-hold, and the game's own
  `bond_interact_object` context logic makes that choice — so the port
  emits B and leaves the decision to the game, as on the N64). Defaults are
  empty strings = unbound (byte-identical N64 layout out of the box).
- `Input.Layout` (0 = FPS defaults, 1 = GEPD) + `Input.CrouchMode`
  (0 = hold, 1 = toggle/latched). The GEPD preset
  (GEPORT-REFERENCE-DEEPDIVE.md section 7.3: Action=Q, Cancel=E, Reload=R,
  Crouch=Left Ctrl, Fire="" — LMB fires/RMB aims, LeanLeft="" — lean is
  GEPD-removed) acts as the EFFECTIVE DEFAULT only for actions the user has
  not individually overridden in `[Input.Bind]`; per-key ini values layer
  on top, so switching layouts never clobbers explicit binds. Revert =
  switching back.
- Toggle crouch latches B; the latch is dropped on entering a menu (a
  latched B would read as back/cancel) and on `inputLayoutApply()`.
- F10 rows: "Key layout" (kKeyLayout) + "Crouch mode" (reuses kHold) under
  INPUT, before its reset row.
- `inputRebuildBinds`: an intentionally-empty string no longer logs the
  "action unbound" warning; only a NON-empty string that yields no valid
  keys warns (D214's warning was written when no default was ever empty).
- `optionsoverlay.c` `rowSetCommit` hook: `Input.Layout` /
  `Input.CrouchMode` -> `inputLayoutApply()` (scheduler thread; a mid-poll
  rebuild can at worst drop/add one scancode for one frame — the D214
  thread-pairing, just at F10 commit time instead of boot).

**Verification:** NTSC build clean. GEPD boot (`Layout=1`, `CrouchMode=1`,
`Bind.Reload=R`, `Bind.Crouch=Left Ctrl`): no crash, no spurious warnings.
ini round-trip via orderly `GE_QUITFRAME` exit: `Layout`/`CrouchMode`
persist under `[Input]`; `Reload`/`Crouch` under `[Input.Bind]` (config.c
`splitKey` splits on the LAST dot — pre-existing D214 behaviour, binds were
always saved there with bare leaf names). F10 INPUT page
(`GE_OPTIONSOVERLAY=2`) boots clean, both rows resolve. **Owed:** live F10
layout-switch feel-check (GEPD vs FPS), in-game crouch toggle/reload B
semantics + menu-latch-reset, PAL/JP.
