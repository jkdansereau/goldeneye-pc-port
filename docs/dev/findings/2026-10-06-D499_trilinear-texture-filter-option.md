# D499

<!-- from the old section-F/H table row -->
| D499 | **Trilinear texture filter option: `Video.TextureFilter = 3`, every texture gets generated mips at upload and samples `GL_LINEAR_MIPMAP_LINEAR`** — full `## D499` entry at file tail | FIXED 2026-10-03, maintainer-checked (no visible difference; kept) |

## D499 — Trilinear texture filter option (2026-10-03)

**Report:** maintainer playtest request ("add trilinear in addition to bilinear").
**Change (port only; commit d796b3dc by Qwen, merge 02786004):** `Video.TextureFilter = 3` "Trilinear" (F10 / PC Options Texture filter row). fast3d `FILTER_TRILINEAR` (`port/fast3d/gfx_opengl.cpp`, `port/fast3d/gfx_rendering_api.h`): every texture gets `glGenerateMipmap` at upload and is sampled `GL_LINEAR_MIPMAP_LINEAR` (min) / `GL_LINEAR` (mag). Bilinear (1) keeps mips only where the game supplies LOD tiles; 3-Point (2) unchanged.
**Note:** 3-Point already uses generated mips for minification (B1 shimmer fix), which is softer at distance than GLideN64/1964 (no generated mips).
**Verified:** build only.
**Owed:** none.
**Status:** FIXED 2026-10-03 (kept; imperceptible on the dev machine).
**Maintainer play check 2026-10-03:** no visible difference on the dev machine (2644x875, aniso 16x: most textures are magnified, and Bilinear already blends between game-supplied mips). Kept; listed after Bilinear for A/B (D502 commit: menu order 0, 1, 3, 2).
