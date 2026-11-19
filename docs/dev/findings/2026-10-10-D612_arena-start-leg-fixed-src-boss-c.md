# D612

<!-- from the old section-F/H table row -->
| D612 | **Arena-start leg fixed (`src/boss.c`: PORT-only `uintptr_t start` + the direct committed address, D453 class); the next arm64 family is the D441 s32-held DRAM pointer slots (`initanitable.c` `expand_ani_table_entries`), whose `(uintptr_t)(u32)` zero-extend is base-0-only and needs `portN64ToHost`** — full `## D612` entry at file tail | PARTIAL 2026-10-07 (macos branch: Mac boot advanced `langInit` -> `alloc_load_expand_ani_table`; remaining sites owed with the #95 re-application) |

## D612: arena-start leg fixed; next arm64 truncation family is the D441 s32-held DRAM pointer slots — PARTIAL 2026-10-07 (macos branch)

*Labelled D584 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

The D611 chain is fixed at its game-side carrier instead of at the two port-side
ones: `src/boss.c` now declares `start` as `uintptr_t` under `#ifdef PORT`
(the N64 `u32` branch untouched) and takes the committed address directly,
`start = (uintptr_t)&_bssSegmentEnd`, instead of the N64 round-trip through
`osVirtualToPhysical` (returns `u32`) and `PHYS_TO_K0` (`((u32)(x))`).
Identity at `PORT_ADDR_BASE == 0`: both expressions yield `0x70050000` for
any address below 4 GiB, and the only other use of `start` is the `(u32)`-cast
size operand, which truncates identically either way — so the Windows/Linux
extent `(block - start)` is base-invariant and unchanged. This is the D453
pattern ("arena start is a host pointer; carry it in `uintptr_t`, not `s32`")
applied to the caller that D453 left behind; rule-2 class §A1 (pointer-width
carrier), no behaviour change, no N64-side change.

**Effect on the Mac:** the mempool banks are now committed addresses
(`PORT_DRAM_V1_BASE + 0x50000` .. `+0x7FF000`, inside the 8 MB V1 mapping),
`langInit` completes, and the run advances to the next family: SIGSEGV at
`0x707B50E4` inside `alloc_load_expand_ani_table` (src/game/initanitable.c).

**That family, named:** `expand_ani_table_entries` stores DRAM pointers in
**s32 slots** (`animation_table_ptrs1/2`, `*var_v0`) and D441 fixed the
dereference with `(struct anim_entry *)(uintptr_t)(u32)(*var_v0)` — a
**zero-extend of the arena-relative value**, which is only correct while the
arena sits below 4 GiB (D441's base-0 tier). On arm64 the slot must be
converted with `portN64ToHost` (add `PORT_ADDR_BASE`), i.e. exactly the
`PORT_N64PTR` territory of #95 — and the same shape recurs at the other
tier-2 sites the D441 census logged as owed (`initanitable.c` heap slots,
`mema.c`, the `struct player`/`struct hand` raw offsets in porting-notes §A1).
The `port_addr.c` implementation is still not in this branch's tree, so those
stay with the #95 re-application rather than being patched one cast at a
time under a locally invented name.
