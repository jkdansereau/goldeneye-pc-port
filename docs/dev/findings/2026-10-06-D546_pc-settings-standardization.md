# D546

<!-- from the old section-F/H table row -->
| D546 | **PC settings standardization: round defaults, vertical-degree FOV, "Gameplay view area" wording** — draw/LOD 2.0x, deadzone 25 %, mouse sens max 4x, FOV 30–90° in 5° steps (60 = N64, PD "Vert FOV" model), one-time ini migration; full `## D546` entry at file tail | FIXED 2026-10-05 (port-only; build + maintainer check owed; golden re-base batched with D543) |

## D546: PC settings standardization -- round defaults, vertical FOV, view-area wording — FIXED 2026-10-05 (maintainer-approved table)

**Ask:** N64 value = 1.0x (Original N64 preset); PC defaults on round steps
that every slider lands on; wording that reads as a setting, not a warning.

**Changes (port-only):**
- `Video.DrawDistance` / `Video.LodDistance` default 250 -> **200** (2.0x):
  initializers, `kVideoPresets` port column, reset table; D482 low-end lowering
  now keys on 200. Fog stays 1.0x.
- Stick deadzones 7000 (23 %, off the 300 step) -> **7500 (25 %)**
  (`STICK_DEADZONE`, reset table).
- Mouse horizontal/vertical sensitivity UI max 3x -> **4x**; controller look
  stays 2x (PD port: stick scale 0..2.0x; PD mouse 0..30 at default 2.5).
- **FOV:** shown as **vertical degrees, 30..90 in 5° steps, 60 = N64** in the
  middle (PD's "Vert FOV" model: vertical degrees, default 60, range 15..170).
  Horizontal degrees (D443/D357) moved with the window aspect (N64 = 75° at
  4:3, 91.5° at 16:9) and never landed round. Storage stays
  `Video.FovScale` % but is now a float so each step (100/12 %) is exact;
  old integer inis load unchanged and snap to the next mark on the first step.
- `Game.AIWideView` (D468) row relabelled **"Gameplay view area"**, values
  **Original / Extended**, tip "Screen area game logic counts as in view"
  (the old "Guard AI uses full wide view" read as a vision cheat; the setting
  picks which screen area on-screen gameplay tests use). Ini key unchanged.
- **Migration:** one-time, untouched values only (`Video.DefaultsRev`,
  `Input.DefaultsRev`, D482 "considered" pattern): draw/LOD 250 -> 200,
  deadzones 7000 -> 7500. Runs before D482.

**Goldens:** captures use the defaults, so draw/LOD 2.0x changes frames; the
re-base is batched with D543 (one round after the fog design is final).
