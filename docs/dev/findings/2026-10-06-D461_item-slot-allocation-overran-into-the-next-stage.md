# D461

<!-- from the old section-F/H table row -->
| D461 | **Item-slot allocation overran into the next stage-bank block: `alloc_additional_item_slots` used the N64 0x14 stride for a 32-byte `InvItem` (the code comment tags it D458b); `bondinvReinitInv` wrote `p_itemcur[i].type = -1` at the C stride, scribbling 0xffffffff into the first prop model file (TT33 root Child) and crashing 2P respawn / pickups later (ABI/layout, section A1).** Full `## D461` entry at file tail | FIXED (maintainer-verified: ~10 min of 2P on Caves, Bunker and Complex, no crash). |

## D461 — `p_itemcur` allocated with the N64 stride; overran into the first prop model file (ABI/layout exception, 2026-09-30)

**Symptom.** 2P (split-screen branch) on Caves: a crash at the first or second respawn, or on a body-armor pickup, in `modelUpdateRelationsQuick` / `modelInitRwData`. The fault address `0xffffffff7022cb4c` repeated across runs: the TT33 pistol model's root Child slot had its high half overwritten with 0xffffffff.

**Cause.** `inititemslots.c` allocated `equipmaxitems * 0x14` bytes; `InvItem` is 32 bytes on PC (verified by a compile-time `_Static_assert`). `bondinvReinitInv` (`bondinv.c`) indexes `p_itemcur[i]` at the C stride and sets `.type = -1`, so the block was overrun by about 12 bytes per item (roughly 360-420 bytes at equipmaxitems 30-35) into the next MEMPOOL_STAGE allocation, which is the first prop model file loaded (TT33 on Caves). Solo levels run the same code, so this was likely corrupting something there too, silently. Not a converter or PROMOTE bug (all 340 P* models in `pcmodels.bin` were walked clean).

**Fix.** `#ifdef PORT` allocates `equipmaxitems * sizeof(InvItem)`; the `#else` arm is the original. Commit `0ebda7e8`. No logic change.

**Process.** The agent that found the corrupted node (analysis, D416-D448 branch) could not find the writer; a local Qwen read-only census of stage-bank allocations named `p_itemcur`, and the lead verified the 0x14 literal, `sizeof(InvItem) == 32`, and the write loop before applying. **Lesson (generalisable):** an allocation that multiplies a count by an N64 literal stride for a struct that grew on PC is invisible to a pointer-cast warning census; grep every `* 0x..` stride next to `mempAlloc*` and compare with `sizeof`.

**Verified.** Maintainer: no crash in about 10 minutes of 2P on Caves, Bunker and Complex (respawns including rocket-launcher kills, body-armor pickups). **Confidence: [H].** Still worth auditing: the other `mempAlloc*` sites with literal strides.
