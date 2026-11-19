# D617

<!-- from the old section-F/H table row -->
| D617 | **The HD pack's `ge007.tdb` is the emulator's full per-draw key list, not the pack's PNG list — non-CI rice keys are per draw tile, so the join needs display-list tile parameters** — ROADMAP #100 rice-pack stage (tooling); full `## D617` entry at file tail | OPEN — plan written (`docs/dev/TEXPACK-RICE-DRAW-PLAN.md`); whole-texture recipe confirmed correct by 9 tdb hits with exact `;WxH` agreement; PNG-filename and brute-force param searches are dead ends |

## D617: The HD pack's `ge007.tdb` is the emulator's full per-draw key list — non-CI rice keys are per draw tile, so the join needs display-list tile parameters (ROADMAP #100, rice-pack stage)

**Status:** OPEN 2026-10-06 — investigation landed, plan written
(`docs/dev/TEXPACK-RICE-DRAW-PLAN.md`); no code change yet. Tooling-only,
`feature/texpack` worktree.

**What this corrects in D616.** D616 assumed the pack targets a recompile
whose texnums are unreachable, and treated the pack's PNG filenames as the
join target. The pack page says otherwise: it is a **GLideN64 HTS pack for
emulators** (Project64 + mupen64plus; "HTS file goes to:
Project64/Plugin/GFX/cache"), and `ge007.tdb` is the **emulator-side
registry of every texture key**, 35517 lines, of which the 2046 pack PNGs
are a small subset.

**Findings.**

1. **Join against the tdb, not the PNGs.** Parsing the tdb's 2567
   non-CI keys (`GOLDENEYE#<crc>#<fmt>#<siz>_all;WxH`, hex upper-case,
   `_all` suffix must be stripped before splitting on `#`) and hashing the
   whole stored texture with the D616 recipe (`rice_hq` BE variant over
   pool bytes at decode dims and pool stride) yields **9 hits** where the
   PNG-filename join yielded **0**. The 1491 non-CI PNGs are mostly added
   HD content (Effects/Explosion ×325, Smoke ×312, Gunbarrel ×299, UI
   letters, character hacks) — they are not vanilla replacements, which is
   why the PNG join is empty.
2. **The recipe is confirmed correct**, not merely close: for the 9 hits
   the tdb's `;WxH` equals the decode dims exactly, and the fmt/siz map
   (`TEX_GBI_FMT`/`TEX_GBI_SIZ`) and the pool-stride formula agree with
   `texGetLineSizeInBytes(tex,0)*8` in `src/game/tex.c` (the emulator's
   `line<<3`).
3. **Why 9/953 and not ~953**: the tdb carries ~2.7 keys per vanilla
   texture. GLideN64 hashes **per draw tile** —
   `src = RDRAM + tileAddr + ul_t*bpl + ((ul_s<<siz)+1)>>1`,
   `RiceCRC32(src, tile.width, tile.height, fmt<<8|siz, bpl)`,
   `bpl = tile.width<<siz>>1`, `rowStride = tile.line<<3` — and most GE
   draws set a display-list `G_SETTILE` whose width/height/`ul_s`/`ul_t`
   differ from the stored texture (sub-rectangles, tiled draws, repeats).
   `bpl` (the inner word-walk extent) and `rowStride` (the row advance)
   are different values for those draws.
4. **Brute force is not the answer.** Exhaustive `(w,h,stride,offset)` and
   BE/LE/swizzle/nibble-swap/decoded-pixel sweeps over the strongest
   content pairing (texnum 2214, I4 128×48, key `1478129A`) score 0 — the
   tile parameters must be read from the display lists, not searched for.
   The C oracle for the ROM-side decode is also not the unblocking step:
   the pool bytes are already content-verified against the pack PNG.

**Next step** is the display-list walk in the plan doc (reuse
`tools_pc/d43_gdldump.py` / `tools_pc/bg_gdl_census.py` framing); the tdb is
the oracle for validating the resulting key set before touching the pack.
