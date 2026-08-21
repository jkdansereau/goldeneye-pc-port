# Handoff brief — GoldenEye 007 PC port (Phase 1.5: boot to first frame)

_Paste-ready briefing for the next agent session. The authoritative detail lives in
`AGENTS.md` and `docs/PCPortResearch.md` (§H handoff, §F/D31–D32 findings); this is
the summary + the immediate task._

## Your job
Get `ge007.x86_64.exe` booting to a **rendered first frame** on PC. You are mid-Phase-1.5:
the game boots, maps the ROM, opens the window, and runs mainThread through most of
`bossInitMainthreadData()`, but currently SIGSEGVs in weapon-animation init. Work
agentically — fix → build → verify under gdb → commit at each working milestone. The goal
when the user is back: a booting game.

## Read first (authoritative, in order)
1. `AGENTS.md` — non-negotiables, critical files, build, verification ritual, phase status.
2. `docs/PCPortResearch.md` §H (handoff + plan), then §F/D31–D32 (findings).

## Non-negotiables (the ones that bite)
- N64 build files (`Makefile`, `tools/`, `rsp/`, `ld/`) — never touch.
- **Game logic is unmodified.** Hardware deps go in `port/`. Narrow exception: mechanical,
  semantics-preserving ABI/layout fixes forced by the 32→64-bit transition (embedded
  pointers → `u32` + cast at use), each documented as a D3x finding. No behavior changes.
- Region macros in `CMakeLists.txt` must mirror the N64 Makefile exactly.

## Current state (verified)
- **D31 FIXED:** langInit file-load crash gone (real-zlib `decompressdata` in
  `port/src/rzdecomp.c`; excluded `src/game/decompress.c`+`zlib.c`). mainThread boots past
  all 7 language files into `bossInitMainthreadData()`.
- **D32 Part A DONE:** `ModelAnimation` pointer fields → `u32` (N64 layout, sizeof 64),
  casts in `model.c`.
- **D32 Part B = the current blocker.** Boot dies at `boss.c:233`
  `init_weapon_animation_groups_maybe()` → … → `modelAnimReadRootMotionValue`
  (`model.c:914`) reading `desc->bitCount`, because animation entries' `bitDescriptors` /
  `bitStream` are not valid DRAM pointers after load.

## Immediate task — D32 Part B
Make `expand_ani_table_entries()` (`src/game/initanitable.c:233`) rebase **every** entry's
`bitDescriptors` (u32 @0x08) and `bitStream` (u32 @0x10) to a valid V1 address (< 0x80000000).

Gdb method (launch mode only — attach fails with error 87):
- Break at `boss.c:233` (right after `alloc_load_expand_ani_table` returns). base =
  `&ptr_animation_table->data`.
- For each `animation_table_ptrs1[i]` / `animation_table_ptrs2[i]`, check
  `*(u32*)(entry+8)` and `*(u32*)(entry+16)`.
- Two anomalies to resolve first: (a) in-memory `animation_table_ptrs1[]` reads
  `[ptr, 0, ptr, 0, …]` though the source is a dense `PTR_ANIM_*` list — reconcile that
  against the `while (*var_v0 != 0)` fixup loop; (b) entry[0]'s `bitStream` high byte is
  0x88, not 0x70. Likely a 32/64-bit or write-overlap issue in the two fixup loops.
- Prefer understanding *why it works on N64* and matching that. If a game-code edit is
  needed it's an ABI/layout change (allowed per non-neg #2) — document it as D3x.

## Standing procedure (you will hit more of these)
Every ROM-serialized struct with a pointer field faults the same way once you reach more
asset loading (models, textures, other tables). The D32 fix procedure (doc §H): at the
fault run `ptype /o <Struct>`; if a pointer field's offset/size diverges from its N64
offset comment, change it to `u32` + cast at the use sites; verify the load-time rebase
yields valid V1 addresses; rebuild; confirm the boot advances past this struct. Log each as
D3x in §F and note it in AGENTS.md phase status.

## Environment (do not rediscover these)
- MSYS2/MinGW tools are in `/c/msys64/mingw64/bin/` — **not** on PATH. Prefix every shell:
  `export PATH="/c/msys64/mingw64/bin:$PATH"`.
- Build: `./build-pc.sh ntsc-final` (incremental; full rebuild only when `port/shim/`
  changes). ROM is at `data/ge007.ntsc-final.z64`.
- gdb: **launch** mode only (`gdb -batch -ex "handle SIGSEGV stop" -ex run …`). Symbolicate
  offline with `addr2line -e build-pc/ge007.x86_64.exe -f -C <0x140000000+rel>`. Image base
  is `0x140000000` (re-verify with `info address` after a rebuild).
- Many init functions use a fake RBP — compute stack offsets from the **entry RSP**, not RBP.
- The D30 crash handler did **not** write `ge007.crash.log` for the game-thread fault this
  session; attribute crashes via gdb. (Worth a separate look later.)

## Definition of done (this milestone)
mainThread reaches `bossMainloop()` and a frame is actually drawn on the GL surface (not just
the clear color). After that, Phase 2 (fast3d CC/RM correctness vs `gmain.s`) makes it look
right. Commit + push to `origin/master` at each working checkpoint using the message style
`PC port: <phase> — <what>`.
