# Brief — M1 / D69: Stage (BG/stan) loading for PC

_Paste-ready milestone brief. Authoritative context: `AGENTS.md`,
`docs/HANDOFF.md`, `docs/PCPortResearch.md` §F/D69 (+D68), §G, §H._

## Mission

Get the first stage (**BUNKER1**) to load and render after the intro, without
faulting. Then extend the same conversion to **all levels'** `bg/*.seg` +
`Tbg_*_stanZ` files (all three regions). Success bar: **loads without fault +
plausible pixels** — not pixel-perfect (3D pipeline correctness is a separate
milestone, D75; do not let visual wrongness displace this one).

## Non-negotiables (from AGENTS.md)

1. N64 build untouched: `Makefile`, `tools/`, `rsp/`, `ld/`.
2. Game logic unmodified. Only narrow `#ifdef PORT` ABI/layout exceptions,
   each documented as its own finding in §F. **The intended solution for this
   task requires ZERO game-code edits** — pure offline data conversion +
   port-layer table patch. If you believe a game-file edit is needed, stop and
   document why as a finding before making it.
3. Offline sidecar conversion preferred over runtime fixup (AGENTS.md; the
   D43/Plan-B precedent in `tools_pc/d43_emit.py` + `port/src/pcmodels.c`).
4. The PD port (`PD_PORT_CHECKOUT`) is an *approach*
   reference only — its BG format differs (zipped multi-section). Validate
   every field against GE's own consumers (`bg.c`, `stan.c`, N64 disasm).

## Current state (verified)

- Build GREEN: `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final` (~5 s).
- Game boots → full intro (Rareware logo, gun barrel w/ Brosnan, cast; ~frame
  2100) → crashes in `load_bg_file`.
- Crash: EXCEPTION 0xc0000005; symbolicated PC = **`load_bg_file`,
  src/game/bg.c:865**; the crash stack contains BG header words
  `0x0F000334` / `0x0F00032C` — confirms the BE-offset misread below.

## The bug, precisely

- BG-file internal offsets are N64 **big-endian u32s in segment-0x0F form**
  (`0x0Fxxxxxx`).
- `BG_SEG_TO_PTR` (bg.h:23):
  `((void*)(((u32)(base)) + (((u32)(off)) + 0xF1000000)))` — on N64,
  `off = 0x0Fxxxxxx` and `+ 0xF1000000` wraps in 32-bit arithmetic to the
  plain file offset `0x00xxxxxx`.
- On PC, `((s32 *)ptr_bg_data)[1]` reads LE: ROM bytes `0F 00 00 14` become
  `0x1400000F`; the fold yields `base + 0x0500000F` ≈ 5 MB past the header
  buffer → fault dereferencing `[1].pPointTableBin`.
- Scope: every bg `.seg` header word and record (room file-position list,
  portals, etc.) **and** the whole `Tbg_*_stanZ` geometry file
  (`StanPrefixRecord` + everything `stanDetermineEOF`/`stanLoadFile` walk).
  Same class as D68 (image table) but a far larger format surface.

## Known data points (BUNKER1)

- Per-level filenames: `levelinfotable` (bg.c ~line 183) →
  `bg_seg_filename` / `bg_stan_filename`. Name→symbol map:
  `assets/obseg/file_resource_table.inc.c` (e.g. line 6
  `"bg/bg_sev_all_p.seg"` → `&bg_sev_all_p_seg`; line 586
  `"Tbg_sev_all_p_stanZ"` → `&Tbg_sev_all_p_stanZ`).
- BUNKER1 files: `bg/bg_sev_all_p.seg` @ cart **0x10438660**, size **0x10DF0**;
  `Tbg_sev_all_p_stanZ` @ cart **0x1087C3F0**, size **0x3DD0**.
- Header words (BE values): word0 = `0x00000000`; word1 = `0x0F000014`
  (room file-position list @ file offset 0x14); word2 = `0x0F000334`;
  word3 = `0x0F00032C`.
- `bg_room_data` records: `pPointTableBin` at record+0x28 (per crash disasm),
  more `0x0Fxxxxxx` offsets per record.
- Load path: `obLoadBGFileBytesAtOffset(name, buf, offset, size)` does partial
  loads via memcpy from `&fileentry->hw_address[offset]`; stan files load via
  `_fileNameLoadToBank(..., bank 2)`. Both already work on PC — the bug is
  **interpretation only**, so redirecting `hw_address` at converted bytes is
  sufficient.

## Task 1 — decode the formats (write notes as you go)

1. Dump BUNKER1's `.seg` header + first records from the ROM; map every word
   to its consumer in `bg.c` (`load_bg_file` ~line 795+ and the table walks it
   performs: rooms/portals/bgcmds/lights). Note which words are
   `0x0Fxxxxxx` offsets vs plain data.
2. Same for `Tbg_sev_all_p_stanZ`: `StanPrefixRecord` + whatever
   `stanDetermineEOF`/`stanLoadFile` walk (`src/game/stan.c`). Identify vertex
   batches, bounds (cf. `RoomVtxBatchBounds` in bg.h), Gfx/DL references, and
   texture refs inside stan data.
3. **Classify every field:** u32 numeric (bswap32 for PC) / s16 numeric
   (bswap16) / byte stream (leave alone — compressed payloads etc.). Where the
   decomp is ambiguous, cross-check struct field offsets against the N64
   disasm of the consumer function.
4. Write the format notes into `PCPortResearch.md` §F as you go (labels
   D78+) — they are the converter spec and must be complete enough to
   regenerate the converter from them.

## Task 2 — implement offline conversion (Plan-B pattern)

