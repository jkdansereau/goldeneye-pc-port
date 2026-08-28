# Handoff brief — GoldenEye 007 PC port (Phase 2: Session M-2 —
# whole stage-load → first-frame crash chain CLEAR; next is D85 rendering)

_Paste-ready brief. Authoritative context: `AGENTS.md`,
`docs/PCPortResearch.md` §F (D69, D78-D92), `docs/BRIEF-D69-stage-load.md`._

## READ THIS FIRST — the crash chain is clear; the blocker is now rendering

D88.1–D88.6 + D89 + D90 + D91 + D92 are **committed**. A direct
`-level_09` boot now loads BUNKER1's full stage setup, runs path-table /
intro-section setup, spawns the player and chrs, ticks AI, and reaches
the **first render** — where it hits **D85**: `sysFatalError("Bad size
for RGBA texture in tile 0: 00")` (`gfx_pc.cpp:967`), i.e. the room GDL
binary decodes to a garbage GBI stream. This is 3D-pipeline / render-
milestone work, not a crash-chain fix. Full analysis: `PCPortResearch.md`
§F **D85 revisited**.

- Build: `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`
- Sidecar regen (REQUIRED after this session — D88.5/D88.6 changed the
  converters): `python tools_pc/d69_emit.py ntsc-final && python
  tools_pc/d88_emit.py ntsc-final --regen`. `data/` is gitignored.
- Repro (direct BUNKER1): `./build-pc/ge007.x86_64.exe -level_09 -ml0 -me0
  -mgfx100 -mvtx50 -mt700 -ma150` — the `-m*` args are the per-level
  memory sizes from `boss.c`'s `memallocstringtable`; a bare `-level_09`
  boots but crashes early because the `-level_` branch skips the default
  `-m*` string (TODO: auto-inject in the port). Attract mode (no args)
  reaches the same D90 crash.

## What this session (M-2) fixed — all committed

1. **`-level_09` now works.** `osPiReadIo` was stubbed to 0, so the token
   string was always empty and N64 debug switches were ignored (only
   attract mode could reach a level). Now built from `argv[1..]`.
2. **Sidecar tables patched in `obInit()`** instead of lazily at first
   model load — a direct stage boot loaded raw big-endian ROM before.
3. **D88.5** — stan tile-name byte-swap in the converter (0/276 →
   273/273 name matches during pad setup).
4. **D88.6** — intro CAMERA `lang1c` is a `u16` pair, not an `s32`
   (fixed the `langGet` NULL-bank crash).
5. **D89** — `init_path_table_links` `[-3]` OOB → SIGILL fix; NULL-tile
   guard in `sub_GAME_7F0B0914`.
6. **D90** — `stanTileDistanceRelated`'s 80-byte zero-fill was clobbering
   the caller's live stan-tile local (16B struct, 80B clear). The
   player's spawn stan was fine all along.
7. **D91** — `(s32)&D_800442FC[portalnum]` truncation in bg portal cull.
8. **D92** — `chrAllocate` `s32` param truncated the ailist pointer;
   `Model.unka0` `s32` field truncated a stored function pointer.

**Next steps for the resuming session (render milestone):**
1. **D85** — the room GDL binary decodes to a garbage GBI command stream
   (`fmt=RGBA siz=0`). Decode what `texLoadFromGdl` does with room-
   specific opcodes / CC-RM-LUT markers; verify `csize_*_DL_binary`
   sizing. See §F D85 + "D85 revisited". Quick unblock option: soften the
   four `sysFatalError("Bad size…")` guards in `gfx_pc.cpp` to
   skip-with-warning so the frame renders with placeholder textures.
2. Then the front-end **render bugs (D75)**.

