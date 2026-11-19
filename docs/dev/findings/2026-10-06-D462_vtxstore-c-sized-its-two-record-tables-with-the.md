# D462

<!-- from the old section-F/H table row -->
| D462 | **`vtxstore.c` sized its two record tables with the N64 stride 0x14 for a 24-byte `struct unk_09B7A0_struct_parent` on PC (`Vertex* unk00` widened); both are indexed at the C stride, so each overran its block (up to ~2 KB at 500 entries) into the next stage-bank allocation. Same shape as D461; found by the local-Qwen literal-stride allocation audit (ABI/layout, section A1).** Full `## D462` entry at file tail | FIXED (build + solo `-level_09` and 4P `GE_STARTMP=4` smoke runs exit clean; no crash; by-eye owed in solo). |

## D462 — `vtxstore` tables allocated with the N64 stride (ABI/layout exception, 2026-09-30)

**Cause.** `vtxstore.c` set `tmp = 0x14` and allocated `count * tmp` for two tables of `struct unk_09B7A0_struct_parent` (`Vertex* unk00; s32 unk04; s32 unk08; s16 unk0C, unk0E, unk10, unk12`). On PC the pointer widens it to 24 bytes (0x20 once D457(d) turns `unk04` into a pointer; that part is NOT merged). The init loop indexes the tables at the C stride (`...[i].unk0E = -1`), so each block was overrun by about 4 bytes per entry (about 2 KB at the 500-entry table size) into the next MEMPOOL_STAGE allocation. Silent corruption, the same class as D461. The Vertex arrays (`* 0x10`) are fine (Vertex is pinned to 16 bytes).

**Fix.** `#ifdef PORT` uses `sizeof(struct unk_09B7A0_struct_parent)`; the `#else` arm is the original. Commit `fix/abi-vtxstore-alloc`. No logic change. D457(d) (unmerged, `fix/abi-bg-model-slots`) contained the same sizeof change plus the `unk04` pointer retype; this lands only the allocation part.

**Found by.** The local-Qwen read-only audit of every `mempAlloc*` size expression (top suspects #1 and #2); the lead verified the literal at `vtxstore.c`, the C-stride indexing, and the D457(d) diff.

**Verified.** Build clean; solo `-level_09` to frame 420 and a 4-player `GE_STARTMP=4 GE_MPVIRT=3` run to frame 500 both exit rc=0 with no crash log. **Confidence: [H]** on the overrun, [M] on any visible effect (none expected). Remaining audit suspects: `size_item_buffer` weapon model buffers (`initBondDATA.c`), `model.c` rwdata word count vs converted data, `image_bank.c` +0x1000 slack: all UNKNOWN, need a converted-size check.

**Residual (pre-existing):** `inputRumble` can run on the game thread while a rescan runs on the scheduler thread in non-tick mode: one poll of exposure instead of an arbitrary host-thread moment.