**Key design constraint: in-place, size-preserving conversion.** If every
converted file is byte-identical in *size* to its ROM original, all internal
`0x0Fxxxxxx` offsets remain valid after `BG_SEG_TO_PTR` folding, and the game
code needs **zero changes** — bswapped words read back as the original BE
values. (bswap32/bswap16 are size-preserving by construction; byte streams
pass through untouched.)

1. New converter `tools_pc/d69_emit.py` (model on `d43_emit.py`): per region,
   for every `bg/*.seg` + `Tbg_*_stanZ` referenced by `levelinfotable`:
   extract from ROM → convert per the Task 1 spec → concatenate into
   `data/pccg-<region>/pccg.bin` + `manifest.csv` (`name,offset,size`,
   decimal — same format as pcmodels).
2. Port layer: clone the `port/src/pcmodels.c` pattern (it is the template —
   read it first): sidecar image lives in the cart reservation extension at
   `[CART_BASE + romSize, ...)`; a one-shot table patch matches
   `file_resource_table[]` by filename and rewrites `hw_address` +
   `rom_size`. Either extend `pcmodels.c` or add `port/src/pccg.c`. Wiring
   points (all small):
   - `port/src/romdata.c` ~line 165: include the pccg size in the
     `VirtualAlloc(CART_BASE, romSize + sideTotal)` reservation; load the
     sidecar next to `pcmodelsLoadSidecars`.
   - `port/src/romdata.c` ~line 253 (`romdataCartAddrValid`) and
     `port/src/libultra.c` ~line 821 (DMA source validation, D60 safety net):
     extend the bounds check to cover pccg bytes.
   - One-shot patch call site: same pattern as
     `load_object_fill_header → pcmodelsPatchTable()` (or hook the BG load
     path's first use — your choice, document it).
3. Regenerate per region (`ntsc-final` / `pal-final` / `jpn-final`); add
   `data/pccg-*/` to `.gitignore` like `pcmodels-*`.

**Fallback:** if the decode shows only a handful of fields matter for
first-frame render, a runtime port-layer fixup hook (D68-style bswap after
load) is acceptable as an *interim* step — but stage geometry is deep; expect
most fields to need conversion anyway. Document the choice as a finding.

## Task 3 — verify

1. **Clean run from the repo root** (`cd <repo> && ./build-pc/ge007.x86_64.exe`)
   — running from `build-pc/` picks up the wrong ROM and misses sidecars
   (spurious early crash). Expect: intro → BUNKER1 loads without fault; stage
   geometry visible (plausible pixels, not pixel-perfect).
2. `GE_PCDUMP="2100-2400:5"` frames + `tools_pc/pixcount.py` — confirm
   non-black stage content past the old crash point.
3. **All levels:** converter must cover every bg/stan file in
   `levelinfotable` (20+); the table patch logs its hit count — verify it
   matches the manifest row count exactly.
4. Verification ritual (AGENTS.md): build green; no new undefined/duplicate
   symbols (`/linkcheck`); every touched file parses.
5. Strip TEMP probes before committing (keep the `GE_D69` probe in `ob.c`
   until resolved, then strip it per the HANDOFF Task 3 list).

## Acceptance criteria (all must hold)

- [ ] Clean run reaches BUNKER1 without fault (no new `ge007.crash.log` past ~frame 2100).
- [ ] Stage renders plausible geometry (PPM-verified, non-black content).
- [ ] Converter covers all levels' bg/stan files; all three regions regenerable from the script.
- [ ] Zero game-code edits — or any `#ifdef PORT` edit individually justified and documented as a D7x finding.
- [ ] Format spec written in `PCPortResearch.md` §F (D78+).
- [ ] Build green + `/linkcheck` clean; TEMP probes stripped.

## Environment gotchas

- MSYS2 tools: `export PATH="/c/msys64/mingw64/bin:$PATH"`. Build ~5 s.
- Run the game from the **repo root**, not `build-pc/`.
- Crash log `ge007.crash.log` (repo root) is the first stop for any fault;
  symbolicate offline: `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>`
  (image base 0x140000000).
- gdb is launch-mode only and far too slow for timing-dependent issues —
  prefer env-gated probes + the crash log.
- Heavy logging perturbs tick-driven timing (`osGetCount` advances at
  ~46.5525 ticks/µs) — diagnose stability with clean runs only; use short,
  targeted probe windows.
- `Gfx` is 16 bytes on PC; ROM-copied N64 DLs stay 8 bytes in memory — walk
  them at 8-byte stride when scanning.
- N64 CPU/RDP data in ROM is big-endian; any raw `*(u32*)` read of a
  ROM-copied buffer needs bswap under PORT (D68 pattern) or offline conversion.
- **Don't "fix" the s32 address idioms.** The decomp passes addresses through
  `s32`/`u32` (e.g. `ptr_bg_data = (s32)header`); this works because all
  emulated addresses (cart 0x10000000, DRAM 0x70/0x80xxxxxx) and host
  stack/heap fit in the low 4 GB, so truncation is lossless. Changing these to
  64-bit types breaks the `BG_SEG_TO_PTR` fold and is out of scope.
- Standalone probe compiles need `-std=c11`.

## Out of scope (do not let these displace the milestone)

- D75 (3D matrix/model bugs), D76 (copyright-screen 2D partial draw),
  D77 (no audio). The stage may render *wrong* — that is expected; the bar is
  loads-without-fault + plausible pixels.
- Pixel-perfect intro-logo check vs N64 footage.

## Documentation requirements

- New findings: `PCPortResearch.md` §F, labels **D78+** (D75–D77 are taken).
- Update §G status and `docs/HANDOFF.md` at each sub-milestone.
- Commit per sub-milestone: (1) format spec notes, (2) converter + sidecar,
  (3) port wiring, (4) verification + probe strip.