**D88.4 loose end (still open):** `PROPDEF_PC_BYTES` for `VEHICHLE`/
`AIRCRAFT`/`TANK`/`AMMO`/`DEPOSIT_IN_ROOM` in `d88_propdefs.py` are
placeholder guesses (BUNKER1 doesn't use them). Probe real sizes via
`d88_layoutprobe.c` before loading levels that use those types.

## Known rendering bugs (D75 — still open, orthogonal to the D88 crash)

Even once the crash chain is cleared, the front end has **broken 3D model
rendering** (user-confirmed this session):
- Rareware logo: correct (fixed in D73/D74).
- **Nintendo logo**: renders but **mispositioned**.
- **Gun-barrel intro**: the **James Bond character model is missing
  entirely**.
- **Intro credits / cast roll**: the per-character 3D models **do not
  appear at all** (names draw, models don't).
Pattern: textures/text draw; **animated/skeletal character models never
appear**; static 3D (logos) appears but with a bad transform. Leading
hypothesis is D75(b) — the animated-model path (`animInit` + raw offsets
into `struct player`, cf. D56) is broken independently of the D73 matrix
sin/cos fix. Full triage plan in `PCPortResearch.md` §F D75.

The rest of this document (below) is the **last known-good, committed**
status as of commit `8c9c6a2c` (D86+D87 resolved) — still accurate except
D88 is now further along (D88.1–3 done/verified, D88.4 is the live crash).

## Where things stand

**D69 (the original "stage load faults" milestone blocker) remains
RESOLVED.** `load_bg_file` (bg.c) doesn't fault on BUNKER1. Since then,
two more crashes further down the load chain were found and fixed this
session (D86, D87), and a third — the current blocker — was root-caused
but not fixed (D88).

**D86 RESOLVED.** `modelInitRwData` crash (`model.c:6174`) was a single
truncating pointer cast in the player's embedded gait/arm model:
`src/game/initplayergaitobject.c:5` did
`player_gait_object_header.RootNode = (int)&player_gait_hdr;` — a
same-width no-op on N64 that truncates+zero-extends a real 64-bit pointer
on PC. Fixed with a narrow `#ifdef PORT` branch assigning the pointer
directly. Root-caused via a new node-walk trace (`GE_D86=1`, left in
place, gated in `model.c`/`objecthandler_2.c`).

**D87 RESOLVED.** Once D86 stopped blocking progress, an idle (no-input)
run eventually triggers the front-end's genuine attract-mode demo
playback (`select_ramrom_to_play()` picks a random compiled-in demo —
this is shipped retail behavior, not a debug feature) and crashed in
`ramrom_replay_handler` (`ramromreplay.c`). Root cause: `ramromfilestructure`
is a real ROM-compiled asset (big-endian, like everything else) loaded via
`romCopyAligned()` — a raw byte copy by design (D66) — with **no
byteswap**, so every multi-byte field read back scrambled (e.g. `size_cmds`
2 → 33554432) and drove wild pointer arithmetic. Fixed with a `#ifdef PORT`
`ramromFixupEndian()` called once after the load (same pattern as the D54
cseq fixup). Not BUNKER1-specific — attract mode picks any of 7 demo
locations at random, so don't rely on it for BUNKER1-specific testing (see
`-level_09` below).

**D88 — SUPERSEDED, see "READ THIS FIRST" at top.** D88.1–D88.3 (header +
sub-table width/endian conversion) are now done and verified; D88.4
(`propDefs` byteswap) is the live blocker. The paragraph below is the
original root-cause writeup, kept for context.

**D88 (original writeup) — root-caused.** Launch with
`-level_09` (NTSC `LEVELID_BUNKER1 = 9`; `boss.c:199-339` decodes
`-level_XX` into `g_StageNum`, bypassing the front end/attract-mode
entirely — fast, deterministic BUNKER1 repro, crashes in well under a
minute instead of waiting ~2 min for attract mode to maybe pick Bunker).
Crash: `proplvreset2` (`prop.c:1306`) segfaults reading
`g_CurrentSetup.pathwaypoints[i1].padID`. Root cause: the per-level
`"Usetup<name>Z"` file (`prop.c:1267`, `struct stagesetup` in
`bondtypes.h:4091`) is loaded as raw ROM bytes and has **zero PC porting
work done on it** — unlike bg/stan (D69/D80-82) and models (D43/D50).
Two compounding problems, not just one:
1. The 10 top-level fields (`pathwaypoints`/`waypointgroups`/`intro`/
   `propDefs`/`patrolpaths`/`ailists`/`pads`/`boundpads`/`padnames`/
   `boundpadnames`) are declared as real pointers in the live C struct,
   so on PC they're 8 bytes each (an 80-byte header) instead of the
   file's real 4-byte-each (40-byte) N64 layout — same class as D79
   (`bg_room_data` pointer growth). Field 0 reads fine; everything after
   it is reading the wrong bytes entirely.
2. The 4 meaningful bytes each field *does* store are big-endian (the
   code's own comment: "stores every internal reference as a byte offset
   from the start of the file") and nothing byte-swaps them — same class
   as D87.
No PORT/byteswap handling exists anywhere in `prop.c` (confirmed by grep).
**Not fixed this session** — this is D69-scale format-conversion work: a
byte-accurate spec of the whole `Usetup*Z` format (top-level header +
every nested sub-table: `waypoint`/`waygroup`/`PropDefHeaderRecord`/
`PathRecord`/`AIListRecord`/`PadRecord`/`BoundPadRecord`/`pname`, each
likely with its own internal offsets not yet audited) plus either an
offline converter sidecar (preferred pattern per AGENTS.md, same shape as
`tools_pc/d69_emit.py`) or a careful runtime fixup pass that parses the
raw 40-byte N64-packed header by explicit byte offset, byte-swaps each
field, and writes results into the PC-widened struct.

**Net effect vs. last session:** the game now runs substantially further
— all the way through room-streaming setup, past the intro's model
pipeline, and into per-level "Usetup" data — before hitting D88. The
"loads without fault" acceptance bar is **still not met**, but the
remaining blocker is now narrowly scoped and has a fast, deterministic
repro (`-level_09`, no attract-mode wait, no depending on which random
demo attract-mode picks).

## Recommended next steps, in order

1. **D88.** Get a byte-level spec of `Usetup*Z` (start from BUNKER1's
   file; `strResource` is built as `"U" + "sev" + "Z"`-style name in
   `prop.c:1253-1265` — check `setup_text_pointers[LEVELID_BUNKER1]` for
   the exact literal). Decide offline-converter vs. runtime-fixup (D69's
   bg/stan work is the template for the former; D54's cseq fixup is the
   template for the latter — given the struct-width mismatch on top of
   the byteswap, a runtime fixup that manually walks the *raw* 40-byte
   N64 header by hand (not through the live `stagesetup` struct) into a
   freshly-populated `g_CurrentSetup` is probably simpler than a full
   sidecar here, but verify against a raw ROM hex dump either way).
2. Once BUNKER1 loads past `proplvreset2`, re-check for further crashes
   in the same vein (this session found 3 in a row — D86, D87, D88 — each
   only reachable after the previous one was fixed; expect more).
3. Once BUNKER1 reaches a rendered frame with no fault, revisit **D85**
   (room primary/secondary GDL binaries decode to garbage via
   `texLoadFromGdl`) — use `GE_PCDUMP="<range>:10"` + `tools_pc/pixcount.py`
   to confirm non-black, non-degenerate content per the original D69
   acceptance bar. (This session's `GE_PCDUMP` captures around frame
   2100-2400 during attract-mode-driven "loading" were still on a HUD/menu
   screen, not real 3D geometry — don't read too much into pixel counts
   from before D88 is fixed.)
4. Do NOT touch D75/D76/D77 (parked, lower priority, unrelated).

## Debug tooling added this session (kept, env-gated, zero cost when unset)

- `GE_D86=1` — node-walk trace in `modelInitRwData` (model.c) + a
  load-identity probe in `load_object_fill_header` (objecthandler_2.c).
  Resolved the D86 crash; left in place since the same
  load_object_fill_header/modelInitRwData pipeline could surface new
  edge cases as more of the game becomes reachable.
- `GE_D87=1` — block-setup trace in `iterate_ramrom_entries_handle_camera_out`
  and consumer trace in `ramrom_replay_handler` (ramromreplay.c). Resolved
  the D87 crash; left in place as it's a rarely-exercised path (attract
  mode) worth having visibility into if it acts up again.
- Pre-existing `GE_D69STAN=1`, `GE_D69BB=1`, `GE_D69=1` — unchanged, still
  useful for the bg/stan/D85 load path (see prior session's HANDOFF
  entries, preserved in git history, for exactly what each logs).

## New: fast, deterministic BUNKER1 repro (no attract-mode wait)

Launch with `-level_09` as a program argument
(`./build-pc/ge007.x86_64.exe -level_09`) to skip the front end/attract
mode and load BUNKER1 directly — `boss.c:199-339` decodes `-level_XX`
(the two digit-chars are consumed as raw ASCII bytes:
`g_StageNum = tokenFindLevel[0]*10 + tokenFindLevel[1] - 0x210`; NTSC
`LEVELID_BUNKER1 = 9` → `"09"` since `'0'*10 + '9' - 0x210 = 9`). This is
now the preferred way to test BUNKER1-specific load/render work — it's
faster (crashes/completes in well under a minute vs. ~2+ min waiting on
attract mode) and deterministic (not dependent on which of 7 random demo
locations attract mode happens to pick).

## Environment / build

- `export PATH="/c/msys64/mingw64/bin:$PATH" && ./build-pc.sh ntsc-final`
  (~5 s). Build is GREEN with all D86/D87 work included.
- Run from the **repo root**, not `build-pc/`.
- Regenerate sidecars if missing: `python tools_pc/d69_emit.py ntsc-final`
  (bg/stan) and the D43/D50 model sidecar generator (pcmodels) — both
  gitignored, not checked in. (Both were already present in this dev
  environment this session.)
- `GE_PCDUMP="<start>-<end>:<stride>"` + `tools_pc/pixcount.py` for frame
  captures once D88 is fixed and a frame actually renders real BUNKER1
  geometry.
- Crash log: `ge007.crash.log` (repo root); symbolicate with
  `addr2line -e build-pc/ge007.x86_64.exe -f -C <pc>` (image base
  `0x140000000`).
- gdb **launch** mode is too slow for timing-dependent bugs (unchanged
  guidance). **New this session:** gdb **attach** mode
  (`gdb -batch -x cmds.txt -p <winpid>`, `<winpid>` = 4th column of
  `ps -p <bashpid>`) works well and is fast for "watch a global for a
  legitimate vs. corrupted write" questions on an already-running,
  not-yet-crashed process — see `docs/PCPortResearch.md` §F environment
  reminders for the exact recipe used to root-cause D87.

## Non-negotiables (unchanged, see AGENTS.md)

1. N64 build files untouched.
2. Game logic unmodified except narrow, documented `#ifdef PORT`
   ABI/layout exceptions (D86/D87 this session, both logged in
   `PCPortResearch.md` §F). D88 is explicitly **not** patched with a
   quick inline hack — it needs the same disciplined
   spec-then-convert/fixup treatment as D69, logged as an open finding
   instead per AGENTS.md's "stop and write it up" guidance for anything
   beyond a narrow, obviously-correct exception.
3. Offline sidecar conversion preferred over runtime fixup for whole
   ROM-asset formats (D69/D80-82 pattern) — likely the right call for
   D88 too, though a careful runtime fixup is also plausible; decide
   after the byte-level spec work.
