# D476

<!-- from the old section-F/H table row -->
| D476 | **CI4 textures keyed on their own 16-entry palette bank instead of the whole 256-entry table (D217 hash): unrelated TLUT loads no longer re-import unchanged CI4 textures** — full `## D476` entry at file tail | FIXED 2026-10-02 (local); Statue sweep CI4 cache misses 189 -> 36 |

## D476 — CI4 texture cache keyed on its own palette bank (palette re-import churn, D475 follow-up)

**Cause:** D217 keys CI textures on an FNV hash of the whole 256-entry TLUT table (`rdp.palette`). A CI4 texture decodes only its 16-entry bank (`rdp.palette + palette_index * 16`, `import_texture_ci4`), so a TLUT load into any other part of the table changed the key and forced a re-decode and re-upload of an unchanged CI4 texture.
**Fix (`port/fast3d/gfx_pc.cpp`, port-only, output-identical):** for `siz == G_IM_SIZ_4b` the key's palette hash covers only that bank's 32 bytes; CI8 (reads all 256 entries) keeps the whole-table hash. Two keys can only match when the decoded texels would be identical.
**Measured** (`GE_PWMISS` probe build, Statue sweep at the maintainer's settings, misses after frame 150): CI4 189 -> 36; total 1472 -> 1101 (the CI8 count also moved, which is run-to-run variation; its keys are unchanged).
**Not fixed here:** the bulk is CI8 textures in the arena (e.g. `0x701a2920`) whose palettes the game animates (70-155 distinct palettes over the sweep, only 48 eviction re-misses in total), so no cache can absorb them. That needs a GPU palette lookup (index texture + palette texture, with filtering done in the shader) or an in-place `glTexSubImage2D` update path; see ROADMAP §4.
