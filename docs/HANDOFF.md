# Handoff brief — GoldenEye 007 PC port (Phase 1.5: boot to first frame)

_Paste-ready briefing for the next agent session. The authoritative detail lives in
`AGENTS.md` and `docs/PCPortResearch.md` (§H handoff, §F/D31–D33 findings); this is
the summary + the immediate task._

## Your job
Get `ge007.x86_64.exe` booting to a **rendered first frame** on PC. You are mid-Phase-1.5:
the game boots, maps the ROM, opens the window, and runs mainThread through most of
`bossInitMainthreadData()`, but currently SIGSEGVs in weapon-animation init. Work
agentically — fix → build → verify under gdb → commit at each working milestone. The goal
when the user is back: a booting game.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual, phase status.
2. `docs/PCPortResearch.md` §H (handoff + plan), then **§F/D33** (root cause of the
   current blocker + complete fix design), D31–D32 for background.

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception: mechanical,
  semantics-preserving ABI/layout fixes forced by the 32→64-bit transition, each documented
  as a D3x finding. No behavior changes.
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state (verified this session)
- **D31 FIXED:** langInit file-load crash gone (real-zlib `decompressdata` in
  `port/src/rzdecomp.c`). mainThread boots into `bossInitMainthreadData()`.
- **D32 Part A DONE:** `ModelAnimation` pointer fields → `u32` (N64 layout, sizeof 64).
- **D32 Part B = ROOT-CAUSED (finding D33). Two independent bugs, both confirmed:**
  1. **ROM endianness.** The `.z64` file stores structured multi-byte fields in
     **big-endian byte order** (bit-packed streams are raw). The animation blob's
     20-byte record headers and descriptor arrays must be converted per-field at load
     time: u32 → bswap32, u16 → bswap16, u8 → identity. No uniform word transform exists.
     Validated in Python over all 173 ROM entries: 0 bad ranges, 0 non-sequential
     descriptor arrays; simulated root-motion frames show smooth per-frame deltas.
     The exact garbage seen under gdb (0xC82BD8C0) is reproduced by LE-reading the BE
     file bytes + rebase. Evidence: GE-specific ROM header, `tools/utils.h` "v64→z64"
     convention, build pipeline with no byte-swap step, BE toolchain defaults.
  2. **x86-64 stride bug.** `expand_ani_table_entries()` iterates with `s32** var_v0;
     var_v0++` — +4 bytes/iter on N64, but +8 on x86-64 (verified: `add $0x8,%rdx` in the
     compiled binary). Only even-indexed entries get rebased; odd ones keep raw small
     offsets (fire_standing is index 1 → would stay 0x144 → SIGSEGV on use). The earlier
     "zeros at odd indices" gdb observation was a display artifact — the compiled `.data`
     array is dense (objdump-verified). This pattern occurs nowhere else in the set.

## Immediate task — implement the D33 fix (design complete, ~3 small edits)
1. **`port/src/romdata.c` + `port/include/romdata.h`:** add
   `void romdataFixupAnimationData(u8 *blob, u32 blobSize, const s32 *tableA, const s32 *tableB)`.
   For each non-null (≠0 and ≠1 — sentinel is **1**) offset in both tables:
   - record at `blob+off` (20 bytes): bswap32 fields at +0x00 (address), +0x08
     (bitDescriptors), +0x10 (bitStream); bswap16 fields at +0x04 (frame count),
     +0x0C (bitsPerFrame root motion), +0x0E (frame size bits); bytes +0x06/+0x07
     (u8 angle width / loop flag) untouched.
   - descriptor array at `[blob+bd, blob+bs)` step 6 (`ModelAnimBitField`): bswap16
     words at +0 and +4 (bitOffset, valueOffset); bytes +2/+3 (bitCount, pad) untouched.
     Guards: skip if bd==0 && bs==0; require `bd < bs <= blobSize` and `(bs-bd)%6==0`.
   Full field semantics + layout rules in D33 (blob interleave: entry i's payload sits
   at [PTR_ANIM_{i-1}+0x14, PTR_ANIM_i); all regions disjoint).
