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
- The **release gate** (what must be green before pushing / publishing a
  release) is kept as a local working note — `docs/dev/notes/RELEASE-BLOCKERS-v050.md`
  (gitignored, like the other P-pass reports). Keep it current; don't fork a
  second copy of it into the tracked tree.
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
accuracy gaps in §2. 8 is merged on `release/v0.4.1` (3P/4P with real pads
owed). 9 is open (Stage B). **Parity rule (2026-09-30):** every released
platform carries the same gate: a full 21-level sweep plus a campaign pass.

## Order of work (decided 2026-09-30)

Work continues on `release/v0.4.1`, **shipped as v0.5.0 (named 2026-10-03,
cut staged 2026-10-05, maintainer)** — the docs, golden set (both platforms,
63 frames each) and tooling are complete; the P11 squash + tag + push is owed
maintainer consent (push checklist: `dev/notes/PUSH-TASKS-v041.md`). There are
two stages:

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

**Next (decided 2026-10-03, after the Birdman64 comparison; local note `BIRDMAN64-LESSONS.md`):**
1. D441 census: **done 2026-10-03** (§2).
2. Fix the listed sites as mechanical, build-checked batches on a cheap
   worker; the lead reviews every diff (§7). **Batches 1-5 done
   2026-10-03.** Next: the tier-1 sweep over every level with a seeded
   frame compare, then further batches from it.
3. Scripted-playthrough harness: moved to post-1.0 (maintainer
   2026-10-04; revisit with newer models) (§7).

## Known issues

What a player will notice in the current release. **This table is the
single list:** the README links here instead of keeping its own copy. When
an issue is fixed and released, delete its row. When a new player-visible
issue is confirmed, add a row here (and its tracking row in the sections
below).

