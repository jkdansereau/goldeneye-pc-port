# Handoff brief — GoldenEye 007 PC port (Phase 2: Session L — D88.1-3
# VERIFIED (uncommitted), D88.4 is the new blocker)

_Paste-ready brief. Authoritative context: `AGENTS.md`,
`docs/PCPortResearch.md` §F (D69, D78-D88), `docs/BRIEF-D69-stage-load.md`._

## READ THIS FIRST — verified but uncommitted work in the tree

The D88 `Usetup*Z` converter work is now **format-verified and passes the
`-level_09` repro up to a new, further-along crash (D88.4)** — but it is
**still uncommitted** (carried through two interrupted sessions). This
session (2026-08-28) built it, ran it, and confirmed the fix works; it did
not commit (left to the resuming session so it can bundle D88.4).

`git status` shows:
- Modified: `port/src/pccg.c`, `src/bondtypes.h`, `src/game/bondview2.c`,
  `src/game/bondview_r.c`, `src/game/prop.c`, `src/game/stan.c`
- New/untracked: `tools_pc/d88_emit.py`

**What is verified working (see `PCPortResearch.md` §F D88.1–D88.3):**
- Tree **compiles green**: `export PATH="/c/msys64/mingw64/bin:$PATH" &&
  ./build-pc.sh ntsc-final`.
- `tools_pc/d88_emit.py` (531 lines) is a complete converter; its output
  is already in `data/pccg-ntsc-final/manifest.csv` (21 `Usetup*Z` rows).
  `port/src/pccg.c` `PCCG_MAX_FILES` 128→256 lets the sidecar carry them.
- `-level_09` + `GE_D88=1` confirms `proplvreset2` now walks the **entire
  pads table correctly** — real plink strings (`p1988e`, `p12295e`, …) and
  sane BUNKER1 world coords. The old `prop.c:1306` crash is **gone**.
- `SetupIntroCamera` narrow-`u32` `#ifdef PORT` fields: write-before-read
  claim **verified** against `bondview_r.c:276-300` (all three fields
  written — `prev` link + both `langGet()` results — before any read).

**THE NEW BLOCKER — D88.4:** `-level_09` now crashes in `setupDoor`
(`prop.c:971`) → `modelLoad` (`loadobjectmodel.c:335`), garbage `modelid`
into `PitemZ_entries[]`. Root cause: `d88_emit.py` deliberately leaves the
`propDefs` polymorphic record stream as an un-byteswapped passthrough (its
docstring defers the per-type bswap + PC/N64 `sizeof` audit of ~40
prop-def record types). `door->obj` is read big-endian → garbage. Same
class as D87/D88.1. Full detail + fix plan in `PCPortResearch.md` §F
D88.4. Also see D88.5 (stan tile lookups all miss during pad setup —
watch, may be a related residual bug).

**Next steps for the resuming session:**
1. Read `PCPortResearch.md` §F D88.1–D88.5 (fully rewritten this session).
2. Fix **D88.4**: spec each `propDefs` record type, add per-type byteswap
   (+ `intro` polymorphic records) to `d88_emit.py`, audit struct sizes.
   Regenerate the sidecar (delete the Usetup rows from `manifest.csv` +
   their bytes first, or rebuild from `d69_emit.py`, then re-run
   `python tools_pc/d88_emit.py ntsc-final`).
3. Re-run `-level_09`; expect 1–2 more crashes down the prop-setup chain.
4. Once BUNKER1 loads without fault, **commit** the whole D88 bundle in
   sub-milestones (format spec → converter → port wiring → probes),
   §F D-labeling.
5. Then the render bugs (D75, below) and D85 become the path to a playable
   frame — see the expanded D75 note below and in §F.

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
