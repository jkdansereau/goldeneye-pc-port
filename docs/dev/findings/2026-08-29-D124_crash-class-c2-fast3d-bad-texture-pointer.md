# D124

<!-- from the old section-F/H table row -->
| D124 | **crash class C2: fast3d bad texture pointer.** — full `## D124` entry at file tail | Jungle fixed (`port/src/gimgfixup.c`); Facility → D130 |

## D124 — crash class C2: fast3d bad texture pointer.

crash class C2: fast3d bad texture pointer. **Jungle** (`0xabcd0824`): `gimgSyncCompiledGlobalDLs()` slot-detect keyed on the post-fixup marker, which `texLoad()` had already overwritten → compiled `globalDL_0xNNN` explosion DLs kept link-time `IMAGESEG` words → latent on every level, tripped by the first explosion-DL draw. **Facility** (`0x72181ee8`): see **D130** — the model-GDL-relocation hypothesis (M-14 addendum) was WRONG; real cause was `romdataFixupFont`

**Status (from §F table):** Jungle fixed (`port/src/gimgfixup.c`); Facility → D130
