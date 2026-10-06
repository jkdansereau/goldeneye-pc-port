# Roadmap and open items

**This is the single tracker for high-level work on the port.** If an item
is not here, it is either done or not planned. The README's *Known issues*
and *Roadmap* sections are short summaries of this file. Root-cause detail
for each `Dxx` lives in [`dev/findings.md`](dev/findings.md) (find it via
[`dev/findings-index.csv`](dev/findings-index.csv); that index is generated,
first-line-only, and can lag the entry tail, so read the entry).

Last full triage: 2026-09-29. Sources: the findings log, all GitHub issues,
PRs and releases, every local backlog/plan/roadmap note, `HANDOFF.md`, all
local and remote branches, and known community forks.

## How to use this file

- **One row per item.** Link the `Dxx` and/or GitHub `#issue`. Keep detail
  in the finding, not here.
- **Status vocabulary:** `open` · `partial` · `verify` (fixed, a live check
  is owed) · `parked` (work exists on a branch or patch, not merged) ·
  `decision` (blocked on a maintainer call).
- **When an item closes, delete its row.** The history is kept in the
  finding and in git. Do not strike rows through. Struck rows are what made
  the old backlogs unreadable.
- **New work goes here first.** Do not start a new backlog, roadmap or
  plan-of-record file. A design doc for one item is fine; link it from the
  row and delete it when the item lands.
- Release scoping (which items go into which version) happens in the
  release notes, not here.
- **Known issues** (the player-facing subset) live in the table below. The
  README links to it and does not duplicate it.

## Goal and definition of done

