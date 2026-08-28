# Handoff brief — GoldenEye 007 PC port (Phase 2: Session J — D69 resolved, D85/D86 open)

_Paste-ready brief. Authoritative context: `AGENTS.md`,
`docs/PCPortResearch.md` §F (D69, D78-D86), `docs/BRIEF-D69-stage-load.md`._

## Where things stand

**D69 (the "stage load faults" milestone blocker) is RESOLVED and
verified.** `load_bg_file` (bg.c) no longer faults on BUNKER1 (or any of
the 20+ other levels the offline converter covers). This took five
findings, all committed:

- **D78** `StandTile.id`/`.room` bitfield packing (MIPS-BE vs x86-LE) —
  `#ifdef PORT` layout fix in `src/bondtypes.h`.
- **D79** `bg_room_data`'s 3 offset fields declared `void *` (never
  dereferenced, but grows the record on x86-64 and breaks array indexing)
  — `#ifdef PORT` `u32` fix in `src/game/bg.h`.
- **D80/D81/D82** the bg `.seg` + `Tbg_*_stanZ` format spec, the offline
  converter (`tools_pc/d69_emit.py`), and the port-layer sidecar wiring
  (`port/src/pccg.c`, `port/include/pccg.h`, wired into `romdata.c` /
  `libultra.c` / `objecthandler_2.c`, same Plan-B pattern as
  `pcmodels.c`/D50).
- **D83** `StandTileHeaderMid`/`StandTileHeaderTail` bitfield packing
  (same MIPS-BE/x86-LE class as D78, found because it caused an actual
  infinite loop in `stanBuildRoomData` once D78-D82 got that far) —
  `#ifdef PORT` reversed-field-order fix in `src/bondtypes.h`.
- **D84** three hand-inlined `BG_SEG_TO_PTR`-style segment folds in
  `bgLoadRoomVtxData`/`bgLoadRoomPrimaryGdl`/`bgLoadRoomSecondaryGdl` did
  the `+0xf1000000` fold as real 64-bit pointer arithmetic (doesn't wrap
  at 32 bits the way the macro's `(u32)` cast does) — `#ifdef PORT` fix,
  same fold as `BG_SEG_TO_PTR`, in `src/game/bg.c`.

**Regenerate the sidecar** with `python tools_pc/d69_emit.py ntsc-final`
(writes `data/pccg-ntsc-final/pccg.bin` + `manifest.csv`, gitignored like
`pcmodels-*`) before running — it is not checked into git. PAL/JPN are
implemented in the converter but **not regenerated or verified** (those
ROMs aren't present in this dev environment).

## What's still open (found *during* D69 verification, not the original bug)

**D85 (room DL binaries decode to garbage; safety-netted, not fixed).**
`bgLoadRoomPrimaryGdl`/`bgLoadRoomSecondaryGdl` decompress correctly (the
`11 72` RZ header and offset are right — D84 fixed that), but the content
`texLoadFromGdl` (the *same* runtime converter already used for model
GDLs) produces from the raw N64 bytes is not valid GBI opcodes. Not
root-caused. A `#ifdef PORT` bounds check was added in
`bgBuildRoomVtxBounds` so a garbage command stream can't segfault (it just
produces an empty/degenerate bounding box for that batch) — this is a
crash-safety net, not a geometry fix. **Next step:** figure out what
`texLoadFromGdl` actually does with room-specific GBI markers — room GDLs
reference `bgApplyDynamicCCRMLUT`/`ptrDynamic_CC_RM_LUT`/
`DL_LUT_PRIMARY_ADDFOG`, suggesting a CC/RM-LUT-selection marker convention
models don't use — and verify `csize_primary_DL_binary`/
`csize_secondary_DL_binary` sizing end-to-end (these come from subtracting
adjacent `bg_room_data` offsets; D79 argued the +delta cancels in the
subtraction, but that argument hasn't been independently re-verified with
a probe).

**D86 (new crash, likely unrelated to bg/stan).** With D85's safety net
in, the game reaches a **new** segfault in `modelInitRwData` (model.c:6174,
called from `modelInit`/`animInit`), dereferencing a bad `ModelNode`
pointer. This is the existing D50-D58 model pipeline (stable for every
intro/cast model) hitting a case that's never been exercised before —
most likely a room-instantiated object (light fixture / prop / character)
whose load path differs, or a pcmodels-manifest edge case. **Not
root-caused.** Next step: identify which model name is being loaded at
the crash site (add a probe in `load_object_fill_header` or
`modelInit`/`animInit` printing the header/model name) and check whether
it's in `data/pcmodels-ntsc-final/manifest.csv`.

**Net effect:** the game now runs substantially further (through BUNKER1's
full room-streaming setup) before crashing, vs. the original instant fault
in `load_bg_file`. The "loads without fault" acceptance bar is **not yet
fully met** — D86 is the current hard blocker to reaching a rendered
frame. Recommended order: D86 first (probably a small, isolated fix, and
unblocks seeing whether D85's degenerate room geometry is otherwise
"plausible pixels" per the D69 bar), then D85, and only then worry about
D75 (pre-existing intro-3D findings, still open, still lower priority).

## Debug tooling added this session (kept, env-gated, zero cost when unset)

- `GE_D69STAN=1` — `stanBuildRoomData` (stan.c): logs the tile walk
  (address/room/mid/tail/pointCount) for the first 20 tiles and any tile
  whose derived tile-size would be 0 (the D83 symptom).
- `GE_D69BB=1` — `bgBuildRoomVtxBounds`/`bgLoadRoomPrimaryGdl` (bg.c): logs
  the room's expanded-GDL pointer, vertex-buffer pointer, first 8 decoded
  Gfx slots, and the compressed/decompressed primary-DL bytes around the
  fold (D84/D85 diagnostic).
- Pre-existing `GE_D69=1` (ob.c, `obLoadBGFileBytesAtOffset`) — **left in
  place** (HANDOFF previously said to strip it once D69 is solved; D69's
  *original* bug is solved, but D85/D86 are still open follow-ups in the
  same overall load path, so it stays useful for now).

## Environment / build

- `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`
  (~5 s). Build is GREEN with all D69 work included.
- Run from the **repo root**, not `build-pc/`.
- `GE_PCDUMP="2100-2700:10"` + `tools_pc/pixcount.py` for frame captures
  once D86 is fixed and a frame actually renders past stage load.
- Crash log: `ge007.crash.log` (repo root); symbolicate with
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>` (image base
  `0x140000000`).
- gdb is launch-mode only and far too slow for timing-dependent bugs;
  prefer env-gated probes + the crash log (see the D85/D86 probes above
  for the pattern: `#if defined(PORT)` / `getenv("GE_...")`).

## Non-negotiables (unchanged, see AGENTS.md)

1. N64 build files untouched.
2. Game logic unmodified except narrow, documented `#ifdef PORT`
   ABI/layout exceptions (D78/D79/D83/D84 this session, all logged in
   `PCPortResearch.md` §F).
3. Offline sidecar conversion preferred over runtime fixup — D80-D82
   follow the D43/pcmodels pattern exactly.
