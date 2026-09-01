# GRAPHICS-BACKLOG — known cosmetic / rendering issues

Deferred below level-progression and crash work (user priority, M-11).
Minor graphical glitches are acceptable in the current state. Fix these
once the game is playable start-to-finish without crashes.

## Open

| ID | Symptom | Status / notes |
|----|---------|----------------|
| ~~**D114/D116**~~ | ~~HUD text + ammo digits render each glyph individually X-mirrored~~ | **CLOSED — NOT A BUG (M-33, §F D168).** The `GE_PCDUMP` PPM writer never reversed `glReadPixels` rows, so every capture was upside-down; "mirrored" was a misread of inverted asymmetric content. The game renders correctly on hardware (developer-confirmed). Writer fixed (`gfx_opengl.cpp`). "Every stage verified non-mirrored" was correct — there was no mirror. |
| **D74 wrap-block** | `gfx_pc.cpp:1546/1551` PORT block is dead code: guard `cms & G_TX_WRAP` with `G_TX_WRAP==0` is always false; and it indexes per-texunit `[2]` arrays with the vertex index `i` (0..2) → OOB if reached. | Latent. Harmless for CLAMP glyphs; could affect wrapped textures on triangles (room panels, model skins). Fix: `cms == G_TX_WRAP`, and compute the tile window once per tile not per vertex. Verify against `tools_pc/golden/` after. |
| **D75** | Nintendo logo mispositioned; gun-barrel Bond model absent; cast-roll character models absent. (Rareware logo OK.) | Front-end 3D model path. The "overlaps D114 shared mirror" note is withdrawn (M-33, D168 — D114 was a flipped capture). "Logo mispositioned" should be re-judged from a right-way-up capture; the absent-models half stands. Triage plan in `findings.md` §F D75. |
| **D76** | 2D disclaimer screen only partially drawn. | §F D76. |
| **D77** | No audible music on PC. | Phase 3 (audio). §F D77. |
| ~~**D116 (HUD)**~~ | ~~proportional-font strings + ammo counter mirrored~~ | **CLOSED — NOT A BUG (M-33, §F D168).** Same capture-orientation artifact as D114. |
| **D148** | Level-end cinematics don't play. Dam: hitting the exit trigger should show Bond rappelling down the dam (the bungee jump); instead it cuts straight to the mission-complete report. | Scripted cutscene / cinematic-camera + Bond rappel anim. Found M-27 during the first full Dam→Facility playthrough. Progression is unaffected — the report and next briefing load fine. **M-31 (§F D160):** static-traced the Dam end ailists (`ai_24`→`ai_17`, `camera_switch`→`CutsceneRecord` type-46 lookup). Diagnostic `GE_D160=1` shipped (`c95713f5`) — user runs Dam to the exit with it set; the trace pins whether the cutscene AI runs, and whether the propDef-index walk (D122/D132 family) hands `camera_switch` a bogus cutscene record. Leading hypothesis: propDef `sizepropdef` stride desync (medium confidence), not D75 anim-model. |
| **D149** | Front-end MISSION COMPLETE / mode-select 3D models (dossier, wallets) render as garbled full-screen geometry or not at all — one of the compiled sub-DLs (seg5+0x9ee4) is malformed, D146 now skips it instead of aborting. | D75 family. §F D146. |

## Resolved (for reference)

- D70–D74: intro-logo pixels (Rareware logo renders correctly).
- D103: viewport / native-resolution half-frame.
- D105: room geometry Z-buffer never cleared (`G_CLEAR_DEPTH_EXT`).
- D106: sky-void through doorways (portal near-plane min>max).
- D107: blurry ceilings/wall-panels (LOD mip tile fabrication).
- D112: skeletal models were "3D lines" (`d43_emit.py put_f32` byte order).