**Feature complete and faithful to the N64 original.** Every mode and
region the cartridge ships should work and behave 1:1 with the N64,
checked against an accurate emulator (1964/GEPD) or real hardware. On top
of that sits a **modern-remaster layer** (the bar set by Nightdive's
Quake/Turok remasters, the XBLA remaster and GE+/Dab's mod). It must never
change game logic by default, and it must always be possible to get back
to the original look and feel (see the *Original N64 preset*, D440).

The port is **feature complete** when all of these hold:

1. Every solo mission (all 20 plus the credits) is completable at every
   difficulty, on Windows and Linux.
2. No crash on any level under normal play, on either platform.
3. Audio is complete and faithful: music, positional SFX, menu and weapon
   sounds.
4. Cutscenes play correctly (camera, actor positions, timing).
5. AI locomotion and pacing match the N64.
6. Input feels like GoldenEye on a controller, and like a modern FPS on
   mouse and keyboard.
7. Rendering has no visible defect versus 1964/GEPD at default settings,
   apart from the port's deliberate modern defaults.
8. **Multiplayer (2–4 player split-screen) works.** It is the largest item
   left (§1).
9. **PAL and JP ROMs work** (§1, pending a decision).

**Exit gate:** a full two-platform campaign playthrough at more than one
difficulty, plus a split-screen session, on a build with the §3
verification debt cleared.

Where things stand (2026-10-01): 1–7 hold for NTSC-U, with the small
accuracy gaps listed in §2. 8 is merged on `release/v0.4.1` (2P verified on
every MP map; 3P/4P with real pads owed). 9 is open (Stage B).

**Parity rule (decided 2026-09-30):** every platform we release for
carries the same sign-off gate: a full 21-level sweep plus a campaign
pass. Windows and Linux now; macOS when it joins.

## Order of work (decided 2026-09-30)

Work continues on `release/v0.4.1`. When to cut it, and what to call it
(v0.5.0 or later), is decided as the work lands. There are two stages:

**Stage A: now → the pre-1.0 build**
1. Clear the §3 verification debt.
2. Split-screen (§1): keep working and testing until it is good.
3. `All unlocked` rework (§5a).
4. D357 settings values (§5a), plus the restart button.
5. Outside PRs (§5a), in the proposed order once the maintainer approves it.
6. Pillarbox with the exact original aspect for the Original N64 preset (§5a).
7. Platform parity: re-base the golden baselines and extend them to both
   platforms (§7), then run the gate on each platform.
8. Investigate and triage every §2 fidelity gap to fix or accept. Do not
   leave any of them as "open, unexamined".

**Stage B: pre-1.0 build → 1.0**
1. PAL/JP (§1). It goes last on purpose, so that region-specific
   regressions are easy to isolate and unwind.
2. macOS/ARM: merge the `macos` branch (§4), with the D441 census
   (store-to-u32 tagging) and a high-arena test.
3. The exit gate on every shipped platform.
4. Remove the temporary scaffolding (§7).

**Next (decided 2026-10-03, after the Birdman64 comparison;** findings and
lessons in the maintainer's local note `BIRDMAN64-LESSONS.md`**):**
1. D441: finish the census as one complete, lead-verified list, compiler
   pass in both directions plus the compiler-invisible classes (§2).
   **Done 2026-10-03.**
2. Fix the listed sites as mechanical, build-checked batches on a cheap
   worker in its own worktree; the lead reviews every diff (§7).
   **Batches 1-5 done 2026-10-03** (35 sites, driven by the tier-1
   high-arena build's crash order). Next: the tier-1 sweep over every
   level with a seeded frame compare, then further batches from it.
3. Scripted-playthrough harness: a feasibility spike first. Aiming,
   shooting, guards and timed events may block open-loop scripts (§7).

## Known issues

What a player will notice in the current release. **This table is the
single list:** the README links here instead of keeping its own copy. When
an issue is fixed and released, delete its row. When a new player-visible
issue is confirmed, add a row here (and its tracking row in the sections
below).

| Issue | Impact | Workaround | Tracking |
|---|---|---|---|
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source (D258, D487, D488; see `docs/building.md`); packaging (per-region exe, CI matrix, converter region check) is still open | D258, #85, §1 |
| No macOS or ARM builds | Platform | — | #88, #95, #101, §4 |
| Saves from builds before v0.4.1 can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.4.1 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it | D442, D387 |
| `Skip intro` and `All unlocked` are experimental | Opt-in options | Leave them off for a normal playthrough | D408, D442 |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed | D475, D480 |
| Far objects almost fully hidden in fog can show as a faint silhouette with straight vertical edges (e.g. Surface's satellite dish from the start area). The N64's 16-bit colour hid the 2-5 level difference; the port's 32-bit output shows it | Cosmetic, any settings | None; raising Draw/LOD distance makes more such objects visible | D503, §5c |

---

## 1. Feature completeness vs the N64 cartridge

| Item | Status | Refs | Notes |
|---|---|---|---|
| **2–4 player split-screen multiplayer** | verify | #99, D416–D423, D496 | Merged 2026-10-01. D417 Rule-2 approved. **Maintainer playtest 2026-10-03:** 2P and 3P with real Xbox and PS5 pads, handicaps, teams and game modes all good; the short 3P/4P pickup text is the game's own (D496 fixed an uninitialised buffer behind the weapon text). Earlier: every MP map in 2P, 4P Temple. **Still owed:** 4P with real pads, pads-only mode, MP audio, D422 centred aim live on P2+ (`AimMode = 1`), D428 sway on P2+. |
| **PAL and JP ROM support** | partial | D258, D487, #85 | **2026-10-02: both regions convert, build and boot** (D258 fixed and byte-verified against real PAL and JP ROMs; region-aware emitters and ROM detection; PAL link fix). Both play Dam with no crash; NTSC-U unchanged (`verify.sh dam` PASS). Build: `BUILD_DIR=build-pal ./build-pc.sh pal-final` / `build-jpn ... jpn-final`, with `data/ge007.<region>.z64` and the emitters run for that region. **D487 fixed 2026-10-02** (PAL frame height decoded with the NTSC rule cut the bottom ~12% of every PAL frame). **D488 fixed 2026-10-02** (PAL build now reports the PAL TV type, not Brazil's MPAL). **Open:** full runtime passes per region (all levels, menus, audio, saves; PAL 50 Hz feel incl. the D427/D451/D486 2-ticks/frame gates; EU-only text paths); packaging (per-region exe names, CI matrix, drop-in converter region gate, #6). ROMs: `ge-port-reference/1964_GEPD_Edition/ROMs` (JP .n64 = v64, PAL `.z64`, both hash-verified). #85 needs reopening or a fresh tracker when this ships. |
| Drop-in ROM converter, remaining parts | decision | #6, [`dev/release-dropin-rom-plan.md`](dev/release-dropin-rom-plan.md) | NTSC-U drop-in shipped. Left: per-region output names and re-exec guard (with PAL/JP), native C emitters to replace the PyInstaller converter. |
| Cut content (TCRF weapons/items) | decision | [`dev/CUT-CONTENT-BACKLOG.md`](dev/CUT-CONTENT-BACKLOG.md) | Not part of the shipped game. Tier 0 already works via the debug/cheat menus. Tier 1 (config-toggle grants) is small and port-only. Tier 2/3 (placing props, re-enabling unused MP maps) need game data changes and are out of scope for this repo. |

## 2. Accuracy (behaviour differs from the N64)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Third-person Bond offset residual | partial; intro puppet explained (2026-10-02) | D173, D292 | Mostly fixed. On the level-start puppet the variance is tick/PRNG-dependent idle timing (D13 class, faithful, no sign-off requested); one 1-frame first-tick Y transient is untraced. If the residual is seen elsewhere (cutscene body model), the maintainer should note where. |
| Dam ending cutscene: Bond placement; railing transparency | open | D173, D292; maintainer 2026-10-02 | **Maintainer to re-check the end of Dam** (the last place a Bond offset was seen; the D173 level-start probe found no defect). Also seen in the Dam cutscene: an edge railing texture (a 2D element, maintainer believes) with odd transparency; possibly already fixed by the alpha pass. Capture a screenshot of each if still present. |
| Dam: first-tower guard walks a small circle (glass collision polygon) | done | D490 | **Fixed 2026-10-02, merged into `release/v0.4.1` 2026-10-03:** a stack overflow in `sub_GAME_7F03ECC0` (flat-bbox objects, i.e. glass) corrupted the collision polygon; `rem[8]` under `AVOID_UB`. Headless probe: the guard now walks his whole patrol loop. Maintainer by-eye check in Dam passed 2026-10-02. The porting-notes D24 audit ran 2026-10-03 and found D491 (below); no other overflow of this kind. |
| Struct reads across adjacent globals: portal bbox seeds put props in extra rooms; casing spin seeded from trigger state | done | D491 | **Fixed 2026-10-03, merged into `release/v0.4.1`:** `bg.c` / `gunfire.c` read three separate scalar globals as one coord3d; mingw reverses their order. `#ifdef PORT` reads the named globals. Builds clean, 900-frame `-level_09` smoke clean. Nothing by eye is expected. Delete this row once released. |
| Sniper rifle: zoom level and speed vs 1964 | verify | #136, D484, D485, D494 | Crouch: **D494** (2026-10-03) makes the PC crouch key crouch/stand freely with the sniper, as GEPD does (superseding D483's freeze); maintainer-verified. Zoom level (D484, 20 -> 7 degrees) and speed (D485, tick-scaled, Rule-2) fixed 2026-10-02. **Owed:** 1964 A/B of zoom level and speed; close #136 after it. |
| Crash on save slots 1/3/4, plus a host BSOD (fltmgr.sys) | decided | #94 | Needs a repro. Two maintainer retest requests (v0.3.0, v0.4.0) got no reply. Likely D299/D301 (save-slot crash, fixed) and D344 (BSOD on exit, fixed). **Decided 2026-10-01: close as stale when the release ships**, pointing at D299/D301/D344; no GitHub activity before then. |
| Authored mip chains not sampled for explicit-LOD textures | post-1.0 | D323 | Distant textures use driver mips instead of Rare's hand-authored chain (heaviest on Statue/Aztec). Parked as a fidelity enhancement. **Decided 2026-10-01:** after 1.0, together with the LOD-fraction approximation as one "real RDP LOD" project. |
| Approximations in the software RSP | post-1.0 | `gfx_pc.cpp` `G_CCMUX_LOD_FRACTION`; D323 | The LOD-fraction CC input is an eyeballed approximation, bundled with D323 as one "real RDP LOD" project after 1.0 (2026-09-30 triage). **WrapFix settled 2026-10-02:** the maintainer checked the Depot building ceilings against 1964 with the default `Video.WrapFix = 0` and they look right, so the default stays off (the opt-in knob remains; RC3/D167). |
| Latent 64-bit ABI hazards | partial | D441, #107, #108; ~396 `-Wpointer-to-int-cast` sites at the start of the sweep (185 remain in the 17 touched files; many are intentional `(s32)&ANIM_DATA_*` offsets, D34); AUDIT-M6 `struct player`/`struct hand` raw offsets (porting-notes §A1) | **Next (2026-10-03): one complete census list** replacing parts 1/2 (part 3 was fabricated): re-run `scratch/d441p2/run.py` with `-Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion` (mingw is LLP64, so this already matches Birdman64's "long forced to 32 bits" pass), add the classes no compiler flags (raw `sizeof` copies of structs with pointer slots, union/raw-offset aliases, u32 addresses in display lists, bit-31/K0 tests, `&p[-u32]`, adjacent-global reads (D491), N64-stride allocations, s32 sentinels, endianness of serialized records), one row per site with provenance, store-to-u32 vs arithmetic-only, status and verifier. Part 1 landed (D441): 17 files widened to `uintptr_t` under `#ifdef PORT`, build and Bunker/Silo runs verified. **Still owed:** the full census with per-site provenance (H/R/I/?, table not yet written) plus a **store-to-u32 vs arithmetic-only** tag per site (a `uintptr_t` widening is enough only for arithmetic; sites that store back into a u32 field need `portHostToN64` under the macOS port_addr model), an env-gated high-arena test mode, and a check under an arena above 4 GB. Harmless today because the arena sits low. They would bite on macOS/ARM/ASLR. **#108 concern:** widening the `sndPlaySfx` bound disables the guard on every platform; prefer a range check. **#107 will now conflict** with D441 in `bg.c` (`bgRoomCalcBB`), so rebase or close it during the outside-PR review. A read-only provenance pass over the remaining sites is a good local-Qwen delegate task. **2026-10-01:** D457(c)(d) merged (player matrix pointers, Model/PROMOTE/vtxstore pointer width); #107's two sites fixed (D441); read-side census part 2 (403 `-Wint-conversion`/`-Wint-to-pointer-cast` warnings, triaged by file) in the local note `ABI-CENSUS-D441-PART2.md`; model.c rows need a re-run after D457(d). **Stage B plan (2026-10-01, maintainer's local design note):** (1) centralise the hard-coded arena address windows: **done 2026-10-02** (`port/include/portaddr.h`: `PORT_DRAM_V1_BASE` / `PORT_DRAM_K0_BASE` / `PORT_DRAM_SIZE`, used by `OS_K0_TO_PHYSICAL`, `dram.c`, `libultra.c` bases, `n64stubs.c`, the fast3d window checks; compile-time constants, no codegen change; menu / Bunker / Statue smoke clean). Left: the three link-time symbols in `port/src/dram_syms.s` (needs a preprocessed `.S` or `--defsym`), and a runtime-derived base for macOS; (2) widen `mema.c` (`memaspace.addr` s32 with `0xffffffff` sentinels, `memaRealloc`/`_memaFree` s32 params; the deferred D453 item); (3) build the high-arena test flavour (`GE_HIGHARENA_BASE`, tier 1 at 0x90000000, tier 2 above 4 GiB) and iterate on the first failures; (4) the heap-backed animation-table slots (`initanitable.c`); converges with #95's port_addr chokepoint. **2026-10-02 (D478):** read-side census re-run (319; 129 outside `chr.c` D34) triaged site by site: nothing live on the current layout; the remaining items are arena pointers in 32-bit slots and stack buffers through `s32` (safe only while stacks are low: `MAP_32BIT`, no ASLR on Windows), which is the Stage B / macOS list in the D478 entry. **2026-10-03 (D441):** complete census (424 compiler sites + 460 compiler-invisible grep hits, local `scratch/d441/`); Stage B item (3) tier 1 built (`GE_HIGHARENA_BASE=0x90000000`, `dram_syms.S`) and item (1)'s leftover link-time symbols done; 35 sign-extension sites fixed in 5 batches plus a fast3d `0x90000000` range literal; CI ratchet `tools_pc/abi_ratchet.py` (baseline 389). Tier 1 runs the menu and `-level_09` pixel-identical to the default build. **Still owed:** the tier-1 all-level sweep, tier 2 (above 4 GiB, needs the u32 slots widened, not zero-extension), the store-to-u32 tagging for macOS. |

## 3. Verification debt (fixed; a live check is owed)

A single campaign pass with these on the checklist would clear most of them.

| Item | Refs | Check |
|---|---|---|
| Presets: Original N64 / Port defaults | D440 | Press both rows in F10 and in the front-end PC Options with a mouse and a pad; draw distance applies from the next level load. `GE_OPTIONTREEPROBE` already fails on the base commit (separate look). |
| Probe strip (2026-09-30) | D245, D302, D318 | Frigate or Dam water/sky A/B against the previous build (the s16 sky path was removed); one Facility playthrough to confirm the D318 watchdog stays silent. |
| Infra fixes | D179, build-pc.sh, crash.c, CI | The 2026-09-30 re-exec no-op (PATHEXT=.CPL in the inherited env) is fixed and verified from the agent shell (see AGENTS.md). `./build-pc.sh` from a genuine MSYS2 login shell; the next CI run (pinned actions, workflow input check). `GE_CRASHTEST=6` shows resolved frames. The romdata `VirtualAlloc` failure path now fails boot instead of falling back to the heap copy: confirm you want that. |
| Light-fixture hit-type reads (impact sound + sparks) | D434, #119 | Shoot fixtures on Bunker and Caverns. |
| Dam/Caverns water animation | D465, #127 | Fixed 2026-10-01 (16-byte `Gfx` raw-index write). Maintainer: still "slightly faster" than 1964 on Dam; measured rate equals N64 within 0.1%. Optional objective check: time ten shimmer cycles on both (expect ~26 s). |
| End credits after Cradle | #129 | Did not reproduce on `release/v0.4.1` 2026-10-01 (maintainer finished Cradle on Agent, credits played, clean exit). Close #129 as not reproducing when the release ships. |
| v0.4.1 batch (unreleased; fixes live on `release/v0.4.1`) | D424–D432; #114 fire rate, #115 tank-crush audio loop, #116 tank movement, #117 sway, #118 GL recoil, #119 light fixtures | All user-verified against GEPD/1964 on 2026-09-29. Close the issues when it ships. |
| FOV-scale edge culling | D222 | Max FOV, pan across NPCs at the screen edge. |
| Water pulse, Surface 2 | D229 | Frigate is verified. Only the Surface 2 by-eye check is owed. |
| Linux save/config path when launched outside the game dir | D256 | Save-flow check on Linux. |
| Settings/F10 batch | D356, D358, D361, D363, D374, D406 | Live accept of each item in the F10 overlay. (D377–D379 residuals were waived.) |
| Controller feel | CONTROLLER-INPUT-PLAN Wave A, D204 | Per-stick deadzone/smoothing feel on a pad and on Deck; D204 tempo by ear; `Input.RumbleScale=0` silences rumble. |
| Widescreen HUD at 21:9 | D334/D335 | One ultrawide HUD playthrough. |
| Older "owed" notes | D371, D372 | **Bookkeeping done 2026-10-01:** D150, D177, D264 ticked off against the 2026-09-28 campaign sign-off; D369 accepted in use. Still owed: D371 (GEPD key-layout preset + reload/crouch bindings: select the preset in F10 and play a level). (D372 low-end preset: no further effort; low-end work is real optimizations, see §4.) |

## 4. Platform

| Item | Status | Refs | Notes |
|---|---|---|---|
| macOS (Intel + Apple Silicon) / ARM64 Linux | open (target 0.9.0/1.0, maintainer 2026-10-02) | PRs #88, #95; #101 | Outside drafts, parked for `main` until after 1.0. **Address model decided (2026-09-30): port_addr (#95)**, i.e. a single `portN64ToHost`/`PORT_N64PTR` chokepoint with a `PORT_ADDR_STRICT` validator, over the dram-scoped alternative. The `macos` integration branch **exists on origin** (2026-09-30). It was cut from `origin/main` `0e8c2ce2`, and it takes #88 (merged as `c4153157`, using #95's conflict-resolved copy `ae1884cc`), #132 (Linux build fix, `39d4f8d0`: #88's `romdata.c` included `<errno.h>` before `ultra64.h`, and glibc's `errno` macro broke the `errno` fields in `PR/os.h`; #95 needs it on rebase) and #95 (retargeted to it, still open). italoarruda's work also goes there. Nothing merges from it to `main` before the post-1.0 pass. **Open against `macos`:** #131, the fork-friendly self-hosted sweep. It adds `platform` and `runner_labels` inputs, a macOS job running `level_sweep_mac.sh` with a JSON verdict, seed-dir-only ROM staging, and `docs/selfhosted-sweep.md`. It is a scaffold that has not been run on a Mac, and it needs #95 in `macos` first. **When `macos` is next rebased onto `main`** (after the v0.4.1 push), `selfhosted.yml` will conflict with the v0.4.1 ROM-cache fix. Take #131's version, which already includes that fix. The crouched-rifle table fix (`chr.c`), found three times (#95, italoarruda, #120), landed on `release/v0.4.1` via #120 (D445) on 2026-09-30. #120's shape is the correct one (it restores the N64 table length of 2; #95's terminator-only version leaves length 1), so **drop #95's `crouched_rifle` hunk** when `macos` is rebased. #95's finding labels (D29x, then D405–D421) clash with ours, so relabel on merge. The maintainer tests firsthand before merging. Needs the ABI-hazard sweep in §2 (store-to-u32 tagging). |
| Platform parity gate (Linux co-equal with Windows) | open | — | **Decided 2026-09-30:** yes. Every released platform carries the same gate: a 21-level sweep plus a campaign pass. Needs the golden baselines on both platforms first (§7). The last known Linux crash (D190) was closed as not-a-bug. |
| Low-end GPUs (~30 fps on Celeron/HD 400) | fixed, verify on N3060 class | D339, D481, D482, #92 | **D482 (2026-10-02):** measured against the PD port on the X220, fast3d is as cheap per batch as PD's; the remaining gap is our heavier defaults. Atom/Celeron-class GPUs (HD 400-605, UHD 600/605, Bay Trail class, software renderers) now get DrawDistance/LodDistance 100 and MSAA 1 once, where still at the defaults (Cradle: 482 -> 146 batches, render CPU 8.2 -> 5.2 ms). **D481 (2026-10-02):** the root cause was architectural, not render cost: fast3d ran inline on the scheduler thread, delaying retrace delivery, so the game's tick gate skipped frames. With the render worker a Lenovo X220 (HD 3000) went from 44-46 to 60.0 fps at every setting; the PD port measured 60 on the same box. **Next only if N3060-class hardware still drops:** decide CPU vs GPU from `GE_PERFSTAT` there (`gpu=` vs `run=`), then either a renderer-keyed lighter default (GPU) or fast3d per-batch cuts diffed against PD's hot loop (CPU). **Earlier direction (maintainer, 2026-10-01): real optimizations, not a preset.** **2026-10-01 profile** (all-thread sampler, local note `PERF-HOTSPOTS.md`, probes on branch `perf/profile-hotspots`): the top render-thread hotspot was an uncached getenv per vertex batch, fixed as **D473** (Statue display-list CPU about -55%). After it, whole-process CPU is ~1.5-2 ms/frame on a fast machine (~8-10 ms scaled 5x), so the N3060 / HD 400 is probably GPU- or driver-bound (per-draw cost, MSAA, fill). **Next:** one `GE_PERFSTAT=1` run on that class of hardware (compare `gpu=` vs `run=` vs `interval=`), then target what dominates: draw-call/state-change reduction (needs a flush-reason histogram on the target), texture-bind lookups, per-triangle debug hooks, combiner lookup (ranked in the note). The D372 preset gets no further effort. |
| Statue/Cradle hitching (maintainer, 2026-09-30) | parked (not player-visible, 2026-10-02) | `GE_PERFSTAT` spike counters (2026-10-01) | **Diagnosed 2026-10-01 (D475):** no sustained load. The low-CPU ~33 ms frames are the game's own frame limiter skipping a retrace after a slightly late gfx task (`boss.c` tick-interval check; a D13-class timing interplay, needs a design look and possibly Rule-2). The CPU-side bursts are CI-palette re-imports (one texture re-imported with 51 palettes in ~8 frames), not first-sight loads; room-texture pre-warm was built and measured, with ~no gain (unmerged branch `perf/prewarm`). **CI8 palette re-imports ruled out as a CPU cost (D477, 2026-10-02):** decode+upload ~0.008 ms/frame, worst frame 0.44 ms; the cache already holds the animated palettes (10 re-misses in 1314); the same texture is drawn with several palettes per frame, so an in-place update path would thrash. GPU palette lookup is not justified by this data. CI4 part done (D476). Frame-limiter design look done 2026-10-02: no independent pacing bug; the skips follow long gfx tasks. **Next (if the hitches still matter):** a per-frame timeline probe (game-thread build + limiter decision, render-thread misses/new textures/shader compiles, per-frame GPU timestamps, swap time) over >=5 runs, PresentMon during real play at a spot that hitches, then a disable-falsifier per suspect (driver-deferred texture allocation, `glGenerateMipmap` under three-point filtering, shader compiles). Needs the maintainer's description of where the hitch shows. **Parked 2026-10-02:** the timeline probe was built (`probe/hitch-timeline`, `GE_HITCHTL`, never merge) and run during a ~2 min maintainer play session. The maintainer felt no hitching at their settings, only the FPS counter dipping to 58/59 as a level starts. The capture agrees: the long frames sit at level load and transitions (one 59 ms frame with 316 new textures + 4 shader compiles) plus one ~20 ms in-play cluster with 3-7 shader compiles and new GL textures (first-draw cost). Reopen only if a visible hitch is reported (e.g. on low-end hardware); the cheapest lever then is shader/texture pre-warm at level load. **D480 (2026-10-02, `perf/shader-prewarm`):** the in-play cluster was shader compiles; created shaders are now remembered in `ge007.shaders` and compiled at the first frame of the next session (0 in-play compiles on a warm run, output-identical). The level-start texture frame is left as is (see D480). |
| Steam Deck preset skipped when first launched from Desktop Mode | verify | D283 | Fixed on `release/v0.4.1`: the Deck is detected by DMI hardware id (Valve Jupiter/Galileo; `STEAMOS` kept as a second signal) and the preset applies once per ini, only while Fullscreen/Maximized are untouched. Only the Windows build and a Linux `-fsyntax-only` check ran. **Owed: one run on a real Deck** (Desktop-Mode-created ini, then Game Mode; expect fullscreen 1280x800 and the log line `video: Steam Deck (DMI Valve ...)`). The Known-issues row above goes when this ships. |
| Windows 7 | open | #98 | **Re-checked 2026-10-02: likely feasible around 1.0.** The exe imports no Windows 8+ functions; the bundled SDL2 2.32.10 (still supports Windows 7) and winpthreads only load newer APIs dynamically with fallbacks; msvcrt runtime; OpenGL 3.0 drivers exist on Windows 7. The real blocker is the first-run asset converter `ge007-convert.exe` (PyInstaller on Python 3.9+, which does not run on Windows 7): the native C converter (#6 Part B) removes it, or a separate Python 3.8 build. Work: one Windows 7 VM test, then either converter route; CI cannot test Windows 7, so it is a manual per-release check. Supersedes the earlier 'won't-fix (toolchain)' recommendation. |

## 5. Modern-remaster layer (port features beyond the N64)

**The bar:** feature parity with a Nightdive-style remaster and with
GE+/Dab's mod where it's practical. Shipped so far: native widescreen and
ultrawide, FOV, MSAA/anisotropic/texture-filter options (including N64
3-point), draw and LOD distance, VSync and a frame cap, show-FPS,
keyboard/mouse rebinding with a GEPD-style layout, pad deadzone,
sensitivity, smoothing, southpaw and triggers, rumble, crosshair and HUD
customisation, HUD scale, music/FX volume, no-hit-flash, SkipIntro
(experimental), All unlocked (experimental, see D387), and the F10 overlay.

The modern defaults (native widescreen, overscan crop, 250% draw/LOD
distance, bilinear filtering) are **deliberate**. They are not a fidelity
defect.

Background research lives in the maintainer's local notes (gitignored):
`GEPORT-REFERENCE-DEEPDIVE.md`, `MODERN-PORT-FEATURES-BACKLOG.md`,
`MODERN-OPTIONS-PLAN.md` and `PD-LEGACY-SURVEY.md` under `docs/dev/notes/`.

### 5a. Before 1.0

| Item | Status | Refs | Notes |
|---|---|---|---|
| `All unlocked` rework: RAM override instead of save patching | done | D442, D387, D259 | **Landed 2026-09-30 (D442)** on `release/v0.4.1`: query-time hooks in `file2.c` (`fileGetIsCheatUnlocked`, `fileIsStageUnlockedAtDifficulty`), RULE-2-SIGNOFF recorded; the EEPROM patch and the D387 merge are removed. **Verified 2026-10-03 (headless, frame dumps):** ON = all 20 levels in mission select, every difficulty ticked (real menu path), cheat menu fully populated (all 23 incl. Magnum/Laser/Golden Gun); OFF = only the save's real progress (Dam); `ge007.eep` byte-identical across all runs. MP character list not captured (same COMPLETED query path). Delete this row once released. |
| Settings units and values cleanup | done | D357, D443 | **Landed 2026-09-30 (D443):** mouse ×, whole-% deadzone and volume, FOV in horizontal degrees, MSAA 16×. FPS presets above 60 are deferred to the frame-rate-above-60 work (the sim ticks at 60 Hz). **Verified 2026-10-03 (headless frame dumps of Input/Graphics/Audio in F10 and front PC Options):** both UIs show the same values in the new units (mouse 1.0x, deadzones 23%, volumes %, FOV 75 deg, MSAA list up to 16x). Vibration now shows whole % too (was a raw 0.50). The `N/100` sliders (mouse vertical, look sensitivity, distances, trigger) are the calibrated-default scheme (50 = default), kept. Delete this row once released. |
| Restart button in the settings menus | done | D443 | **Landed 2026-09-30 (D443):** a "Restart game" row (orderly D344 quit, then relaunch). **Owed:** click it once on Windows (no BSOD); the Linux relaunch path has never been compiled. |
| Outside PRs #107–#109, #122–#124 | decided | #122 hot-plugged pads never reopen, #123 persistent crosshair (`gunfire.c`), #124 compact health/armour bars (`bondview2.c`); #107/#108/#109 (italoarruda) | **Decided 2026-10-01 after design reviews (notes: `docs/dev/notes/PR-REVIEW-109.md`, `PR-REVIEW-123-124.md`, local):** **#120/#121/#122** merged locally (#122 = D450, with split-screen); close all three when the branch is pushed. **#123** (persistent crosshair): accept-with-changes, **deferred**; later, either amend their PR or do the work ourselves (add to the Original N64 preset, relabel D436, our Rule-2 line, 2P + sniper-zoom check). **#124** (compact health/armour bars): **deferred** (pre- or post-1.0); too many issues to merge now: ignores HudScale, overlaps the bottom-left status text, 4:3-edge anchoring on ultrawide, draws extra commands when off, mixed apparent/real values, a test knob in shipping code, not in the Original N64 preset, split-screen untested. Either we take it on or ask the author to address the list when GitHub activity resumes. **#133** (F10 ultrawide aspect, JosephAHK, 2026-10-01): duplicates D472 (overlay card pillarboxed + mouse mapping, already on `release/v0.4.1`); close as superseded with thanks when the branch is pushed. **#109** (pad rebinding): **decline as-is**. It conflicts in all 4 files, would revert the D394 Jinx defaults, uses global (not per-seat) bindings, and its cancel is keyboard-only. Supersede with our own pad presets + rebinding (§5a row below), crediting the author's two-names format, menu lockout guard and release-before-capture arming (Co-authored-by); close with thanks and the change list when GitHub activity resumes. **#107**: folded into D441 on 2026-10-01 (both sites fixed with the author's `uintptr_t` arithmetic, Co-authored-by); close it on push. **#108**: belongs with `macos`; ask for a range check instead of widening the bound. Relabel any outside D-labels on merge. |
| Gamepad presets: per-seat preset, duplicate-binding warning | open | D469, D498, D394 | D469 (pad action table, Jinx 1.1 default, Custom per-seat bindings) and **D498** (Xbox 1.2 Christmas / 1.3 Frost / 1.4 Elektra) are done and maintainer-verified with pads (2026-10-01, 2026-10-03). Not done: a per-seat preset; a warning when two actions share a button; the Xbox "left stick aims the crosshair" and "D-pad copies the left stick" behaviours (D394). |
| Pad button names on a Nintendo pad | verify | D471; local note `PAD-MENU-AUDIT.md` | Audit done 2026-10-01 (no controller-only trap; D472 fixed G1-G3). D471 family button names + player LEDs: **PS5 DualSense verified 2026-10-03.** **Owed:** a Nintendo (Switch) pad check; the maintainer has no such pad, so this waits for one or for a user report. |
| Mute and auto-pause on focus loss; PNG screenshots to `screenshots/` | parked | D231, PR #56 (closed unmerged), `docs/dev/notes/parked/0001-QoL-*.patch` (local) | Deprioritised by the maintainer on 2026-09-25. F12 today is the dev PPM dump. |
| FPS tanks while adjusting F10 music volume with a controller | done | D489 | Maintainer, 2026-10-02 (seen on PAL; general, also on v0.4.0). Cause: every pad/arrow slider step saved to the EEPROM (3 joy-poll handshakes + file rewrite on the game thread). Fixed: steps stage, save once on release; merged into `release/v0.4.1` 2026-10-03. Maintainer pad check passed 2026-10-03. Delete this row once released. |
| Frame rate above 60 (120/144/uncapped with interpolation) | decision | [`dev/UNLOCKED-FPS-PLAN.md`](dev/UNLOCKED-FPS-PLAN.md) | The menu cap is {30, 60} (the ini accepts 0 = uncapped, but the sim ticks at the VI rate). The plan's Phase 0 is resolved by D248 (the default was a port bug; it is a real 60 fps) and its Phase 1 audit and Phase 2 slice scope are written (landed 2026-09-30), but the header still reads parked. Next step is a standing sim/render-rate measurement, not the old 30 fps investigation. Must stay compatible with netplay determinism. |
| Swapping saves with emulators | decided (after the D441 census) | D492 | **Investigated 2026-10-03 (D492):** the port stores the save checksum words and `options` little-endian, so an unconverted 1964 save fails validation and all five slots are wiped on first boot. Converting (swap `options`, recompute the checksum pair) was proven in a headless A/B: all slots OK, all 20 levels unlocked. **Decided:** `tools_pc/eep_convert.py` (both directions, `--verify`) plus a README "Using an emulator save" section; the on-disk format stays as is. Seed: `scratch/eepinv/convert.py`. |

### 5b. GE+ / Dab's-mod gameplay options (opt-in; touch game code)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Disable knockback / hitstun, damage sound, endless death cam | post-1.0 | hooks at `chraction.c:2233`, `gunfire.c:3334`, `bondview2.c:8354` | Needs one batched Rule-2 sign-off. **Decided 2026-10-01:** after 1.0, all opt-in (default OFF), one batched Rule-2 sign-off. |
| Bodies stay where they fell | post-1.0 | — | **Decided 2026-10-01:** after 1.0, opt-in. |
| Port-native third-person camera | post-1.0 | #110 | The 007 Plus ROM patch itself is out of scope. Decide whether a port-native TP camera is in or out. **Decided 2026-10-01:** decide in/out after 1.0 with the other §5b options. |

### 5c. Enhanced visuals (optional; after feature-complete)

The XBLA/Nightdive-style "new look". None of it has started in this repo.
An outside fork has a large Original/Enhanced/Remaster render-mode stack
(see *Community forks* below). Adopting any of it is its own decision and
needs Rule-2 review.

| Item | Status | Notes |
|---|---|---|
| Post-process chain: FXAA/SMAA, bloom, depth of field, colour grade/LUT, CRT filter, brightness/gamma, colourblind filters, SSAO | post-1.0 | Build the framebuffer pass once. Every effect defaults OFF. **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Near-full fog reveal (D503): far objects about 97% fogged show as faint silhouettes with straight room-clip edges on 32-bit output (e.g. Surface's dish) | triage | Two fix options, both opt-in or by-eye gated: (a) snap fog at or above ~97% to full fog (port-only, fast3d vertex fog; small, but changes fog everywhere slightly, needs a by-eye pass across levels); (b) an N64 16-bit colour/dither output mode (5 bits per channel, as the console framebuffer), a natural fit for the Original N64 preset and the "authentic 320x240" item below. Draw/LOD distance cannot help: 100 is already the N64 value. Measurements: HANDOFF 2026-10-03 / findings D503 (owed). |
| Render scale / supersampling, integer scaling, "authentic 320×240" mode | post-1.0 | **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Dynamic/coloured lighting, dynamic shadows, water reflections | post-1.0 | Large. Consult the PD port first. #105 is about the fork's version of reflections, not ours. **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Path-traced render mode (opt-in, RTGL1) | post-1.0 | Experimental, for fun (maintainer, 2026-10-01): **deprioritised to after 1.0**. Plan in the maintainer's local notes: vendor MIT RTGL1 behind `port/rt/`, default OFF; fast3d stays the default backend and the fidelity ground truth; Phase 0 is a toolchain spike (RTGL1 under MSYS2/MINGW64). Nothing starts before 1.0. |
| HD texture packs: loader + texnum dumper; model packs | post-1.0 | #100. There is a staged design. Step 1 (texture identity registry) touches `src/game`, so it needs Rule-2 framing. **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Animation smoothing (keyframe slerp) | post-1.0 | **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |

### 5d. Extras (nice to have)

In-game console · demo/replay record and playback (doubles as the
determinism harness, §7) · photo mode · benchmark mode · subtitles · HUD
opacity and a flash-intensity slider · a screen-shake slider that covers
every shake source (D181 only scales explosions) · F11 video recording ·
in-game patch notes and update check · mod loader (parked: branches
`land/modloader-*`, D413/D414; only the content tier M1/C3′ is viable here)
and a stage loader.

## 6. Multiplayer beyond the N64

| Item | Status | Refs | Notes |
|---|---|---|---|
| LAN netplay | post-1.0 | — | Depends on split-screen. Determinism groundwork is D117 (not achieved). **Decided 2026-10-01:** in scope after 1.0. |
| MP bots | post-1.0 | #111 | Not N64 (GoldenEye's MP is human-only; Simulants are Perfect Dark). birdturtle `simulants` is an outside prototype. **Decided 2026-10-01:** in scope after 1.0 (opt-in; not N64). |
| Co-op campaign | post-1.0 | — | Not N64. thepont `feature/coop` is an outside prototype. Listed under Not planned unless this changes. **Decided 2026-10-01:** in scope after 1.0 (opt-in; not N64). |
| Cheats beyond the N64 cheat menu | post-1.0 | #113 | Only in scope as an opt-in toggle. **Decided 2026-10-01:** in scope after 1.0, opt-in toggles only. |

## 7. Tooling, infrastructure, security

| Item | Status | Refs | Notes |
|---|---|---|---|
| Security review follow-ups | open | maintainer's local security review | Three Low hardening items remain (local-input only; details are kept out of this public file). Done on `release/v0.4.1`: the Medium item (self-hosted workflow input handling), actions pinned by SHA, `pyinstaller` pinned to 6.22.3, and the ROM no longer stored in `actions/cache` by `selfhosted.yml` (fork PR workflows can restore base-branch caches; no cache entry ever existed, checked 2026-09-30; it is now staged from the runner's `_rom-seed` dir only). The Dependabot action bumps (#1/#2) would now need to land as SHA pins, not tag bumps. |
| 1964-vs-port comparison harness | partial | `tools_pc/abpad/` | **2026-10-02:** one millisecond timeline drives 1964 (virtual XInput pad through the maintainer's own NRage profile) and the port (`GE_INPUTSCRIPT_MS`), with both windows captured on one clock and side-by-side sheets; verified through intro skip, file select and folder open. Not frame-identical (1964 has no seed pin or frame dump), so it's a timing/behaviour comparison, not a pixel diff. **Left:** in-level timelines (load a level on both sides, e.g. the D486 turret check); the two sides use different save files. **2026-10-03:** scripted menu navigation dropped (the free cursor lands on different cells on each side); replaced by `live_ab.ps1` (1964 then port after `-PortDelayMs`, 1280x720 each, position-only) with one real pad (`GE_PAD_BACKGROUND=1`). C-button hold tokens (`CUPHOLD`...`CNONE`) added for 1.2 walking in scripts. |
| Bond seems to move faster on the port than on 1964 (live A/B) | parked | — | Maintainer, 2026-10-03, live one-pad A/B (`tools_pc/abpad/live_ab.ps1`). Not investigated. Likely input mapping, not game speed: the port forces 1.2 SOLITARE and turns the left stick into digital C-buttons past a threshold (`port/src/input.c` ~2717), so a slightly off-centre push strafe-runs; 1964 is probably 1.1 analog through NRage (speed depends on Range/deadzone). Next step: match control styles (1964 save on 1.2, NRage left stick as C-buttons), push straight forward, then compare on a fixed route. |
| Scripted-playthrough harness | feasibility spike (after D441 and the worker tasks) | `GE_INPUTSCRIPT`, D117 | Input script + `GE_PCDUMP` + crash-log check, for unattended regression runs. **Maintainer concern (2026-10-03):** aiming/shooting, PRNG guards and timed events may block open-loop scripts, and runs aren't deterministic (D117). Levers: the game's own Invincibility + Invisibility cheats, closed-loop waypoint steering from Bond's position (port hook), a pinned seed (`GE_RSEED`), and coverage routes (load, walk, doors) instead of objectives. Spike: one Dam route run 5 times, measure end-position spread, then go/no-go. |
| Cheap worker for mechanical, build-checked tasks | open | `delegate-local` MCP; memory of past Qwen issues | Birdman64's fast worker model wrote 44% of its commits while the lead planned and reviewed. Ours: pattern-applied edits (D441 fix batches, scaffolding strips, small tools), each gated by a clean build + a specific check + the lead's diff review, in the worker's own worktree. Local Qwen for edit-only tasks (it fabricates on reading tasks), or a hosted fast model via `delegate_to_provider` (cost is the maintainer's call). |
| Golden baselines | partial | `tools_pc/verify.sh`, `tools_pc/golden/README.md`, D117 | **Windows done 2026-10-02:** all 21 levels re-based (`tools_pc/golden/<level>/win/`) with a near-deterministic recipe (pinned seed, START skips the intro flyby, frames 900-1500 of settled gameplay; Cuba captures its ending); two-tier gate (structural + per-pixel with per-level limits) catches a global texture-filter change (22% vs <= 2.3% noise). verify.sh now quits via `GE_QUITFRAME` and never kills other game instances. **Left:** Linux baselines (needs a real-GPU Linux box: `GE_PCDUMP` reads black on llvmpipe/WSLg), then the parity gate per platform. `GE_DETERM` stays experimental (not needed for this recipe). |
| Flicker scan (A/B/A frame alternation) | parked (2026-10-03: tooling, nothing blocked on it; spec in the local investigation report `scratch/birdman64-rows-investigation.md` §4) | `GE_PCDUMP`, `GE_INPUTSCRIPT` | Dump **every** frame of a scripted run at low resolution and flag frame n when it differs from n-1 but matches n-2. Dumping every Nth frame hides this kind of flicker by design. Cheap once the scripted-playthrough harness exists. For any old-vs-new pixel diff outside the golden gate, run a same-commit control first to measure the noise floor, as the golden gate already does. Reference: Birdman64 `tools/flicker_scan.py`, `docs/notes/sweep.md`. |
| Release build-provenance attestation | decided | `.github/workflows/ci.yml` release job; `.github/SECURITY.md` | **Decided 2026-10-03:** add SHA-pinned `actions/attest-build-provenance` to the tag-only release job, with `id-token: write` on that job only. It backs SECURITY.md's "unsigned, but reproducible from this repository" stance with a check anyone can run (`gh attestation verify <file> --repo jkdansereau/goldeneye-pc-port`); document that in SECURITY.md and the README. Verifiable only on the next tag. Dropped: a combined `SHA256SUMS` (no security gain over the per-file `.sha256` from the same page) and `READ ME FIRST.txt` (duplicates the bundled README). |
| Self-hosted CI runner link failure | open | D244 | Needs a `pacman -Syu` on the runner (maintainer-only access). Release tag builds use GitHub runners, so it doesn't block a release. |
| Remove temporary scaffolding before a release | partial | [`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md), D302 | The 2026-09-30 pass removed D207S, D157, D252POOL, D245V, D288, D309, the D318B suite, the D320 repro harness and `scan_op12/13.py`, demoted the D318 watchdog to detect-and-log, and cached the per-frame `getenv` sites. **2026-10-03 (batch 5, `chore/strip-batch5`):** removed the dead registry probes D116, D51, D56, D60-D63, D69/D69BB/D69STAN, D71LOG, D85DUMP, D86-D88, D90, D96, D154, D178 (logging only; ~500 lines; build clean, intro + `-level_09` smokes clean). **Left:** D104 (kept while D294 is in verify); closed-finding TEMPs (**removed 2026-10-02 (batch 4, `chore/strip-batch4`):** `GE_D235_OFF`/`_RELOAD`/`_NEXT` (D235 closed; the fix and the `GE_D235` log stay), `GE_D245V`, `GE_D245_FIXEDSHIFT` and the inert sky tile-capture/shift code in `sky.c`; registry drift fixed and `gen_env_probes.py --check` passing again; **removed 2026-10-01:** `GE_D306C`, `GE_D318T` (batch 2); the whole D243 diagnostic set -- `GE_D243X2`, `GE_D243X4`, `GE_D243M` (+ its `d243mProbeActive`/frame-counter plumbing and all `D243M:` log lines in `bondview2.c`/`chr.c`/`chraction.c`/`chrai.c`/`model.c`/`objecthandler.c`), `GE_D243CAM`, `GE_D243SWIRL`, `GE_D243` (front.c menu trace) (batch 3, `chore/strip-d243-probes`; the shipped fix and the M-183/M-185 clamps are kept); `GE_DYNTEXHASH_OFF`, `GE_FORCEALARM`, `GE_D204_OLD`, `GE_D252`, `GE_RSEED_LV` were already gone); and the M-183/M-185 clamps, which need a full cutscene re-check first. |
| Findings log and doc size | partial | `tools_pc/docs_budget.py` | The budget checker exists (per-file and tier-1 token budgets; `--toc FILE`), and everything is within budget. `findings.md` is still about 2 MB (~500k tokens), so agents must keep using the index. Remaining: nothing blocks; trim only if the budget check starts failing. |
| Branch and PR housekeeping | open | — | 30 landed local branches pruned 2026-09-30 (tips kept in the notes archive, `prune-branches-2026-09-30.txt`). `docs/m138-security-disclosure` landed on `release/v0.4.1` with a stale-fact pass (SECURITY.md, the security-and-fidelity page, README roadmap bullet). `feat/unlocked-fps-phase1-audit`'s commit (Phase 0 resolved by D248, Phase 1 audit) landed as `docs/unlocked-fps-audit`. **On hold by the maintainer (2026-09-30, do not prune yet):** probe-only leftovers `d243-verify-local`, `investigate/d243-cutscene-race`, `park/d236-probes-m139`. Local `main` and `release/v0.4.1` are deliberately not pushed. **Pushed on 2026-09-30:** the `macos` integration branch; `ci/selfhosted-fork-runners` (PR #131 into `macos`, open); `fix/macos-errno-include` (PR #132, merged into `macos`). Pruned after merging, 2026-09-30: `fix/selfhosted-rom-no-cache`, `docs/macos-ci-tracking` and `fix/macos-errno-include` (local and remote), plus the `review/pr88` and `review/pr95` read-only refs. PR #62 (mouse dt-decouple) stays held until mouse feel is fundamentally better. |
| Docs site: Web 1.0 phase 2 | decision | — | Phase 1 shipped. Phase 2 (centred serif page) is plan-only. |

## Community forks (not ours; reference only)

| Fork | What it has | Relevance |
|---|---|---|
| thepont/goldeneye-pc-port `feature/graphics-final-stack`, `fix/original-water-fidelity`, `feature/coop` | ORIGINAL/ENHANCED/REMASTER render mode, lighting, post-FX, bump/shadow/DoF, water reflections, vector HUD font, 2P co-op | Source of #105. Heavy `src/game` edits. **Their D323–D326 and "D329" labels clash with ours**, so relabel anything ported. |
| birdturtle `feature/local-mp-input`, `simulants` | Local MP restore; first MP bot | Cross-check for #99 and #111. |
| kaziema `vita` | PS Vita port | Platform reference. |
| mattymattmattmatt/goldeneye-pcvr-port `vr/stereo-headaim` | VR stereo + head aim | Platform reference. |

## Decisions owed (maintainer)

Decided 2026-09-30 (moved into the rows above): PAL/JP goes in Stage B;
`All unlocked` becomes a RAM override; D357 approved with changes;
platform parity gate; feature PRs get a design review first; the
#88/#95 address model is port_addr. Decided 2026-10-01: D417 approved (split-screen merged); the gameplay-visibility policy (§2); §5b, §5c and §6 are post-1.0 and opt-in (co-op included); D433 gets a code audit, D323 goes post-1.0; #94 closes as stale on release. Still owed:

Nothing else owed right now (outside PRs decided 2026-10-01, see §5a).

## Not planned

Online (non-LAN) multiplayer, matchmaking, leaderboards, anti-cheat,
cross-play · Steamworks, achievements, cloud saves (no storefront) · HDR,
frame generation, stereo 3D (ray tracing only as the opt-in post-1.0
experiment in §5c) · remake-scope assets (new
models, music or voice) · new movement mechanics (jump, roll, melee) · the
007 Plus ROM patch as such (#110) · a per-user save folder (decided
2026-10-03: saves stay next to the game, which keeps the security doc's
"never writes to `%APPDATA%`" promise; README *Updating* covers moving them) ·
a first-launch controls card and controller toasts (decided 2026-10-03: new
UI for little gain; F10 is documented in both READMEs).