| Issue | Impact | Workaround | Tracking |
|---|---|---|---|
| PAL and JP ROMs aren't supported in release packages | NTSC-U only | Use an NTSC-U ROM. Both regions convert, build and boot from source (D258, D487, D488; see `docs/building.md`); packaging (per-region exe, CI matrix, converter region check) is still open | D258, #85, §1 |
| Changing Aspect ratio in a level (mostly Fill window to Original or a forced ratio) can briefly glitch the gun / hand model, rarely | Cosmetic, one-off | Change the ratio from the front-end PC Options, or accept it | D509 (frame hold fixed the common case; the rare remainder is logged, not investigated) |
| No macOS or ARM builds | Platform | — | #88, #95, #101, §4 |
| Saves from v0.4.0 and earlier can hold fake unlocks from `All unlocked` | Save data | Not repaired automatically. Since v0.5.0 the option never writes the save; keep a backup of `data/ge007.eep` from before you used it | D442, D387 |
| The first frame of a level takes a little longer while its textures upload | Brief FPS-counter dip | None needed | D475, D480 |
| Far objects almost fully in fog are now hidden as on the N64, except in widescreen, where the wider view can still show a faint distant building edge (e.g. Surface's dish from the start area). 4:3 matches the N64 | Cosmetic | None; higher Draw/LOD distance shows more | D503, §5c |

---

## 1. Feature completeness vs the N64 cartridge

| Item | Status | Refs | Notes |
|---|---|---|---|
| **2–4 player split-screen multiplayer** | verify | #99, D416–D423, D496 | Merged 2026-10-01. D417 Rule-2 approved. **Maintainer playtest 2026-10-03:** 2P and 3P with real Xbox and PS5 pads, handicaps, teams and game modes all good (D496 fixed an uninitialised weapon-text buffer). **Still owed:** 4P with real pads, pads-only mode, MP audio, D422 centred aim live on P2+ (`AimMode = 1`), D428 sway on P2+. |
| **PAL and JP ROM support** | partial | D258, D487, #85 | **2026-10-02: both regions convert, build and boot** (D258 byte-verified against real PAL and JP ROMs); both play Dam with no crash; NTSC-U unchanged. Build: `BUILD_DIR=build-pal ./build-pc.sh pal-final` / `build-jpn ... jpn-final` with `data/ge007.<region>.z64`. D487, D488 fixed (PAL frame height, TV type). **Open:** full runtime passes per region (levels, menus, audio, saves; PAL 50 Hz feel incl. the D427/D451/D486 gates; EU-only text); packaging (per-region exe names, CI matrix, converter region gate, #6). ROMs: `ge-port-reference/1964_GEPD_Edition/ROMs`. #85 needs reopening or a fresh tracker when this ships. |
| Drop-in ROM converter, remaining parts | decision | #6, [`dev/release-dropin-rom-plan.md`](dev/release-dropin-rom-plan.md) | NTSC-U drop-in shipped. Left: per-region output names and re-exec guard (with PAL/JP), native C emitters to replace the PyInstaller converter. |
| Cut content (TCRF weapons/items) | decision | [`dev/CUT-CONTENT-BACKLOG.md`](dev/CUT-CONTENT-BACKLOG.md) | Not part of the shipped game. Tier 0 already works via the debug/cheat menus. Tier 1 (config-toggle grants) is small and port-only. Tier 2/3 (placing props, re-enabling unused MP maps) need game data changes and are out of scope for this repo. |

## 2. Accuracy (behaviour differs from the N64)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Third-person Bond offset residual | partial; intro puppet explained (2026-10-02) | D173, D292 | Mostly fixed. On the level-start puppet the variance is tick/PRNG-dependent idle timing (D13 class, faithful, no sign-off requested); one 1-frame first-tick Y transient is untraced. If the residual is seen elsewhere (cutscene body model), the maintainer should note where. |
| AI awareness follows two non-default render settings | fixed 2026-10-06 (D565: Fog distance floor 100; AI fog cull rescaled to the game's FOV) | D565, D466, D468, D540, D222 | AI review of v0.4.0..v0.5.0 (interpreter, movement/timing, visibility/combat): no regression at shipped settings (Draw 2.0x, Fog 1.0x, FOV 60, native widescreen). Two leaks of player render settings into AI awareness remain: **(1) `Video.FogDistance` below 100** shrinks the far clip (`bgfog.c fogLoadCurrentEnvironment`, clip = FarFog x min(Fog, Draw)). AI on-screen checks require the render `PROPFLAG_ONSCREEN` (`drawdistgameplay.c portPropGameplayOnScreen`), so guards beyond the shortened clip stop counting as on screen; at Draw 1.0x `portD466Active()` is off and sight range (`fogGetScaledFarFogIntensitySquared`) shrinks too. Effect: guards notice Bond / on-screen triggers fire later than the N64. Fix (shipped): `Video.FogDistance` is floored at 100 (`video.c` registration, so 50/75 in an ini clamp to 100 on load; F10 slider `uiMin` 100); 100-800 only extends, which D466 already keeps off gameplay. **(2) FOV other than 60 (`Video.FovScale` != 100, or native widescreen off at a wide aspect)**: the gameplay fog-cull `propobj.c portSub7F054C58Gameplay` uses `getPlayer_c_lodscalez()`, which grows with the rendered FOV (D222), so at a wider FOV far fogged positions drop out of the AI on-screen verdict sooner (narrower: later). Fix: divide out the port FOV factor (as `portFovYScaleFactor`), keeping the game's own zoom FOV changes. Minor at defaults: the D466 verdict reuses the extended pass's room screen bbox (`posIsOnScreen`); at Fog 1.0x the clip is the authored far, so the two passes match. D491 (room-list portal seeds) and D431 (light-fixture hit scoring) were checked: both restore N64 data reads and only move AI toward the N64. |
| Dam ending cutscene | done (2026-10-06) | D526, D552 | Railing/grate transparency faithful (D526); first-shot swivel restored (D552, maintainer-verified). |
| Title-screen attract demo runs too fast; F10 box rescales while it plays | backlogged (after v0.5.0, maintainer 2026-10-04) | D87 (ramrom file struct); timing: D13, D248, D427, D451, D486 | **Reported 2026-10-04 (live pass):** attract plays visibly too fast; not investigated. Lead: it is a ramrom demo replay whose recordings carry a per-frame tick count, which the port's 60 fps / 1-tick pacing may override. Rule out PC causes, then compare with 1964. Same pass: the F10 box briefly rescaled during the demo (not reproduced; candidate fix = overlay reads fast3d's native viewport, parked locally). |
| Sniper rifle: zoom level and speed vs 1964 | verify | #136, D484, D485, D494 | Crouch: **D494** (2026-10-03) makes the PC crouch key crouch/stand freely with the sniper, as GEPD does (superseding D483's freeze); maintainer-verified. Zoom level (D484, 20 -> 7 degrees) and speed (D485, tick-scaled, Rule-2) fixed 2026-10-02. **Owed:** 1964 A/B of zoom level and speed; close #136 after it. |
| Crash on save slots 1/3/4, plus a host BSOD (fltmgr.sys) | decided | #94 | Needs a repro. Two maintainer retest requests (v0.3.0, v0.4.0) got no reply. Likely D299/D301 (save-slot crash, fixed) and D344 (BSOD on exit, fixed). **Decided 2026-10-01: close as stale when the release ships**, pointing at D299/D301/D344; no GitHub activity before then. |
| Authored mip chains not sampled for explicit-LOD textures | post-1.0 | D323 | Distant textures use driver mips instead of Rare's hand-authored chain (heaviest on Statue/Aztec). Parked as a fidelity enhancement. **Decided 2026-10-01:** after 1.0, together with the LOD-fraction approximation as one "real RDP LOD" project. |
| Approximations in the software RSP | post-1.0 | `gfx_pc.cpp` `G_CCMUX_LOD_FRACTION`; D323 | The LOD-fraction CC input is an eyeballed approximation, bundled with D323 as one "real RDP LOD" project after 1.0 (2026-09-30 triage). **WrapFix settled 2026-10-02:** the maintainer checked the Depot building ceilings against 1964 with the default `Video.WrapFix = 0` and they look right, so the default stays off (the opt-in knob remains; RC3/D167). |
| Latent 64-bit ABI hazards | partial | D441, #107, #108; AUDIT-M6 `struct player`/`struct hand` raw offsets (porting-notes §A1) | **State (2026-10-03, D441):** complete census (local `scratch/d441/`; many hits are intentional `(s32)&ANIM_DATA_*` offsets, D34); 17 files widened to `uintptr_t` under `#ifdef PORT`; 35 sign-extension sites fixed; CI ratchet `tools_pc/abi_ratchet.py` (baseline 389); arena windows centralised (`port/include/portaddr.h`); tier-1 high-arena build (`GE_HIGHARENA_BASE=0x90000000`) runs the menu and `-level_09` pixel-identical to default. D478: nothing live on the current layout; the rest would bite only on macOS/ARM/ASLR. **Sweep (2026-10-05):** tier-1 all-level sweep done, 21/21 PASS, no fix batches, ratchet unchanged (389), recipe in the D441 entry. **Owed:** tier 2 (above 4 GiB; u32 slots widened); store-to-u32 vs arithmetic-only tag per site (`portHostToN64` under macOS port_addr); `mema.c` widening (D453); `initanitable.c` heap slots; GE_DETERM wall-clock timers (D117 follow-up, blocks using determinism mode for frame gates). **#108:** widening the `sndPlaySfx` bound disables the guard everywhere; prefer a range check. |
| Port-memory vertex/pool bounds hardening | post-1.0 (decided; not a v0.5.0 blocker) | D545, D549 | D545 (rockets through floor, fixed) left one residual: ~1 dropped rocket in ~60 spawns (Archives, akimbo), consistent with the room-pool hypothesis (`bgBuildRoomVtxBounds` allocation failing leaves no bounds) but unproven; D549 (same stack-overflow class in grenade/knife/object throws) is fixed. Both shipped. Proposed hardening: a port-memory bounds table validating vtx/pool pointers against the known arena windows. |

## 3. Verification debt (fixed; a live check is owed)

A single campaign pass with these on the checklist would clear most of them.

| Item | Refs | Check |
|---|---|---|
| Presets: Original N64 / Port defaults | D440 | Press both rows in F10 and in the front-end PC Options with a mouse and a pad; draw distance applies from the next level load. `GE_OPTIONTREEPROBE` already fails on the base commit (separate look). |
| Probe strip (2026-09-30) | D245, D302, D318 | Frigate or Dam water/sky A/B against the previous build (the s16 sky path was removed); one Facility playthrough to confirm the D318 watchdog stays silent. |
| Infra fixes | D179, D515, build-pc.sh, crash.c, CI | 2026-09-30 re-exec no-op (PATHEXT) fixed; 2026-10-04 empty-arch-tag bug (D515) fixed headless (details in AGENTS.md) — confirm on your next fresh-dir build. **Owed:** `./build-pc.sh` from a genuine MSYS2 login shell; the next CI run (pinned actions, workflow input check). `GE_CRASHTEST=6` shows resolved frames. The romdata `VirtualAlloc` failure path now fails boot instead of falling back to the heap copy: confirm you want that. |
| Light-fixture hit-type reads (impact sound + sparks) | D434, #119 | Shoot fixtures on Bunker and Caverns. |
| Dam/Caverns water animation | D465, #127 | Fixed 2026-10-01 (16-byte `Gfx` raw-index write). Maintainer: still "slightly faster" than 1964 on Dam; measured rate equals N64 within 0.1%. Optional objective check: time ten shimmer cycles on both (expect ~26 s). |
| End credits after Cradle | #129 | Did not reproduce on `release/v0.4.1` 2026-10-01 (maintainer finished Cradle on Agent, credits played, clean exit). Close #129 as not reproducing when the release ships. |
| v0.4.1 batch (unreleased; fixes live on `release/v0.4.1`) | D424–D432; #114 fire rate, #115 tank-crush audio loop, #116 tank movement, #117 sway, #118 GL recoil, #119 light fixtures | All user-verified against GEPD/1964 on 2026-09-29. Close the issues when it ships. |
| FOV-scale edge culling | D222 | Max FOV, pan across NPCs at the screen edge. Also audit the FOV/aspect terms in `camIsPosInScreen` and `c_lodscalez` for the Surface dish (D503 residual). Maintainer decision owed: expected widescreen behaviour vs fix. |
| Draw/LOD 800% | D503 | Draw/LOD distance ceiling raised 400% → 800% (2026-10-04). Owed: at 600-800, check far-field z-fighting on Dam (tunnel), Surface and Jungle; on Aztec or Streets, grep the log for the D500 vtx-pool budget line. If z-fighting shows, lower the ceiling to 600 rather than touching depth precision. |
| Water pulse, Surface 2 | D229 | Frigate is verified. Only the Surface 2 by-eye check is owed. |
| Linux save/config path when launched outside the game dir | D256 | Save-flow check on Linux. |
| Settings/F10 batch | D356, D358, D361, D363, D374, D406 | Live accept of each item in the F10 overlay. (D377–D379 residuals were waived.) |
| Controller feel | CONTROLLER-INPUT-PLAN Wave A, D204 | Per-stick deadzone/smoothing feel on a pad and on Deck; D204 tempo by ear; `Input.RumbleScale=0` silences rumble. |
| Widescreen HUD at 21:9 | D334/D335 | One ultrawide HUD playthrough. |
| Older "owed" notes | D371, D372 | **Bookkeeping done 2026-10-01:** D150, D177, D264 ticked off against the 2026-09-28 campaign sign-off; D369 accepted in use. Still owed: D371 (GEPD key-layout preset + reload/crouch bindings: select the preset in F10 and play a level). (D372 low-end preset: no further effort; low-end work is real optimizations, see §4.) |

## 4. Platform

| Item | Status | Refs | Notes |
|---|---|---|---|
| macOS (Intel + Apple Silicon) / ARM64 Linux | open (target 0.9.0/1.0, maintainer 2026-10-02) | PRs #88, #95; #101 | Outside drafts, parked until after 1.0. **Address model decided (2026-09-30): port_addr (#95)**, a `portN64ToHost`/`PORT_N64PTR` chokepoint with a `PORT_ADDR_STRICT` validator. The `macos` integration branch is on origin with #88 (merged), #132 and #95 (open); nothing merges to `main` before the post-1.0 pass. **Open against `macos`:** #131 (self-hosted sweep scaffold, never run on a Mac; needs #95 first). **On the next rebase onto `main`:** take #131's `selfhosted.yml`; drop #95's `crouched_rifle` hunk (our #120/D445 is correct); relabel #95's D29x/D405-D421 labels. Needs the §2 ABI-hazard sweep; the maintainer tests firsthand before merging. |
| Platform parity gate (Linux co-equal with Windows) | open | — | **Decided 2026-09-30:** yes. Every released platform carries the same gate: a 21-level sweep plus a campaign pass. Needs the golden baselines on both platforms first (§7). The last known Linux crash (D190) was closed as not-a-bug. |
| Low-end GPUs (~30 fps on Celeron/HD 400) | fixed, verify on N3060 class | D339, D481, D482, #92 | **D481 (2026-10-02):** fast3d ran inline on the scheduler thread, delaying retrace delivery so the tick gate skipped frames; the render worker took a Lenovo X220 (HD 3000) from 44-46 to 60.0 fps. **D482:** Atom/Celeron-class GPUs (HD 400-605, UHD 600/605, software renderers) get DrawDistance/LodDistance 100 and MSAA 1 once, where still at defaults. **D473:** uncached per-batch getenv fixed. Direction: real optimizations, not a preset. **Next only if N3060-class hardware still drops:** one `GE_PERFSTAT=1` run (`gpu=` vs `run=`), then a lighter renderer-keyed default (GPU) or fast3d per-batch cuts (CPU; local `PERF-HOTSPOTS.md`). |
| Statue/Cradle hitching (maintainer, 2026-09-30) | parked (not player-visible, 2026-10-02) | `GE_PERFSTAT` spike counters | **Diagnosed (D475, D477, D480):** no sustained load — the ~33 ms frames are the frame limiter skipping a retrace after a late gfx task (`boss.c`, D13-class timing); CPU bursts were CI-palette re-imports (ruled out, D477) and in-play shader compiles (fixed, D480). **Parked 2026-10-02:** a play session with the timeline probe felt no hitching. Reopen only if a visible hitch is reported (lever: pre-warm at level load). |
| Steam Deck preset skipped when first launched from Desktop Mode | verify | D283 | Fixed on `release/v0.4.1`: the Deck is detected by DMI hardware id (Valve Jupiter/Galileo; `STEAMOS` as a second signal) and the preset applies once per ini, only while Fullscreen/Maximized are untouched. Only a Windows build and a Linux `-fsyntax-only` check ran. **Owed: one run on a real Deck** (Desktop-Mode-created ini, then Game Mode; expect fullscreen 1280x800 and the log line `video: Steam Deck (DMI Valve ...)`). The Known-issues row goes when this ships. |
| Windows 7 | open | #98 | **Re-checked 2026-10-02: likely feasible around 1.0.** The exe imports no Windows 8+ functions and the bundled SDL2 2.32.10 still supports Windows 7. The real blocker is the first-run converter `ge007-convert.exe` (PyInstaller, Python 3.9+): the native C converter (#6 Part B) removes it, or a separate Python 3.8 build. Work: one Windows 7 VM test, then manual per-release checks (CI cannot test it). Supersedes the earlier 'won't-fix' call. |

## 5. Modern-remaster layer (port features beyond the N64)

**The bar:** feature parity with a Nightdive-style remaster and with
GE+/Dab's mod where it's practical. Shipped so far: native widescreen and
ultrawide, FOV, MSAA/anisotropic/texture-filter options (including N64
3-point), draw and LOD distance, VSync and a frame cap, show-FPS,
keyboard/mouse rebinding with a GEPD-style layout, pad deadzone,
sensitivity, smoothing, southpaw and triggers, rumble, crosshair and HUD
customisation, HUD scale, music/FX volume, no-hit-flash, SkipIntro,
All unlocked (see D387, D442), and the F10 overlay.

The modern defaults (native widescreen, overscan crop, 250% draw/LOD
distance, bilinear filtering) are **deliberate**. They are not a fidelity
defect.

Background research lives in the maintainer's local notes (gitignored):
`GEPORT-REFERENCE-DEEPDIVE.md`, `MODERN-PORT-FEATURES-BACKLOG.md`,
`MODERN-OPTIONS-PLAN.md` and `PD-LEGACY-SURVEY.md` under `docs/dev/notes/`.

### 5a. Before 1.0

| Item | Status | Refs | Notes |
|---|---|---|---|
| Log file (next release) | open | maintainer 2026-10-06 | Write `ge007.log` next to the ini (console output is lost on a normal launch). Crosshair pointer done in v0.5.0 (D555). |
| F10 / PC Options menu: PD-style dialog refactor + watch-consolidation review | verify | D504-D512, `MENU-PD-ALIGNMENT-PLAN.md` (maintainer's local notes) | **Built 2026-10-04 (D504); follow-ups merged:** D505/D506/D507 (wording, real-unit sliders, descriptions); D508 forced aspect ratios (16:9 / 21:9); D509 2-frame hold after an aspect change (rare gun glitch = known issue); D510 overlay + FPS counter same size front end vs level; D512 `Game.HudScale` scales the F10 overlay. **Owed (live pass, `docs/dev/notes/MENU-LIVE-CHECKLIST.md`, launches A/B/C):** look-and-feel vs PD; mouse, keyboard and pad in a level and on the front end; HudScale 75/100/150; 21:9; display-mode round trip across a restart; split-screen 2P with the overlay open. **Gaps:** background blur (post-1.0, §5c); per-seat control scheme is #139 / D513. Watch consolidation: not cheaply feasible (parked as T5). |
| Defaults/presets consolidation: one `Display mode` switch | verify | D504, D440, `MENU-PD-ALIGNMENT-PLAN.md` §5 (local notes) | **Landed 2026-10-04 (D504):** the derived `__DisplayMode` row shows Modern / Original N64 / Custom (no ini key); F10 dropdown + left/right stepping, front end steps too; one reset row per dialog covering exactly its keys; editing a non-bundled key does not flip Display mode. **Decided 2026-10-03: tri-state**, flipping to Custom when the user edits a bundled key. **Owed (live):** display-mode round trip across a restart; read of the regrouped pages and wording (MENU-LIVE-CHECKLIST.md A1/A2/A4). |
| Control style Original shows the game's own N64 styles | verify | #139 follow-up, D516, D517 | **Landed 2026-10-04 (D516, merged @ 91595597):** "Controller style" row (Original only; the game's own 1.1-2.4 per seat, saved to the Bond file; front end reads "(in a mission)"); D498 presets hidden in Original. **D517:** hold-repeat on any Bond row dropped fps (EEPROM rewrite per detent); now staged like sliders. Live passes 2026-10-04 (maintainer): single player + split-screen 2P with different styles per seat. **Owed:** the 2.x second-pad pass (real second controller). |
| Pad layout preset per player (setting P2's no longer moves P1) | verify | D518, D498 follow-up | **Landed 2026-10-04 (D518):** `Input.PadPreset` kept as seat 1's key + new `Input.PadPreset2/3/4`; the F10/front-end preset row follows the Select-player seat. Headless GE_PADMAPTEST PASS; **live passes 2026-10-04** (P2 change leaves P1 alone; split-screen: each seat ran its own preset). |
| Control scheme selectable: Ext / Original control style | verify | #139, D513, D498, D166 | **Landed 2026-10-04 (D513, merged into `release/v0.4.1`).** `Input.ControlScheme` (row "Control style", Ext default / Original) on the Controller page, both UIs. Original leaves the game's own style alone; D498 presets set layout and stick swap only in Original; 2.x runs faithfully; global in v1, per seat with the Select Player work. **Owed:** pad pass in both modes, watch-selected 1.1-1.4, split-screen 2P with different styles, 2.x with a second pad. |
| Outside PRs #107–#109, #122–#124 | decided | #122 hot-plugged pads never reopen, #123 persistent crosshair (`gunfire.c`), #124 compact health/armour bars (`bondview2.c`); #107/#108/#109 (italoarruda) | **Decided 2026-10-01:** **#120/#121/#122** merged locally (#122 = D450); close on push. **#123**: merged 2026-10-06 into v0.5.0 (D436; presets set it off, tip, Rule-2 signed off; by-eye hipfire / sniper zoom / 2P owed). **#124**: deferred (ignores HudScale, overlaps status text, ultrawide anchoring, split-screen untested). **#133**: duplicates D472; close as superseded on push. **#109** (pad rebinding): decline as-is; supersede with our pad presets (§5a), crediting the author (Co-authored-by). **#107**: folded into D441; close on push. **#108**: belongs with `macos`; ask for a range check. |
| Gamepad presets: duplicate-binding warning, Xbox stick behaviours | open (after v0.5.0; documented in the README) | D469, D498, D518, D394 | D469 (pad action table, Jinx 1.1 default, Custom per-seat bindings), **D498** (Xbox 1.2 Christmas / 1.3 Frost / 1.4 Elektra) and the per-seat preset (**D518**) are done and maintainer-verified with pads. Not done (maintainer 2026-10-04: skip in game for v0.5.0, document only): a warning when two actions share a button; the Xbox "left stick aims the crosshair" and "D-pad copies the left stick" behaviours (D394). |
| Pad button names on a Nintendo pad | verify | D471; local note `PAD-MENU-AUDIT.md` | Audit done 2026-10-01 (no controller-only trap; D472 fixed G1-G3). D471 family button names + player LEDs: **PS5 DualSense verified 2026-10-03.** **Owed:** a Nintendo (Switch) pad check; the maintainer has no such pad, so this waits for one or for a user report. |
| Mute and auto-pause on focus loss; PNG screenshots to `screenshots/` | parked | D231, PR #56 (closed unmerged), `docs/dev/notes/parked/0001-QoL-*.patch` (local) | Deprioritised by the maintainer on 2026-09-25. F12 today is the dev PPM dump. |
| Frame rate above 60 (120/144/uncapped with interpolation) | decision | [`dev/UNLOCKED-FPS-PLAN.md`](dev/UNLOCKED-FPS-PLAN.md) | The menu cap is {30, 60} (the ini accepts 0 = uncapped, but the sim ticks at the VI rate). The plan's Phase 0 is resolved by D248 (the default was a port bug; it is a real 60 fps) and its Phase 1 audit and Phase 2 slice scope are written (landed 2026-09-30), but the header still reads parked. Next step is a standing sim/render-rate measurement, not the old 30 fps investigation. Must stay compatible with netplay determinism. |
| PD Extended Options parity: settings that need new dev work | done (reviewed 2026-10-04; two rows deferred) | `MENU-PARITY-SONNET-PLAN.md` §3 (local notes); pd_port `port/src/optionsmenu.c` @ 32a1cb9 | **Reviewed 2026-10-04 (maintainer decisions; local notes).** **Done:** D511 fullscreen mode, Center window, crosshair opacity and color by health (live check owed, MENU-LIVE-CHECKLIST.md A4/A5/B3). **Watch Screen size/Ratio:** stay on the watch, no PC rows; the game saves watch settings on watch close (D349 correction) and they don't fight Aspect ratio (README). **Deferred past v0.5.0:** mouse lock mode (Auto/Always/Off; aim lock itself = `Input.AimMode`, D337); per-seat controller device select (needs a stable pad id; MP input layer). **Closed:** edge deadzone = `Input.AimRange` (D338); crosshair sway N/A (GE has none; gun sway is D428); use key reloads, no toggle (Ext keeps D378's GEPD split, Control style Original gives the N64 behaviour); per-seat options/binds (pad binds already per seat, D469/D518; keyboard/mouse is one device); no Detail textures toggle (`Video.DetailBaseTile` is the D236 fix); N/A confirmed: HUD centering (D335b), glare, overexposure, GE64 muzzle flashes, MP death music, radial menu speed, language filter, crosshair speed. **Other rows:** analog movement/stick scale/crouch → #139, T7; tickrate/framerate limit → frame rate above 60. |
| One PC options UI: front-end "PC Options" opens the F10 overlay over the folder graphic | verify | D519, D343, D504, D510, D512 | **Landed 2026-10-04 (D519):** the file-select "PC Options" entry opens the F10 overlay over file select; the `MENU_PC_OPTIONS` screen and four old probes are gone; Bond-file rows and the watch chooser are keyed to file select. Headless checks pass. **Owed:** by-eye mouse, keyboard and pad pass on file select; profile chooser + Controller style with a real profile; footer text overlaps the Copy/Erase bar. |
| Emulator saves load directly in the game (no tool for import) | verify | D514, D492; `port/src/eepimport.c` called from `geEepromLoad`; D297 `legacycrc.c` same pattern | **Landed 2026-10-04 (D514).** Copying a 1964/Project64 `.eep` in as `data/ge007.eep` works: on first launch each region that validates in emulator format and not port format is converted in place (same rules as `eep_convert.py`); regions invalid in both are left to the game's wipe; port saves untouched; the original is backed up to `ge007.eep.emulator.bak`. Export stays with `eep_convert.py`. Headless verify 6/6 byte-identical to the reference. **Owed:** by-eye mission select after importing a real 1964 save. |
| DexDrive / GameFAQs `.n64` saves | idea (easy, post-1.0) | D514; sample `scratch/eepinv/gamefaqs_dexdrive_2001.eep` (local) | **Noted 2026-10-04.** Old GameFAQs GoldenEye saves are DexDrive Controller Pak dumps (`123-456-STD` header, then a 32 KB pak) whose note (game code `3BADD1E5`, 2 pages) holds the 512-byte cartridge EEPROM. Extracting it and zero-padding to 2048 bytes gives a valid emulator-format save (TenebrusoM sample: all 6 CRCs OK). Support = walk the note table, pull the note, pad, hand to the D514 importer (or an `eep_convert.py dexdrive-to-pc` subcommand). Not tested in game. |
| Title bar and window icon (no FPS counter; static "GoldenEye 007" title + GE-crosshair window icon) | done (v0.5.0 polish) | maintainer request 2026-10-17; D550; `port/src/video.c`, `port/fast3d/gfx_sdl2.cpp`, `cmake/icon/ge007.rc`; icon art = `docs/img/icon/ge007-icon.png` (`tools_pc/make_icon.py`) | **DONE 2026-10-17 (D550):** static title bar (live FPS stays on F10), `SDL_SetWindowIcon` from a generated 32x32 header, exe icon via `ge007.rc`. Original generated mark, no game imagery. Build-verified; maintainer live window/taskbar check owed. |
| Opt-in update check (`Game.CheckUpdates`, off by default) | verify | D551; `port/src/updatecheck.c` | **Landed 2026-10-06:** Game-page toggle; one HTTPS GET per launch to `/releases/latest` (no pre-releases); Windows loads `winhttp.dll` at runtime only. Live-verified (Win/Linux/Deck), security-reviewed. **Follow-up PR (2026-10-06):** (1) the Linux/Deck `curl` path reads into a 64 KB buffer and treats `curl`'s exit code as failure, so a `/releases/latest` response over 64 KB (long notes, many assets) silently gives no result even though `tag_name` is in the first ~1.5 KB; raise the cap and accept a full buffer (v0.4.0's response is 27 KB, so v0.5.0 is safe). (2) Validation matrix: offline harness for `versionNewer`/`extractTag` edge cases; throwaway builds with `GE007_VERSION` set to 0.3.0/0.4.0 against the live endpoint, Windows + Deck; a pre-release never prompts; after a publish, an older-version build shows the F10 row and opens the releases page; network-off and setting-off runs. |

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
| Near-full fog reveal (D503): far objects about 97% fogged show as faint silhouettes with straight room-clip edges on 32-bit output (e.g. Surface's dish) | verify | **Snap landed 2026-10-04** (cutoff 247, not derived from measured vertex fog); dish residual = (C) widescreen frustum, moved to the §3 D222 row; by-eye pass across levels owed. **Decided 2026-10-03 (maintainer): option (a)**, snap fog at or above ~97% to full fog (port-only, fast3d vertex fog). Option (b) (N64 16-bit colour/dither output) stays a candidate for the Original N64 bundle. Measurements: findings D503. |
| Render scale / supersampling, integer scaling, "authentic 320×240" mode | post-1.0 | **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Dynamic/coloured lighting, dynamic shadows, water reflections | post-1.0 | Large. Consult the PD port first. #105 is about the fork's version of reflections, not ours. **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Path-traced render mode (opt-in, RTGL1) | post-1.0 | Experimental, for fun (maintainer, 2026-10-01): **deprioritised to after 1.0**. Plan in the maintainer's local notes: vendor MIT RTGL1 behind `port/rt/`, default OFF; fast3d stays the default backend and the fidelity ground truth; Phase 0 is a toolchain spike (RTGL1 under MSYS2/MINGW64). Nothing starts before 1.0. |
| HD texture packs: loader + texnum dumper; model packs | post-1.0 | #100. There is a staged design. Step 1 (texture identity registry) touches `src/game`, so it needs Rule-2 framing. **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| Animation smoothing (keyframe slerp) | post-1.0 | **Decided 2026-10-01:** after 1.0; our own opt-in implementation (default OFF; the Original N64 preset restores the N64 look); thepont's fork is reference only. |
| F10 background blur + GUI-only texture filtering | post-1.0 | **Backlogged 2026-10-04 (maintainer): visual nice-to-have after 1.0.** Design done (local `MENU-BLUR-GUIFILTER-DESIGN.md`, ready Sonnet prompts in its §4). Blur: fast3d already decodes PD's framebuffer EXT opcodes and has the 16-tap kernel; only an emitter is missing (`port/fast3d/gbiex.h`); capture once on open (live capture too costly on HD 3000, D481). GUI filter: `Video.GuiTextNearest` point-samples texrects only. Port-only, default off. |

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
| 1964-vs-port comparison harness | partial | `tools_pc/abpad/` | One millisecond timeline drives 1964 (virtual XInput pad) and the port (`GE_INPUTSCRIPT_MS`); both windows captured on one clock with side-by-side sheets, verified through intro skip, file select and folder open. Not frame-identical (1964 has no seed pin or frame dump). **Left:** in-level timelines (e.g. the D486 turret check); the two sides use different save files. |
| Scripted-playthrough harness | post-1.0 (maintainer 2026-10-04: not a good use of current LLMs, revisit with newer models) | `GE_INPUTSCRIPT`, D117 | Input script + `GE_PCDUMP` + crash-log check for unattended regression runs. Levers: Invincibility + Invisibility cheats, closed-loop waypoint steering, pinned seed. Spike: one Dam route x5, measure end-position spread, then go/no-go. |
| Cheap worker for mechanical, build-checked tasks | open | `delegate-local` MCP; memory of past Qwen issues | Birdman64's fast worker model wrote 44% of its commits while the lead planned and reviewed. Ours: pattern-applied edits (D441 fix batches, scaffolding strips, small tools), each gated by a clean build + a specific check + the lead's diff review, in the worker's own worktree. Local Qwen for edit-only tasks (it fabricates on reading tasks), or a hosted fast model via `delegate_to_provider` (the maintainer's cost call). |
| Golden baselines | partial | `tools_pc/verify.sh`, `tools_pc/golden/README.md`, D117/D522/D529/D531 | **2026-10-06: all three sets (win/linux/deck) re-based for D543/D546**, A/B-confirmed; history in the golden README. **Windows gate GREEN (2026-10-05 re-base):** 21/21 PASS honest sequential. The recipe pins the save CONTENT (D529: `verify.sh` installs the canonical in-tree save `tools_pc/golden/ge007.eep` per level) and per-level windows — `900-1500:300` on 14 levels, `1000-1400:200` on Surface 2/Streets/Depot/Cradle/Frigate, `1500-1700:100` on Dam — because the 900 stem sits inside the wall-clock-paced intro flyby (D117; dam measured 1.6–7.2 % run to run). No tol limit raised. **Linux set (D525 → D530 → D531):** D530's re-round ran under the box-local save (`capture_p7.sh`'s re-pin overwrote D529's canonical install); D531 fixed the tool and re-captured all 21 levels under the canonical eep (47/63 frames moved). **Cross-platform spread recomputed 2026-10-05** from the committed pairs (`framediff.py` win-vs-linux at the stems, tol 2): exact **0.380–5.120 %** on the 20 non-Cuba levels (silo's 900 stem is the outlier: cell mean 34.3, phash 10), structural **21/21 clean**, max phash 40 (cradle, at threshold); Cuba 1.357–12.663 % is the structural-tier-only level. **Pixel gate green on all three, final v0.5.0 build (2026-10-06):** win 21/21; linux 21/21 on the X220 (`DISPLAY=:1` makes the GL probe see the real GPU; one Jungle flyby-jitter miss passed twice solo); deck 21/21 (needs `XAUTHORITY` for the probe over ssh). |
| Flicker scan (A/B/A frame alternation) | parked (2026-10-03: tooling, nothing blocked on it; local spec `scratch/birdman64-rows-investigation.md` §4) | `GE_PCDUMP`, `GE_INPUTSCRIPT` | Dump **every** frame of a scripted run at low resolution and flag frame n when it differs from n-1 but matches n-2. For any old-vs-new pixel diff outside the golden gate, run a same-commit control first. Reference: Birdman64 `tools/flicker_scan.py`. |
| Release build-provenance attestation | verify | `.github/workflows/ci.yml` release job; `.github/SECURITY.md`; README Status section | **Implemented 2026-10-04:** SHA-pinned `actions/attest-build-provenance` attests `dist/*.zip[.sha256]` + `dist/*.tar.gz[.sha256]` in the tag-only release job (`id-token: write` + `attestations: write` on that job only). Backs SECURITY.md's "unsigned, but reproducible" stance with `gh attestation verify <file> --repo jkdansereau/goldeneye-pc-port`; documented in SECURITY.md and the README. **Owed:** the v0.5.0 tag build is the first real run; confirm the step passes and verification succeeds on a downloaded asset, then delete this row. |
| Self-hosted CI runner link failure | open | D244 | Needs a `pacman -Syu` on the runner (maintainer-only access). Release tag builds use GitHub runners, so it doesn't block a release. |
| Remove temporary scaffolding before a release | partial | [`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md), D302 | Passes 2026-09-30 to 2026-10-03 removed the closed-finding probe suites (registry in `GE-ENV-PROBES.md`, `gen_env_probes.py --check` passing); TEMP probes `GE_TESTSTYLE` / `GE_D516` removed 2026-10-04. **Left:** D104 (kept while D294 is in verify); the M-183/M-185 clamps were **deleted 2026-10-04** (`879f879a`, `model.c` pure decomp again; the M-189 root fix stays) and the cutscene re-check was covered by the 2026-10-05 Deck campaign playthrough on rc1 (level intros, endings incl. Facility and Dam, credits). |
| P12 U5 review of `tools_pc/**` (release-diff, B10) | partial | D531/D532/D533 | Reviewed 2026-10-05 (the unit that owed the P7 merge). **Blocker found + fixed (D531):** `capture_p7.sh`'s per-level re-pin restored the local save (see Golden baselines). **Should-fixes applied as D532 (tooling-only):** `verify.sh`'s `pin_ini_640x480` fails closed like `pin_eep`; `GOLDEN_SEED` hard-pinned with a warning for a stray `GE_RSEED`; `audiodebug.ps1`'s dead `-AB`/`-Old` switches removed. Box pixel verify done 2026-10-06 (see Golden baselines). |
| Findings log and doc size | partial | `tools_pc/docs_budget.py` | Budget checker (per-file and tier-1 token budgets; `--toc FILE`). `findings.md` is still about 2 MB (~500k tokens), so agents must keep using the index. Nothing blocks; trim only when a budget check fails. |
| Branch and PR housekeeping | open | — | 30 landed local branches pruned 2026-09-30 (tips in `prune-branches-2026-09-30.txt`, notes archive). **On hold by the maintainer (do not prune yet):** probe-only `d243-verify-local`, `investigate/d243-cutscene-race`, `park/d236-probes-m139`. Local `main` and `release/v0.4.1` are deliberately not pushed. PR #62 (mouse dt-decouple) stays held until mouse feel is fundamentally better. |
| Docs site: Web 1.0 phase 2 | decision | — | Phase 1 shipped. Phase 2 (centred serif page) is plan-only. |
| Docs site: press/SEO pass (2026-10-05) | partial | `docs/` site pages | Landed on the pure site pages + layout: real `<h1>` masthead, `color-scheme`/`theme-color`/`max-image-preview`/`rel=me`, ink-mute >=4.5:1, focus-visible + selection, 15px body, mobile padding; `date`/`modified_time` front matter on the pure pages. Home-page additions (pull-quote, Overview fact table, press section) **removed on review 2026-10-05** as duplicates. The five short topic notes (the-software-rsp, the-asset-pipeline, framerate-and-pacing, input-and-aim, saves) were dropped from the published set (files stay as working material). **Open:** the same front matter on the *shared* docs is deliberately untouched (authoritative working docs, not edited for site polish). |
| Agentic-development deep dive (doc) | queued — not yet (2026-10-05) | `docs/dev/agentic-development.md` | Write-up owed for the public case study: per-agent commit/work estimates (manual attribution pass over the session log, incl. D350-style commit-date divergences), a code-quality audit of the trial models (GPT-6 (Sol) et al.), and process feedback / findings. Deferred by the maintainer; nothing here blocks. |
| Docs site: split Security & fidelity -> two pages + v0.5.0 update pass (2026-10-05) | done (local, unpushed) | `docs/security.md`, `docs/fidelity.md` (was `security-and-fidelity-status.md`) | Split on review (the homepage pointer sat near Known issues and read as if there were security *issues*); both pages cross-linked and linked from the hub and index.md's Download section. Update pass 2026-10-05 against the `release/v0.4.1` tree: no network includes/symbols/HTTP literals in compiled `src/`+`port/`; no registry or library-loading calls; converter imports no network module; bundle manifests re-checked (allowlist + ROM/oversized-blob hard-fail); region macros re-compared vs the Makefile (all three regions); library classification re-checked (CMake globs libultra+libultrare audio, gu, `io/vitbl.c` only). Full game-logic audit not re-run; recent `src/game` edits are finding-gated. |
| Docs site: final sanity sweep + anchor fixes (2026-10-05) | done (local, unpushed) | `docs/porting-notes.md` (3 TOC hrefs), `docs/404.html` (permalink) | Audit of the 10-page published set (one h1 per page, no heading skips, in-page anchors resolve, unique titles/descriptions, og:image/canonical, JSON-LD, no local paths, served == built). Three `porting-notes.md` TOC hrefs had a double hyphen where kramdown emits one: corrected at source against the live ids. `docs/404.html` got `permalink: /404.html`. Preview-builder slug rule fixed to kramdown's. |

## Community forks (not ours; reference only)

| Fork | What it has | Relevance |
|---|---|---|
| thepont/goldeneye-pc-port `feature/graphics-final-stack`, `fix/original-water-fidelity`, `feature/coop` | ORIGINAL/ENHANCED/REMASTER render mode, lighting, post-FX, bump/shadow/DoF, water reflections, vector HUD font, 2P co-op | Source of #105. Heavy `src/game` edits. **Their D323–D326 and "D329" labels clash with ours**, so relabel anything ported. |
| birdturtle `feature/local-mp-input`, `simulants` | Local MP restore; first MP bot | Cross-check for #99 and #111. |
| kaziema `vita` | PS Vita port | Platform reference. |
| mattymattmattmatt/goldeneye-pcvr-port `vr/stereo-headaim` | VR stereo + head aim | Platform reference. |

## Decisions owed (maintainer)

Decided 2026-09-30 and 2026-10-01 (all moved into the rows above): PAL/JP in Stage B; `All unlocked` as a RAM override; D357 approved with changes; platform parity gate; feature PRs get a design review first; port_addr for #88/#95; D417 approved; the gameplay-visibility policy (§2); §5b, §5c and §6 post-1.0 and opt-in; D433 code audit; D323 post-1.0; #94 closes as stale on release.

Nothing else owed right now (outside PRs decided 2026-10-01, see §5a).

## Not planned

Online (non-LAN) multiplayer, matchmaking, leaderboards, anti-cheat,
cross-play · Steamworks, achievements, cloud saves · HDR, frame
generation, stereo 3D (ray tracing only as the opt-in post-1.0 experiment
in §5c) · remake-scope assets (new models, music or voice) · new movement
mechanics (jump, roll, melee) · the 007 Plus ROM patch as such (#110) · a
per-user save folder (decided 2026-10-03: saves stay next to the game,
keeping the security doc's "never writes to `%APPDATA%`" promise) · a
first-launch controls card and controller toasts (decided 2026-10-03: new
UI for little gain; F10 is documented in both READMEs).
