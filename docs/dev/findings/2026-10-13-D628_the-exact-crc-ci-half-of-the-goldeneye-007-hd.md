# D628

<!-- from the old section-F/H table row -->
| D628 | **The exact-CRC CI half of the GoldenEye-007-HD join is sound (256 texnums, independently re-verified), but its structural half was scored against `GE_TEXDUMP` captures taken *with a pack selected* — the dump is the pack image, so the hold-out was circular and the "same-size replacement" premise it produced is false (packs are 4x-128x upscales, D620)** — ROADMAP #100; join table `tools_pc/texpack/gehd_join.csv`, tooling `tools_pc/texpack/gehd_{join,build}.py`; full `## D628` entry at file tail | CI HALF ACCEPTED 2026-10-10 (256/256 `(crc,fmt,siz)` agreement with the rice census, 256 distinct texnums, palette-variant collision resolved by ROM palcrc, builder resolves all 256 sources); structural half VOID (same-size gate admitted only the 52 contaminated dumps, so "0 matches" was unmatchable rather than rejected; gate rebuilt aspect-ratio/integer-scale aware); no per-level dump runs needed - the non-CI route is D624 python-decoded vanilla renders + its existing human/vision queue |

## D628: the HD-pack join's exact-CRC half holds up; its structural half was scored against dumps of the pack itself (ROADMAP #100)

**Status:** CI half ACCEPTED, structural half VOID, 2026-10-10. Tooling +
join table landed on `feat/texpack-join` (`tools_pc/texpack/gehd_join.csv`,
`gehd_join.py`, `gehd_build.py`, `geh_spotcheck.py`); no port C change. The
work was produced by the maintainer's local model; this entry is the
integrator's verification of it.

**What holds (independently re-verified, no game run).** 256 join rows, one
per texnum, 256 distinct texnums (4..2696), no `Hacks/` rows. Cross-checking
every row's pack filename against `scratch/rice_ci_fresh.csv`: **256/256
agree on `(crc, fmt, siz)`** - the join is exact texel identity (D616's
big-endian RiceCRC32), which is what makes it safe without any art judgement.
The one palette-variant collision (texnum 4, `UI/Checkmark`) is resolved
towards the variant whose pack palcrc equals the ROM palcrc (`5f045394`),
with the loser logged. `gehd_build.py --dry-run` resolves all 256 sources,
0 missing, 0 `(stage,texnum)` duplicates. Coverage is 251/2046 pack PNGs
(14.1 % excluding `Hacks/`): levels whose area art the pack kept in CI join
well (Statue 12/12, Jungle 13/15, Streets 23/30), the non-CI bulk
(Effects 734, UI 558, Aztec 27) joins nothing.

**The trap.** `GE_TEXDUMP` writes the texture **the GL upload receives**, and
`texpackReplacement` has already substituted by then - so a dump taken with
`Video.TexturePack` set *is* the pack image. In `scratch/b0/` (the D622
Bunker-1 + front-end runs) **52 of 346 dumps are pack-substituted**: tn 26,
151 and 205 are byte-identical to their pack PNG (max channel diff 0), at
1024x1024 / 512x512 where the ROM texture is 32x32. All **45** CRC-joined
texnums that the `precision` subcommand used as its hold-out are inside that
52, so its "45/45 rank-1, NCC = 1.000" compares each pack PNG against a dump
of itself. It measures nothing.

**What the contamination then broke.** From those dumps the report concluded
"HD packs replace at the same pixel size (45/45 identical dims, 0 diff-dim)"
and rewrote the structural matcher to score only candidates whose dump dims
*exactly equal* the pack's. The premise is false - against the ROM census the
same 256 pairs are uniform upscales: 4x(1), 8x(1), 16x(30), 32x(135),
64x(83), 128x(6), **0 at 1x** - which is D620's own 4x-128x measurement. With
a real (vanilla) dump the true candidate is 32x smaller than the pack image,
so the exact-dims gate excludes it by construction: the reported
"0 structural matches, correctly rejected by the confidence bars" was
**unmatchable, not rejected**, and the uniform-tile top-1s it saw (texnum 668
x407, 264 x230) are the handful of other *contaminated* dumps that happened to
share the pack's dims.

**Gate rebuilt.** `Scene.scores` now admits a candidate when its dims are
`pw/k x ph/k` for one common factor - same aspect ratio, no larger - and
scores at the candidate's resolution (one LANCZOS coarse per query, BILINEAR
per target, through the pre-existing helpers). This keeps the protection the
exact-dims gate was reaching for (the 256x96 glyph *strip* vs the 64x32 glyph
that produced the confident wrong match on texnum 83 is an aspect mismatch,
so it is still excluded) without excluding every true pair. Smoke on the
contaminated dumps: candidate pool median 168 (was effectively 0), 45/45
hold-outs rank 1 - still circular on those dumps, so it proves the code path,
not accuracy.

**Non-CI is not a CRC problem (reconfirmed).** 1488 non-CI non-`Hacks` pack
PNGs against the 953-row non-CI census: **0** `(crc,fmt,siz)` hits and 0
crc-only hits - i.e. D540/D620 reproduce exactly. The report's reason ("the
pack re-encoded that content") is right; the correction is only that this is
settled, not newly open.

**Decision / next step.** The CI half ships as the `gehd` pack (exact
identity, no art judgement). The request for per-level `GE_TEXNUMDUMP` +
`GE_TEXDUMP` runs on the other 15 levels is **declined**: it would need the
pack switched off to be worth anything, and the existing D623/D624 pipeline
already scores against python-decoded vanilla renders of the ROM - no game
run at all, scale-aware, GT-tuned, with the 189-auto / 1341-sheet queue
already built. Picking that tail up is a cheap-worker job, post-release.

**One wart for whoever resumes D624:** `scratch/join_verdicts.csv` contains
**63 `auto` rows for pack PNGs that already have an exact-CRC answer**, scored
against a pool from which their own texnum was removed as already-assigned -
so all 63 name a wrong texnum. Harmless as long as exact-CRC wins the merge
(it does in `assemble`), but they must be dropped before any `apply`, and
they are not evidence against D624's GT precision.
