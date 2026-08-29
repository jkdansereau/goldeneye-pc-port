# GRAPHICS-BACKLOG — known cosmetic / rendering issues

Deferred below level-progression and crash work (user priority, M-11).
Minor graphical glitches are acceptable in the current state. Fix these
once the game is playable start-to-finish without crashes.

## Open

| ID | Symptom | Status / notes |
|----|---------|----------------|
| **D114/D116** | HUD text + ammo digits render each glyph individually X-mirrored (screen positions correct, texture content flipped). Likely also explains "inverted" guard skins / mislocated door props. | Root cause NOT found after 4 sessions (M-6/7/8/11). Every stage `textRenderGlyph`→GBI→fast3d→`buf_vbo`→GL runtime-verified non-mirrored (`GE_D116` `[D116/vbo]` probe). Contradiction unresolved. **Next attempt requires a RenderDoc/apitrace capture of one glyph texrect, or the asymmetric-1-texel-texture experiment** — do NOT re-run the static trace, and do NOT apply a global texrect S-swap (mirrors the whole screen). |
| **D74 wrap-block** | `gfx_pc.cpp:1546/1551` PORT block is dead code: guard `cms & G_TX_WRAP` with `G_TX_WRAP==0` is always false; and it indexes per-texunit `[2]` arrays with the vertex index `i` (0..2) → OOB if reached. | Latent. Harmless for CLAMP glyphs; could affect wrapped textures on triangles (room panels, model skins). Fix: `cms == G_TX_WRAP`, and compute the tile window once per tile not per vertex. Verify against `tools_pc/golden/` after. |
| **D75** | Nintendo logo mispositioned; gun-barrel Bond model absent; cast-roll character models absent. (Rareware logo OK.) | Front-end 3D model path. Partially overlaps D114 (shared mirror on asymmetric content). Triage plan in `PCPortResearch.md` §F D75. |
| **D76** | 2D disclaimer screen only partially drawn. | §F D76. |
| **D77** | No audible music on PC. | Phase 3 (audio). §F D77. |
| **D116 (HUD)** | (subset of D114) proportional-font strings + ammo counter mirrored. | Same root cause as D114. |

## Resolved (for reference)

- D70–D74: intro-logo pixels (Rareware logo renders correctly).
- D103: viewport / native-resolution half-frame.
- D105: room geometry Z-buffer never cleared (`G_CLEAR_DEPTH_EXT`).
- D106: sky-void through doorways (portal near-plane min>max).
- D107: blurry ceilings/wall-panels (LOD mip tile fabrication).
- D112: skeletal models were "3D lines" (`d43_emit.py put_f32` byte order).
