# D636

<!-- from the old section-F/H table row -->
| D636 | **Non-CI pack textures ARE fingerprint-keyed: the D617/D620 census only alt-row-swapped explicit-LOD textures, but the game swaps every texture (D149); swapping all gives 203 exact non-CI joins (was 17), and those overrule 30 picture-join picks (near-identical variants on the wrong texnum, e.g. 948/949, 35/36, 284/307)** - `tools_pc/texpack/rice_nonci.py`, `gehd_join.py nonci`, `gehd_join.csv` (+203 rows); full `## D636` entry at file tail | FIXED 2026-10-10 (`feat/texpack-fp`): exact join 256 CI + 203 non-CI = 459 texnums; local pack rebuilt exact-first (511 files) |

## D636: the non-CI fingerprint join was one swap rule away - 203 exact joins, and they correct the picture join

**Status:** FIXED 2026-10-10 on `feat/texpack-fp` (off `release/v0.7.0`);
tools + join table only, no port change. Follows D635.

**Cause of the old 0/17.** `rice_nonci.py` (D617) hashed the alt-row-swapped
pool only for textures with explicit LODs; everything else was hashed
unswapped. The game runs `texSwapAltRowBytes` on every level of every texture
(D149), so the RDP - and an emulator's fingerprint - always sees swapped
rows. With the swap applied to every texture the census (953 non-CI
textures) matches **203** texnums against the pack's `crc#fmt#siz` keys (17
before). The load-width variant tested alongside made no difference here
(unlike the font, D635). D540/D620's "the pack hashes its own re-encoded art"
reading was wrong for the same reason D635's first probe was: an emulator
looks a replacement up by the original texture's fingerprint.

**Cross-check.** Of the 203, 136 had an independent picture/vision/maintainer
join: 122 agree, 14 disagree. Every disagreement is a near-identical variant
on the wrong texnum (35<->36, 284<->307, 145/146, 948/949, 1088/1089,
440/441, 715/716, ...): a 32-bit CRC + format key cannot produce a neat swap
of two siblings, so the picture join was wrong. Rebuilding the local pack
exact-first dropped 30 picture picks whose image is exactly another
texture's; 61 matcher labels (D631) were corrected to the exact owner.

**Result.** `gehd_join.csv` now carries 459 exact rows (256 CI + 203 non-CI,
method `exact-crc-nonci`), built by `gehd_join.py nonci <packroot>
<rice_nonci.csv> <out>`. Local pack: 511 texnums (459 exact + 52 picture
picks that no exact join contradicts). Lesson for the join: run the
fingerprint join first and use picture matching only for what it cannot key
(it has no reach into textures whose pack art is keyed by a different load).
