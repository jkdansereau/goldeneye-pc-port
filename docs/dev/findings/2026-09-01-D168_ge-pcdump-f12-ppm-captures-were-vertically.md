# D168

<!-- from the old section-F/H table row -->
| D168 | **`GE_PCDUMP` / F12 PPM captures were vertically flipped — the entire source of the bogus D114/D116 "HUD/text X-mirror" (M-33, developer-confirmed).** — full `## D168` entry at file tail | FIXED (`port/fast3d/gfx_opengl.cpp`); D114 / D116 reclassified as capture-orientation artifacts (below). |

## D168 — `GE_PCDUMP` / F12 PPM captures were vertically flipped — the entire source of the bogus D114/D116 "HUD/text X-mirror" (M-33, developer-confirmed).

**`GE_PCDUMP` / F12 PPM captures were vertically flipped — the entire source of the bogus D114/D116 "HUD/text X-mirror" (M-33, developer-confirmed).** `gfx_opengl_dump_bound_fbo` (`port/fast3d/gfx_opengl.cpp`) wrote `glReadPixels` output straight to a P6 PPM. GL framebuffer origin is bottom-left; PPM P6 is top-row-first — so every capture (and the `tools_pc/golden/` set, and every screenshot pasted into the finding log since M-6) was upside-down. On real hardware / an actual screen the game renders correctly (developer confirmed). The successive D114→D116 "shared fast3d mirror" investigations — each of which found *every probed stage clean yet the output "mirrored"* — were reading an inverted capture and pattern-matching upside-down asymmetric content (text, guards, the Nintendo logo) as "mirrored". Fix: emit PPM rows bottom-to-top. `tools_pc/golden/*.png` flipped in place to match (see `tools_pc/golden/README.md`); regenerate from a real run when convenient. **Not runtime-verified this session** (no ROM / toolchain in the migrated tree — see the migration note) — needs a fresh capture to confirm text reads normally. Confidence: **high** (mechanism is unambiguous; developer has hardware confirmation the screen is correct). porting-notes.md §D2.

**Status (from §F table):** FIXED (`port/fast3d/gfx_opengl.cpp`); D114 / D116 reclassified as capture-orientation artifacts (below).
