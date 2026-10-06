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

## Known issues

What a player will notice in the current release. **This table is the
single list:** the README links here instead of keeping its own copy. When
an issue is fixed and released, delete its row. When a new player-visible
issue is confirmed, add a row here (and its tracking row in the sections
below).

| Issue | Impact | Workaround | Tracking |
|---|---|---|---|
| Multiplayer isn't available yet | Missing feature | **Fixed on `release/v0.4.1`** (2–4 player split-screen, merged 2026-10-01); delete this row when it ships | #99, §1 |
| PAL and JP ROMs aren't supported | NTSC-U only | Use an NTSC-U ROM | D258, §1 |
| `All unlocked` may already have written fake unlocks into a save from an older build | Save corruption | **Fixed on `release/v0.4.1`**: `All unlocked` is now a RAM-only override (D442) and never writes the save. Fakes an older build already wrote are not repaired; back up `data/ge007.eep`. Delete this row when it ships. | D442, D387, §5a |
| `Game.SkipIntro` also skips the post-mission failure dossier | Minor | Leave SkipIntro off | D408, §2 |
| F10 overlay stretches in native widescreen (menus pillarbox correctly) | Cosmetic | **Fixed on `release/v0.4.1`** (D472 pillarboxes the overlay card); delete this row when it ships | D335b, D472, §5a |
| Rareware logo shows a subtle texture-filtering artifact | Cosmetic | — | D75, §2 |
| Steam Deck: a first launch in Desktop Mode skips the Deck preset | Settings | **Fixed on `release/v0.4.1`** (real-Deck check owed); delete this row when it ships. Until then: first launch in Game Mode, or delete `ge007.ini` and relaunch | D283, §4 |
| ~30 fps on very low-end GPUs (e.g. Intel HD 400) | Performance | Set `Video.LowEndMode=1` in `ge007.ini` | D339, #92, §4 |
| No macOS or ARM builds | Platform | — | #88, #95, #101, §4 |
| Controller buttons can't be rebound in-game (keyboard/mouse can) | Missing feature | **Fixed on `release/v0.4.1`** (D469, F10 → Input → Controller); delete this row when it ships | D469, #109, §5a |

---

## 1. Feature completeness vs the N64 cartridge

| Item | Status | Refs | Notes |
|---|---|---|---|
| **2–4 player split-screen multiplayer** | verify | #99, D416–D423, merged into `release/v0.4.1` 2026-10-01 (from `mp/virtual-seats`) | Merged 2026-10-01. D417 Rule-2 approved. Maintainer live test: every MP map in 2P (2–4 kills per map, each player scored), 4P Temple, two Xbox pads mapped P1/P2. Headless 2P/3P/4P pause, watch-menu pages, exit-confirm and F10-overlay runs clean. D420 fixed (Scores overrun), D421 closed (moot: no MP gadgets), D423 closed (faithful shadow). Still owed: 3P/4P with real pads, pads-only mode, MP audio, the full N64 MP setup screens (scenarios, handicaps, teams), D422 centred aim live on P2+ (`AimMode = 1`), D428 sway on P2+, and whether the `GE_STARTMP` harness skips something the ammo counter needs (seen only in harness launches). Per-pad seats (§5) done (D448/D450). |
| **PAL and JP ROM support** | open | D258, D75 (pal/jpn sidecar regen), #85 | **Scheduled for Stage B (decided 2026-09-30):** done just before 1.0 so that region-specific issues can be unwound in isolation. Conversion is broken at the source-data level (filelist naming). A repair path has been verified but not applied. Needs PAL/JP ROMs to test. #85 was closed as "retired for now", not won't-do, so public users currently have no tracker. Reopen it or file a fresh one when work starts. |
| Drop-in ROM converter, remaining parts | decision | #6, [`dev/release-dropin-rom-plan.md`](dev/release-dropin-rom-plan.md) | NTSC-U drop-in shipped. Left: per-region output names and re-exec guard (with PAL/JP), native C emitters to replace the PyInstaller converter. |
| Cut content (TCRF weapons/items) | decision | [`dev/CUT-CONTENT-BACKLOG.md`](dev/CUT-CONTENT-BACKLOG.md) | Not part of the shipped game. Tier 0 already works via the debug/cheat menus. Tier 1 (config-toggle grants) is small and port-only. Tier 2/3 (placing props, re-enabling unused MP maps) need game data changes and are out of scope for this repo. |