2. **`src/game/initanitable.c`, `alloc_load_expand_ani_table()` (~line 265):** call the
   fixup **between** `romCopy(...)` and `expand_ani_table_entries(...)` — it must run
   *before* expand (expand reads/writes those fields as LE after fixup). Add
   `#include "romdata.h"` (`port/include` is already on the include path). One-line
   mechanical ABI edit — document with a D32/D33 comment.
3. **`src/game/initanitable.c`, `expand_ani_table_entries()` (line 233):** change the loop
   pointer from `s32** var_v0` to `s32 *var_v0` (cast at assignment: `var_v0 = (s32 *)arg0;`)
   so every entry is visited on x86-64. Semantics-preserving on N64. Keep the rest of the
   function body identical (`*var_v0 += base`, `((struct anim_entry *)(s32)*var_v0)->unk08/unk10`,
   and loop 2's `*(s32 *)(s32)*var_v0 += entries_base`).

**Do NOT transform the entries segment** (per-frame joint angles, file 0x124AC0,
0x169EC0 bytes) — it is identity-encoded bit-packed data, not consumed at boot; verify
visually in Phase 2 when Bond first animates.

## Verify (gdb, launch mode only — attach fails with error 87)
- Break at `boss.c:233` (right after `alloc_load_expand_ani_table` returns). base =
  `&ptr_animation_table->data`.
- For a sample of `animation_table_ptrs1/2[i]` entries (include odd indices!):
  `*(u32*)(entry+8)` and `*(u32*)(entry+16)` must be in [0x70xxxxxx] (base + blob offset),
  and the record's address field (+0x00) must be a cart address 0x10xxxxxx.
- Confirm boot advances past `init_weapon_animation_groups_maybe()` into the next init
  step of `bossInitMainthreadData`. Expect more D32-class faults further along (models,
  textures, other tables) — apply the standing procedure below each time.

## Standing procedure (you will hit more of these)
Every ROM-serialized struct with a pointer field faults the same way once you reach more
asset loading. The D32 fix procedure (doc §H): at the fault run `ptype /o <Struct>`; if a
pointer field's offset/size diverges from its N64 offset comment, change it to `u32` + cast
at the use sites; verify the load-time rebase yields valid V1 addresses; **also check the
ROM bytes' endianness per-field (D33 rule: u32 bswap32 / u16 bswap16 / u8 identity) and any
fixup loop's pointer stride on x86-64**; rebuild; confirm boot advances. Log each as D3x in
§F and note it in AGENTS.md phase status.

## Environment (do not rediscover these)
- MSYS2/MinGW tools are in `/c/msys64/mingw64/bin/` — **not** on PATH. Prefix every shell:
  `export PATH="/c/msys64/mingw64/bin:$PATH"`.
- Build: `./build-pc.sh ntsc-final` (incremental; full rebuild only when `port/shim/`
  changes). ROM is at `data/ge007.ntsc-final.z64` (= `baserom.u.z64`, byte-identical).
- gdb: **launch** mode only (`gdb -batch -ex "handle SIGSEGV stop" -ex run …`). Symbolicate
  offline with `addr2line -e build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`. Image base
  is `0x140000000` (re-verify with `info address` after a rebuild).
- `objdump -d --disassemble=<fn>` / `objdump -s -j .data` on build-pc/ge007.x86_64.exe is a
  fast way to check what the compiler actually emitted (used this session to confirm the
  stride bug and the dense table array).
- Many init functions use a fake RBP — compute stack offsets from the **entry RSP**, not RBP.
- The D30 crash handler did **not** write `ge007.crash.log` for the game-thread fault;
  attribute crashes via gdb. (Worth a separate look later.)
- Python on this box: no f-strings with nested quotes (< 3.12) — use `%` formatting.

## Definition of done (this milestone)
mainThread reaches `bossMainloop()` and a frame is actually drawn on the GL surface (not just
the clear color). After that, Phase 2 (fast3d CC/RM correctness vs `gmain.s`) makes it look
right. Commit + push to `origin/master` at each working checkpoint using the message style
`PC port: <phase> — <what>`.