## 2. Accuracy (behaviour differs from the N64)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Auto-fire gunshot sound slightly differs from 1964/GEPD | open | D433, #114 | Cadence matches the N64 rules exactly. The residual is subtle differences in the click/empty-magazine sounds between shots during continuous AK47 fire. **Maintainer-owned (2026-09-30):** possibly already fixed; it needs a recorded A/B against 1964/GEPD, which isn't suited to agents. Done together with the WrapFix check below. **Decided 2026-10-01:** code-side audit of the fire/empty-click sound path first (with #130), before any by-ear A/B. |
| Auto-fire gate at unstable 40–55 fps | partial | D427 | Exact at a steady 60 or 30 fps. At fluctuating rates it can flip between 1× and 2×, because `field_88C` is never rescaled. |
| Third-person Bond offset residual | partial | D173, D292 | Mostly fixed. A small drift remains about 1 time in 10. |
| Statue corner flicker at max FOV | verify | D294 | Room-pool cap raised 2.0 -> 2.5 (2026-09-30; Statue needs about 2.17x). It is unproven: the exhaustion log never fired at spawn. **Owed:** strafe fast past the Statue corners at max FOV and max draw distance. |
| Gameplay visibility vs modern view options (FOV, ultrawide) | verify | D466, D468, D334, D222; maintainer's local policy note | **Policy approved 2026-10-01 and implemented:** AI awareness may see what an N64 player could have seen in some configuration the cartridge supports. D466: the AI uses the authored draw distance (the player can still shoot, use and photograph whatever is drawn). Widescreen up to 16:9 is faithful (the cartridge's own Ratio 16:9 mode). **D468 (merged 2026-10-01):** under ultrawide or `Video.FovScale` > 100 the AI's on-screen view is clamped to the 16:9 / game-FOV view; option `Game.AIWideView` (F10 Gameplay, default off) gives the AI the full view. **Owed:** by-eye check on an ultrawide window or at a high FOV. D466 follow-up (2026-10-01, maintainer decision): movement/locomotion LOD follows the render, only awareness uses the N64 distance. |
| Golden Gun plays the empty click with each shot | open | #130 | Outside report: the out-of-ammo sound plays alongside the firing sound, unlike 1964. Check by ear together with the D433 auto-fire A/B (maintainer-owned). |
| A mine in Control's mainframe room didn't destroy anything | open | #126 | Outside report; the reporter is unsure. Sanity-check on 1964 first (the objects may be indestructible there). |
| Crash on save slots 1/3/4, plus a host BSOD (fltmgr.sys) | decided | #94 | Needs a repro. Two maintainer retest requests (v0.3.0, v0.4.0) got no reply. Likely D299/D301 (save-slot crash, fixed) and D344 (BSOD on exit, fixed). **Decided 2026-10-01: close as stale when the release ships**, pointing at D299/D301/D344; no GitHub activity before then. |
| SkipIntro skips the post-mission failure dossier | verify | D408, D216 | Fixed 2026-09-30: the hook is gated to first boot. **Owed:** fail or abort a mission with SkipIntro on and check the dossier shows; also listen for whether D216's audio break is gone. |
| Front-end: Rareware logo filtering, cast-roll models | partial | D75 | Cosmetic. The other front-end logos are fixed. The cast-roll character models have never been verified. |
| Authored mip chains not sampled for explicit-LOD textures | post-1.0 | D323 | Distant textures use driver mips instead of Rare's hand-authored chain (heaviest on Statue/Aztec). Parked as a fidelity enhancement. **Decided 2026-10-01:** after 1.0, together with the LOD-fraction approximation as one "real RDP LOD" project. |
| Approximations in the software RSP | open | `gfx_pc.cpp` `G_CCMUX_LOD_FRACTION`; `Video.WrapFix` (§F row `RC3 · D167`, D74 family) | The LOD-fraction CC input is an eyeballed approximation. The non-power-of-two wrap fix is still opt-in (default off), with unknown visible impact. Both need a comparison against 1964. **2026-09-30 triage** (`docs/dev/notes/FIDELITY-TRIAGE.md`): the LOD-fraction input is bundled with D323 as one "real RDP LOD" project (fix later, large). **WrapFix is maintainer-owned:** a 1964 Depot-ceiling screenshot vs the port with WrapFix on/off decides the default. |
| Latent 64-bit ABI hazards | partial | D441, #107, #108; ~396 `-Wpointer-to-int-cast` sites at the start of the sweep (185 remain in the 17 touched files; many are intentional `(s32)&ANIM_DATA_*` offsets, D34); AUDIT-M6 `struct player`/`struct hand` raw offsets (porting-notes §A1) | Part 1 landed (D441): 17 files widened to `uintptr_t` under `#ifdef PORT`, build and Bunker/Silo runs verified. **Still owed:** the full census with per-site provenance (H/R/I/?, table not yet written) plus a **store-to-u32 vs arithmetic-only** tag per site (a `uintptr_t` widening is enough only for arithmetic; sites that store back into a u32 field need `portHostToN64` under the macOS port_addr model), an env-gated high-arena test mode, and a check under an arena above 4 GB. Harmless today because the arena sits low. They would bite on macOS/ARM/ASLR. **#108 concern:** widening the `sndPlaySfx` bound disables the guard on every platform; prefer a range check. **#107 will now conflict** with D441 in `bg.c` (`bgRoomCalcBB`), so rebase or close it during the outside-PR review. A read-only provenance pass over the remaining sites is a good local-Qwen delegate task. **2026-10-01:** D457(c)(d) merged (player matrix pointers, Model/PROMOTE/vtxstore pointer width); #107's two sites fixed (D441); read-side census part 2 (403 `-Wint-conversion`/`-Wint-to-pointer-cast` warnings, triaged by file) in the local note `ABI-CENSUS-D441-PART2.md`; model.c rows need a re-run after D457(d). **Stage B plan (2026-10-01, maintainer's local design note):** (1) centralise the hard-coded arena address windows: **done 2026-10-02** (`port/include/portaddr.h`: `PORT_DRAM_V1_BASE` / `PORT_DRAM_K0_BASE` / `PORT_DRAM_SIZE`, used by `OS_K0_TO_PHYSICAL`, `dram.c`, `libultra.c` bases, `n64stubs.c`, the fast3d window checks; compile-time constants, no codegen change; menu / Bunker / Statue smoke clean). Left: the three link-time symbols in `port/src/dram_syms.s` (needs a preprocessed `.S` or `--defsym`), and a runtime-derived base for macOS; (2) widen `mema.c` (`memaspace.addr` s32 with `0xffffffff` sentinels, `memaRealloc`/`_memaFree` s32 params; the deferred D453 item); (3) build the high-arena test flavour (`GE_HIGHARENA_BASE`, tier 1 at 0x90000000, tier 2 above 4 GiB) and iterate on the first failures; (4) the heap-backed animation-table slots (`initanitable.c`); converges with #95's port_addr chokepoint. |

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
| macOS (Intel + Apple Silicon) / ARM64 Linux | open | PRs #88, #95; #101 | Outside drafts, parked for `main` until after 1.0. **Address model decided (2026-09-30): port_addr (#95)**, i.e. a single `portN64ToHost`/`PORT_N64PTR` chokepoint with a `PORT_ADDR_STRICT` validator, over the dram-scoped alternative. The `macos` integration branch **exists on origin** (2026-09-30). It was cut from `origin/main` `0e8c2ce2`, and it takes #88 (merged as `c4153157`, using #95's conflict-resolved copy `ae1884cc`), #132 (Linux build fix, `39d4f8d0`: #88's `romdata.c` included `<errno.h>` before `ultra64.h`, and glibc's `errno` macro broke the `errno` fields in `PR/os.h`; #95 needs it on rebase) and #95 (retargeted to it, still open). italoarruda's work also goes there. Nothing merges from it to `main` before the post-1.0 pass. **Open against `macos`:** #131, the fork-friendly self-hosted sweep. It adds `platform` and `runner_labels` inputs, a macOS job running `level_sweep_mac.sh` with a JSON verdict, seed-dir-only ROM staging, and `docs/selfhosted-sweep.md`. It is a scaffold that has not been run on a Mac, and it needs #95 in `macos` first. **When `macos` is next rebased onto `main`** (after the v0.4.1 push), `selfhosted.yml` will conflict with the v0.4.1 ROM-cache fix. Take #131's version, which already includes that fix. The crouched-rifle table fix (`chr.c`), found three times (#95, italoarruda, #120), landed on `release/v0.4.1` via #120 (D445) on 2026-09-30. #120's shape is the correct one (it restores the N64 table length of 2; #95's terminator-only version leaves length 1), so **drop #95's `crouched_rifle` hunk** when `macos` is rebased. #95's finding labels (D29x, then D405–D421) clash with ours, so relabel on merge. The maintainer tests firsthand before merging. Needs the ABI-hazard sweep in §2 (store-to-u32 tagging). |
| Platform parity gate (Linux co-equal with Windows) | open | — | **Decided 2026-09-30:** yes. Every released platform carries the same gate: a 21-level sweep plus a campaign pass. Needs the golden baselines on both platforms first (§7). The last known Linux crash (D190) was closed as not-a-bug. |
| Low-end GPUs (~30 fps on Celeron/HD 400) | open | D339, #92 | **Direction (maintainer, 2026-10-01): real optimizations, not a preset.** **2026-10-01 profile** (all-thread sampler, local note `PERF-HOTSPOTS.md`, probes on branch `perf/profile-hotspots`): the top render-thread hotspot was an uncached getenv per vertex batch, fixed as **D473** (Statue display-list CPU about -55%). After it, whole-process CPU is ~1.5-2 ms/frame on a fast machine (~8-10 ms scaled 5x), so the N3060 / HD 400 is probably GPU- or driver-bound (per-draw cost, MSAA, fill). **Next:** one `GE_PERFSTAT=1` run on that class of hardware (compare `gpu=` vs `run=` vs `interval=`), then target what dominates: draw-call/state-change reduction (needs a flush-reason histogram on the target), texture-bind lookups, per-triangle debug hooks, combiner lookup (ranked in the note). The D372 preset gets no further effort. |
| Statue/Cradle hitching (maintainer, 2026-09-30) | open | `GE_PERFSTAT` spike counters (2026-10-01) | **Diagnosed 2026-10-01 (D475):** no sustained load. The low-CPU ~33 ms frames are the game's own frame limiter skipping a retrace after a slightly late gfx task (`boss.c` tick-interval check; a D13-class timing interplay, needs a design look and possibly Rule-2). The CPU-side bursts are CI-palette re-imports (one texture re-imported with 51 palettes in ~8 frames), not first-sight loads; room-texture pre-warm was built and measured, with ~no gain (unmerged branch `perf/prewarm`). **CI8 palette re-imports ruled out as a CPU cost (D477, 2026-10-02):** decode+upload ~0.008 ms/frame, worst frame 0.44 ms; the cache already holds the animated palettes (10 re-misses in 1314); the same texture is drawn with several palettes per frame, so an in-place update path would thrash. GPU palette lookup is not justified by this data. CI4 part done (D476). Frame-limiter design look done 2026-10-02: no independent pacing bug; the skips follow long gfx tasks. **Next (if the hitches still matter):** a per-frame timeline probe (game-thread build + limiter decision, render-thread misses/new textures/shader compiles, per-frame GPU timestamps, swap time) over >=5 runs, PresentMon during real play at a spot that hitches, then a disable-falsifier per suspect (driver-deferred texture allocation, `glGenerateMipmap` under three-point filtering, shader compiles). Needs the maintainer's description of where the hitch shows. |
| Steam Deck preset skipped when first launched from Desktop Mode | verify | D283 | Fixed on `release/v0.4.1`: the Deck is detected by DMI hardware id (Valve Jupiter/Galileo; `STEAMOS` kept as a second signal) and the preset applies once per ini, only while Fullscreen/Maximized are untouched. Only the Windows build and a Linux `-fsyntax-only` check ran. **Owed: one run on a real Deck** (Desktop-Mode-created ini, then Game Mode; expect fullscreen 1280x800 and the log line `video: Steam Deck (DMI Valve ...)`). The Known-issues row above goes when this ships. |
| Windows 7 | decision | #98 | Recommend won't-fix (toolchain). |

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
| "Original N64" preset: pillarbox at the original aspect | verify | D440, D447 | Landed 2026-09-30 (D447): `Video.AspectMode` (Window/Original), set by the Original N64 preset. Exact 4:3, or 16:9 while the game's watch Ratio is 16:9. Letterboxes in taller windows. `Crop overscan` no longer eats the game's Wide/Cinema letterbox. Measured: 160 px bars at 1280x720. Window-mode frame diff is within the run-to-run noise floor (dmean 0.93 vs base-vs-base 0.85). **Owed:** compare 4:3 and 16:9 against 1964; Wide/Cinema with crop on; menu mouse alignment in Original mode; MSAA 4/8/16, fullscreen toggle, Linux build. F10 full-window is deferred to an F10 polish pass. Known: with crop off, the D246 one-unit edge line shows at the rect edges. |
| `All unlocked` rework: RAM override instead of save patching | verify | D442, D387, D259 | **Landed 2026-09-30 (D442)** on `release/v0.4.1`: query-time hooks in `file2.c` (`fileGetIsCheatUnlocked`, `fileIsStageUnlockedAtDifficulty`), RULE-2-SIGNOFF recorded; the EEPROM patch and the D387 merge are removed. **Owed:** every level at every difficulty with a cheat on; Magnum/Laser/Golden Gun rows; MP characters; OFF restores real progress. |
| Settings units and values cleanup | verify | D357, D443 | **Landed 2026-09-30 (D443):** mouse ×, whole-% deadzone and volume, FOV in horizontal degrees, MSAA 16×. FPS presets above 60 are deferred to the frame-rate-above-60 work (the sim ticks at 60 Hz). **Owed:** the new values in both settings UIs. |
| Restart button in the settings menus | done | D443 | **Landed 2026-09-30 (D443):** a "Restart game" row (orderly D344 quit, then relaunch). **Owed:** click it once on Windows (no BSOD); the Linux relaunch path has never been compiled. |
| Front-end PC Options: Previous/Next paging for mouse users | verify | D444 | **Landed 2026-09-30 (D444):** Previous/Next page buttons; hover no longer snaps the wheel selection. **Owed:** a mouse pass through Input and Graphics. |
| Outside PRs #107–#109, #122–#124 | decided | #122 hot-plugged pads never reopen, #123 persistent crosshair (`gunfire.c`), #124 compact health/armour bars (`bondview2.c`); #107/#108/#109 (italoarruda) | **Decided 2026-10-01 after design reviews (notes: `docs/dev/notes/PR-REVIEW-109.md`, `PR-REVIEW-123-124.md`, local):** **#120/#121/#122** merged locally (#122 = D450, with split-screen); close all three when the branch is pushed. **#123** (persistent crosshair): accept-with-changes, **deferred**; later, either amend their PR or do the work ourselves (add to the Original N64 preset, relabel D436, our Rule-2 line, 2P + sniper-zoom check). **#124** (compact health/armour bars): **deferred** (pre- or post-1.0); too many issues to merge now: ignores HudScale, overlaps the bottom-left status text, 4:3-edge anchoring on ultrawide, draws extra commands when off, mixed apparent/real values, a test knob in shipping code, not in the Original N64 preset, split-screen untested. Either we take it on or ask the author to address the list when GitHub activity resumes. **#109** (pad rebinding): **decline as-is**. It conflicts in all 4 files, would revert the D394 Jinx defaults, uses global (not per-seat) bindings, and its cancel is keyboard-only. Supersede with our own pad presets + rebinding (§5a row below), crediting the author's two-names format, menu lockout guard and release-before-capture arming (Co-authored-by); close with thanks and the change list when GitHub activity resumes. **#107**: folded into D441 on 2026-10-01 (both sites fixed with the author's `uintptr_t` arithmetic, Co-authored-by); close it on push. **#108**: belongs with `macos`; ask for a range check instead of widening the bound. Relabel any outside D-labels on merge. |
| Gamepad rebinding + modern controller presets | verify | D469 (supersedes PR #109), D394 | **Landed 2026-10-01 (D469):** data-driven pad action table; `Input.PadPreset` (0 = Jinx 1.1, default, proven identical to the old mapping over 20,971,520 synthetic cases; 3 = Custom); per-seat `Input.Pad[N].<Action>` bindings; F10 / front-end "Controller..." page with pad capture (B/Back cancel, hold Back / Y clear); menu lockout guard; credited to italoarruda (#109). **Maintainer live test 2026-10-01:** bind, cancel, clear, 2P per-seat tables, hot-plug and relaunch persistence pass; the Restart row (D443) works. **Still owed:** more play with the default preset ("plays as before") and longer controller-only use. **Not done:** Jinx 1.2/1.3 tables (D394 has no reference tables; presets 1/2 hidden), a per-seat preset, a warning when two actions share a button. |
| Per-pad tuning (device index, hot-plug seats) | verify | D448, D450 (#122) | Pad seats keyed by SDL instance id, reserved in a match; hot-plug events reach the input layer. Merged with split-screen 2026-10-01. **Owed:** unplug and replug a pad mid-match. |
| Full-gamepad menu navigation audit, Xbox/PS button glyphs | verify | D471; local note `PAD-MENU-AUDIT.md` | **Audit done 2026-10-01:** a controller-only player is never trapped in the PC menus. **Glyphs: not wanted** (maintainer); instead D471 shows family-specific button **names** (PlayStation/Nintendo) in the help lines and binding column, Xbox text unchanged, plus player-index LEDs (PD-port parity). **Audit G1-G3 fixed by D472** (owner-pad input). **Owed:** a real PS/Nintendo pad check. |
| Mute and auto-pause on focus loss; PNG screenshots to `screenshots/` | parked | D231, PR #56 (closed unmerged), `docs/dev/notes/parked/0001-QoL-*.patch` (local) | Deprioritised by the maintainer on 2026-09-25. F12 today is the dev PPM dump. |
| F10 overlay polish | verify | D472, D335b, #90 | **Landed 2026-10-01 (D472):** overlay pillarboxed to the centred 4:3 card in widescreen (mouse mapped through the same region); EXPERIMENTAL rows in red; unknown `ge007.ini` keys shown as a non-blocking "N UNKNOWN INI KEY(S) - SEE LOG" in the title bar (#90's promise); the overlay is driven by the pad that opened it (audit G1-G3). **Owed:** 2-pad ownership live check and mouse hit-testing at 16:9/21:9 (checklist in D472). |
| Master volume, audio output device select | verify | D470 | **Landed 2026-10-01 (D470):** `Audio.MasterVolume` (0-100, default 100 = byte-identical output; gain applied after the game's music/FX volumes, live) and `Audio.Device` (default = system default; switch on the audio producer thread; missing device or removal falls back to default). Rows at the top of F10 / front-end AUDIO. **Owed:** listening checklist in the D470 finding (master 100 unchanged, slider live, device switch, persistence, unplug, bogus name). |
| Frame rate above 60 (120/144/uncapped with interpolation) | decision | [`dev/UNLOCKED-FPS-PLAN.md`](dev/UNLOCKED-FPS-PLAN.md) | The menu cap is {30, 60} (the ini accepts 0 = uncapped, but the sim ticks at the VI rate). The plan's Phase 0 is resolved by D248 (the default was a port bug; it is a real 60 fps) and its Phase 1 audit and Phase 2 slice scope are written (landed 2026-09-30), but the header still reads parked. Next step is a standing sim/render-rate measurement, not the old 30 fps investigation. Must stay compatible with netplay determinism. |

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
| Deterministic 1964-vs-port comparison harness | open | — | Would replace by-eye/by-ear fidelity checks. Highest-leverage tooling item. |
| Scripted-playthrough harness | open | `GE_INPUTSCRIPT` | Input script + `GE_PCDUMP` + crash-log check, for unattended regression runs. The tool exists; the harness doesn't. |
| Golden baselines | open | `tools_pc/verify.sh`, D117 | bunker1 has been noisy since M-84, so the per-patch gate is unreliable. Re-base it, extend to all 21 levels × both platforms, and finish `GE_DETERM`. |
| Self-hosted CI runner link failure | open | D244 | Needs a `pacman -Syu` on the runner (maintainer-only access). Release tag builds use GitHub runners, so it doesn't block a release. |
| Remove temporary scaffolding before a release | partial | [`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md), D302 | The 2026-09-30 pass removed D207S, D157, D252POOL, D245V, D288, D309, the D318B suite, the D320 repro harness and `scan_op12/13.py`, demoted the D318 watchdog to detect-and-log, and cached the per-frame `getenv` sites. **Left:** dead registry rows (D116, D51, D56, D60-D63, D69*, D71LOG, D85DUMP, D86-D88, D90, D96, D104, D154, D178); closed-finding TEMPs (`GE_D235_*` once D235 closes; **removed 2026-10-01:** `GE_D306C`, `GE_D318T` (batch 2); the whole D243 diagnostic set -- `GE_D243X2`, `GE_D243X4`, `GE_D243M` (+ its `d243mProbeActive`/frame-counter plumbing and all `D243M:` log lines in `bondview2.c`/`chr.c`/`chraction.c`/`chrai.c`/`model.c`/`objecthandler.c`), `GE_D243CAM`, `GE_D243SWIRL`, `GE_D243` (front.c menu trace) (batch 3, `chore/strip-d243-probes`; the shipped fix and the M-183/M-185 clamps are kept); `GE_DYNTEXHASH_OFF`, `GE_FORCEALARM`, `GE_D204_OLD`, `GE_D252`, `GE_RSEED_LV` were already gone); the now-inert sky tile-capture/shift machinery in `sky.c` (`skyPortCaptureTile`, `skyPortPickShift`, `GE_D245_FIXEDSHIFT`); and the M-183/M-185 clamps, which need a full cutscene re-check first. |
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
007 Plus ROM patch as such (#110) · Windows 7 (#98, pending decision).
