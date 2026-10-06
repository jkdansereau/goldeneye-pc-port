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

Where things stand (2026-09-29): 1–7 hold for NTSC-U, with the small
accuracy gaps listed in §2. 8 and 9 are open.

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
| Multiplayer isn't available yet | Missing feature | — | #99, §1 |
| PAL and JP ROMs aren't supported | NTSC-U only | Use an NTSC-U ROM | D258, §1 |
| `All unlocked` may already have written fake unlocks into a save from an older build | Save corruption | **Fixed on `release/v0.4.1` for new saves** (D387). Back up `data/ge007.eep` before first use; fakes an older build wrote are not repaired; cheats earned while ON are not kept. Delete this row when it ships. | D387, §5a |
| `Game.SkipIntro` also skips the post-mission failure dossier | Minor | Leave SkipIntro off | D408, §2 |
| F10 overlay stretches in native widescreen (menus pillarbox correctly) | Cosmetic | — | D335b, §5a |
| Rareware logo shows a subtle texture-filtering artifact | Cosmetic | — | D75, §2 |
| Steam Deck: a first launch in Desktop Mode skips the Deck preset | Settings | **Fixed on `release/v0.4.1`** (real-Deck check owed); delete this row when it ships. Until then: first launch in Game Mode, or delete `ge007.ini` and relaunch | D283, §4 |
| ~30 fps on very low-end GPUs (e.g. Intel HD 400) | Performance | Set `Video.LowEndMode=1` in `ge007.ini` | D339, #92, §4 |
| No macOS or ARM builds | Platform | — | #88, #95, #101, §4 |
| Controller buttons can't be rebound in-game (keyboard/mouse can) | Missing feature | — | #109, §5a |

---

## 1. Feature completeness vs the N64 cartridge

| Item | Status | Refs | Notes |
|---|---|---|---|
| **2–4 player split-screen multiplayer** | parked | #99, D416–D423, branch `feat/mp-splitscreen-99` | The largest missing N64 feature. 2P has been live-tested (M&K + pad, respawn, 16:9 HUD). Before merge: Rule-2 review of `PROP__PORT_SIGNED` (D417) and a golden gate run on the maintainer's console. Open on the branch: D420 (stats screen shows a large number), D421 (gadget cycling off; needs a Rule-2 game-thread hook), D422 (no centred aim for P2+), D423 (black arrowhead in P2 view), and D428's sway accumulator is player-0-only. Untested: 3P/4P, pads-only mode, pause/watch/F10 during a match, MP audio, most stages, and the full N64 MP setup screens (scenarios, handicaps, teams). Needs per-pad seats (§5). An outside restore exists for reference: birdturtle `feature/local-mp-input`. |
| **PAL and JP ROM support** | open | D258, D75 (pal/jpn sidecar regen), #85 | **Scheduled for Stage B (decided 2026-09-30):** done just before 1.0 so that region-specific issues can be unwound in isolation. Conversion is broken at the source-data level (filelist naming). A repair path has been verified but not applied. Needs PAL/JP ROMs to test. #85 was closed as "retired for now", not won't-do, so public users currently have no tracker. Reopen it or file a fresh one when work starts. |
| Drop-in ROM converter, remaining parts | decision | #6, [`dev/release-dropin-rom-plan.md`](dev/release-dropin-rom-plan.md) | NTSC-U drop-in shipped. Left: per-region output names and re-exec guard (with PAL/JP), native C emitters to replace the PyInstaller converter. |
| Cut content (TCRF weapons/items) | decision | [`dev/CUT-CONTENT-BACKLOG.md`](dev/CUT-CONTENT-BACKLOG.md) | Not part of the shipped game. Tier 0 already works via the debug/cheat menus. Tier 1 (config-toggle grants) is small and port-only. Tier 2/3 (placing props, re-enabling unused MP maps) need game data changes and are out of scope for this repo. |

## 2. Accuracy (behaviour differs from the N64)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Auto-fire gunshot sound slightly differs from 1964/GEPD | decision | D433, #114 | Cadence matches the N64 rules exactly. The residual is subtle differences in the click/empty-magazine sounds between shots during continuous AK47 fire. **Maintainer-owned (2026-09-30):** possibly already fixed; it needs a recorded A/B against 1964/GEPD, which isn't suited to agents. Done together with the WrapFix check below. |
| Auto-fire gate at unstable 40–55 fps | partial | D427 | Exact at a steady 60 or 30 fps. At fluctuating rates it can flip between 1× and 2×, because `field_88C` is never rescaled. |
| Third-person Bond offset residual | partial | D173, D292 | Mostly fixed. A small drift remains about 1 time in 10. |
| Statue corner flicker at max FOV | verify | D294 | Room-pool cap raised 2.0 -> 2.5 (2026-09-30; Statue needs about 2.17x). It is unproven: the exhaustion log never fired at spawn. **Owed:** strafe fast past the Statue corners at max FOV and max draw distance. |
| Crash on save slots 1/3/4, plus a host BSOD (fltmgr.sys) | open | #94 | Needs a repro. Two maintainer retest requests (v0.3.0, v0.4.0) got no reply. Likely D299/D301 (save-slot crash, fixed) and D344 (BSOD on exit, fixed). Close as stale if there's still no reply, and reproduce locally rather than asking the reporter again. |
| SkipIntro skips the post-mission failure dossier | verify | D408, D216 | Fixed 2026-09-30: the hook is gated to first boot. **Owed:** fail or abort a mission with SkipIntro on and check the dossier shows; also listen for whether D216's audio break is gone. |
| Front-end: Rareware logo filtering, cast-roll models | partial | D75 | Cosmetic. The other front-end logos are fixed. The cast-roll character models have never been verified. |
| Authored mip chains not sampled for explicit-LOD textures | parked | D323 | Distant textures use driver mips instead of Rare's hand-authored chain (heaviest on Statue/Aztec). Parked as a fidelity enhancement. |
| Approximations in the software RSP | open | `gfx_pc.cpp` `G_CCMUX_LOD_FRACTION`; `Video.WrapFix` (§F row `RC3 · D167`, D74 family) | The LOD-fraction CC input is an eyeballed approximation. The non-power-of-two wrap fix is still opt-in (default off), with unknown visible impact. Both need a comparison against 1964. **2026-09-30 triage** (`docs/dev/notes/FIDELITY-TRIAGE.md`): the LOD-fraction input is bundled with D323 as one "real RDP LOD" project (fix later, large). **WrapFix is maintainer-owned:** a 1964 Depot-ceiling screenshot vs the port with WrapFix on/off decides the default. |
| Latent 64-bit ABI hazards | partial | D441, #107, #108; ~396 `-Wpointer-to-int-cast` sites at the start of the sweep (185 remain in the 17 touched files; many are intentional `(s32)&ANIM_DATA_*` offsets, D34); AUDIT-M6 `struct player`/`struct hand` raw offsets (porting-notes §A1) | Part 1 landed (D441): 17 files widened to `uintptr_t` under `#ifdef PORT`, build and Bunker/Silo runs verified. **Still owed:** the full census with per-site provenance (H/R/I/?, table not yet written) plus a **store-to-u32 vs arithmetic-only** tag per site (a `uintptr_t` widening is enough only for arithmetic; sites that store back into a u32 field need `portHostToN64` under the macOS port_addr model), an env-gated high-arena test mode, and a check under an arena above 4 GB. Harmless today because the arena sits low. They would bite on macOS/ARM/ASLR. **#108 concern:** widening the `sndPlaySfx` bound disables the guard on every platform; prefer a range check. **#107 will now conflict** with D441 in `bg.c` (`bgRoomCalcBB`), so rebase or close it during the outside-PR review. A read-only provenance pass over the remaining sites is a good local-Qwen delegate task. |

## 3. Verification debt (fixed; a live check is owed)

A single campaign pass with these on the checklist would clear most of them.

| Item | Refs | Check |
|---|---|---|
| Presets: Original N64 / Port defaults | D440 | Press both rows in F10 and in the front-end PC Options with a mouse and a pad; draw distance applies from the next level load. `GE_OPTIONTREEPROBE` already fails on the base commit (separate look). |
| Probe strip (2026-09-30) | D245, D302, D318 | Frigate or Dam water/sky A/B against the previous build (the s16 sky path was removed); one Facility playthrough to confirm the D318 watchdog stays silent. |
| Infra fixes | D179, build-pc.sh, crash.c, CI | The 2026-09-30 re-exec no-op (PATHEXT=.CPL in the inherited env) is fixed and verified from the agent shell (see AGENTS.md). `./build-pc.sh` from a genuine MSYS2 login shell; the next CI run (pinned actions, workflow input check). `GE_CRASHTEST=6` shows resolved frames. The romdata `VirtualAlloc` failure path now fails boot instead of falling back to the heap copy: confirm you want that. |
| Light-fixture hit-type reads (impact sound + sparks) | D434, #119 | Shoot fixtures on Bunker and Caverns. |
| v0.4.1 batch (unreleased; fixes live on `release/v0.4.1`) | D424–D432; #114 fire rate, #115 tank-crush audio loop, #116 tank movement, #117 sway, #118 GL recoil, #119 light fixtures | All user-verified against GEPD/1964 on 2026-09-29. Close the issues when it ships. |
| FOV-scale edge culling | D222 | Max FOV, pan across NPCs at the screen edge. |
| Water pulse, Surface 2 | D229 | Frigate is verified. Only the Surface 2 by-eye check is owed. |
| Linux save/config path when launched outside the game dir | D256 | Save-flow check on Linux. |
| Settings/F10 batch | D356, D358, D361, D363, D374, D406 | Live accept of each item in the F10 overlay. (D377–D379 residuals were waived.) |
| Controller feel | CONTROLLER-INPUT-PLAN Wave A, D204 | Per-stick deadzone/smoothing feel on a pad and on Deck; D204 tempo by ear; `Input.RumbleScale=0` silences rumble. |
| Widescreen HUD at 21:9 | D334/D335 | One ultrawide HUD playthrough. |
| Older "owed" notes likely cleared by the 2026-09-28 campaign sign-off | D150, D177, D264, D369, D371, D372 | Tick off in findings (bookkeeping only). |

## 4. Platform

| Item | Status | Refs | Notes |
|---|---|---|---|
| macOS (Intel + Apple Silicon) / ARM64 Linux | open | PRs #88, #95; #101 | Outside drafts, parked for `main` until after 1.0. **Address model decided (2026-09-30): port_addr (#95)**, i.e. a single `portN64ToHost`/`PORT_N64PTR` chokepoint with a `PORT_ADDR_STRICT` validator, over the dram-scoped alternative. The `macos` integration branch **exists on origin** (2026-09-30). It was cut from `origin/main` `0e8c2ce2`, and it takes #88 (merged as `c4153157`, using #95's conflict-resolved copy `ae1884cc`), #132 (Linux build fix, `39d4f8d0`: #88's `romdata.c` included `<errno.h>` before `ultra64.h`, and glibc's `errno` macro broke the `errno` fields in `PR/os.h`; #95 needs it on rebase) and #95 (retargeted to it, still open). italoarruda's work also goes there. Nothing merges from it to `main` before the post-1.0 pass. **Open against `macos`:** #131, the fork-friendly self-hosted sweep. It adds `platform` and `runner_labels` inputs, a macOS job running `level_sweep_mac.sh` with a JSON verdict, seed-dir-only ROM staging, and `docs/selfhosted-sweep.md`. It is a scaffold that has not been run on a Mac, and it needs #95 in `macos` first. **When `macos` is next rebased onto `main`** (after the v0.4.1 push), `selfhosted.yml` will conflict with the v0.4.1 ROM-cache fix. Take #131's version, which already includes that fix. The crouched-rifle table fix (`chr.c`), found three times (#95, italoarruda, #120), landed on `release/v0.4.1` via #120 (D445) on 2026-09-30. #120's shape is the correct one (it restores the N64 table length of 2; #95's terminator-only version leaves length 1), so **drop #95's `crouched_rifle` hunk** when `macos` is rebased. #95's finding labels (D29x, then D405–D421) clash with ours, so relabel on merge. The maintainer tests firsthand before merging. Needs the ABI-hazard sweep in §2 (store-to-u32 tagging). |
| Platform parity gate (Linux co-equal with Windows) | open | — | **Decided 2026-09-30:** yes. Every released platform carries the same gate: a 21-level sweep plus a campaign pass. Needs the golden baselines on both platforms first (§7). The last known Linux crash (D190) was closed as not-a-bug. |
| Low-end GPUs (~30 fps on Celeron/HD 400) | open | D339, #92 | Needs `GE_PERFSTAT` data from that class of hardware. The D372 low-end preset is the mitigation. |
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
| `All unlocked` rework: RAM override instead of save patching | open | D387, D259 | **Approved 2026-09-30.** Levels are already RAM-only (the game's `debug_enable_all_levels_flag`/`debug_007_unlock_flag`, `port/src/main.c`). Replace the cheat half, a block-4 EEPROM read patch plus the D387 write merge in `port/src/libultra.c`, with one opt-in `#ifdef PORT` hook at the cheat-unlocked check (`src/game/file2.c` `fileGetIsCheatUnlocked`, and confirm every caller including `front.c` `frontCheckIfCheatIsUnlocked`). The save is then never touched: earned cheats and times persist normally, OFF restores real progress, and the D259 silent-volume quirk goes away. This is a `src/game` edit for an opt-in port feature, so document it as such (maintainer-approved). Then remove the D387 merge machinery. Already-persisted fakes from older builds stay unrepaired. |
| Settings units and values cleanup | open | D357 ([plan](dev/D357-SETTINGS-VALUES-PLAN.md)) | **Approved 2026-09-30 with changes** (recorded in the plan's §0): FOV in degrees; FPS cap presets 30/60/90/120/144/240/Uncapped **plus** a free custom value; MSAA adds 16×; mouse invert stays a separate toggle; the texture filter keeps "N64 3-point" (the plan's rename rests on a wrong premise). Refresh the plan's stale "Now" column first (the deadzone is split L/R; draw/LOD defaults are 250). |
| Restart button in the settings menus | open | — | QoL (2026-09-30): next to Quit to desktop, a "Restart game" action so that restart-required changes (MSAA, etc.) apply in one click. Relaunch the same exe with the same args after an orderly quit (the D344 path, since hard exits have BSOD'd). Offer it wherever a changed row is flagged "restart". |
| Front-end PC Options: Previous/Next paging for mouse users | open | — | QoL (2026-09-30): add clickable Previous/Next (page) controls to the main-menu settings screen, so mouse users don't have to rely on wheel scrolling. Today the wheel scroll competes with mouse hover: any small mouse nudge re-selects a row and snaps the scroll back. Fix that interaction too. Hover must not reset the scroll position, and it should only change the selection once the pointer moves onto a different row. Owner: the D357/D443 track (same file, `port/src/frontoptions.c`). |
| Outside PRs #107–#109, #122–#124 | open | #122 hot-plugged pads never reopen, #123 persistent crosshair (`gunfire.c`), #124 compact health/armour bars (`bondview2.c`); #107/#108/#109 (italoarruda) | **#120 and #121 merged locally into `release/v0.4.1` on 2026-09-30** (dolent's original commits kept for credit; relabelled D445/D446; the review is in `docs/dev/notes/PR-REVIEW-120-121.md`). Close both on GitHub when the branch is pushed. **Owed live:** kneeling rifle guards (#120), and the 2P watch-menu text (#121, on the MP branch). **Remaining order (proposed; approval owed):** #122 (then rebase split-screen on it; #122/#109/MP all touch `input.c`) → #107 (fold remaining sites into the D441 census, then close) → #108 (ask for a range check; belongs with `macos`) → #109 (after split-screen settles `input.c`) → #123/#124. **Policy:** any PR that adds a *feature* (#109, #123, #124, …) gets a **design review before any work**: compare against other PC ports and remasters (PD port, Nightdive, GE+/XBLA), then approve, or send suggestions back and defer until the submitter revises. **Their finding labels clash with ours:** relabel on merge. `src/game` touches (#120/#123/#124) each need a Rule-2 or ABI-exception check. |
| Gamepad rebinding + modern controller presets | open | PR #109, D394 | QoL (maintainer, 2026-09-30), one item: rebindable controller buttons, plus presets taken from the modern Xbox Series X release's control scheme (the D394 "Jinx" 1.1 layout, with a 1.2/1.3 selector). The remaining D394 gap is the **gadget category**: the camera, the GoldenEye key copier, bombs, and other devices planted on things. Also pad-initiated binding capture. Keyboard/mouse rebinding has shipped. Review #109 under the feature design-review policy first. |
| Per-pad tuning (device index, hot-plug seats) | open | — | Needed for split-screen anyway. |
| Full-gamepad menu navigation audit, Xbox/PS button glyphs | open | — | Menus are only partly audited for pad-only use. There are no glyphs yet. |
| Mute and auto-pause on focus loss; PNG screenshots to `screenshots/` | parked | D231, PR #56 (closed unmerged), `docs/dev/notes/parked/0001-QoL-*.patch` (local) | Deprioritised by the maintainer on 2026-09-25. F12 today is the dev PPM dump. |
| F10 overlay polish | open | D335b, #90 | Pillarbox the overlay in widescreen; colour experimental rows red; warn on unknown `ge007.ini` keys (promised in #90; today they are only logged). |
| Master volume, audio output device select | open | — | |
| Frame rate above 60 (120/144/uncapped with interpolation) | decision | [`dev/UNLOCKED-FPS-PLAN.md`](dev/UNLOCKED-FPS-PLAN.md) | The menu cap is {30, 60} (the ini accepts 0 = uncapped, but the sim ticks at the VI rate). The plan's Phase 0 is resolved by D248 (the default was a port bug; it is a real 60 fps) and its Phase 1 audit and Phase 2 slice scope are written (landed 2026-09-30), but the header still reads parked. Next step is a standing sim/render-rate measurement, not the old 30 fps investigation. Must stay compatible with netplay determinism. |

### 5b. GE+ / Dab's-mod gameplay options (opt-in; touch game code)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Disable knockback / hitstun, damage sound, endless death cam | decision | hooks at `chraction.c:2233`, `gunfire.c:3334`, `bondview2.c:8354` | Needs one batched Rule-2 sign-off. |
| Bodies stay where they fell | decision | — | |
| Port-native third-person camera | decision | #110 | The 007 Plus ROM patch itself is out of scope. Decide whether a port-native TP camera is in or out. |

### 5c. Enhanced visuals (optional; after feature-complete)

The XBLA/Nightdive-style "new look". None of it has started in this repo.
An outside fork has a large Original/Enhanced/Remaster render-mode stack
(see *Community forks* below). Adopting any of it is its own decision and
needs Rule-2 review.

| Item | Status | Notes |
|---|---|---|
| Post-process chain: FXAA/SMAA, bloom, depth of field, colour grade/LUT, CRT filter, brightness/gamma, colourblind filters, SSAO | decision | Build the framebuffer pass once. Every effect defaults OFF. |
| Render scale / supersampling, integer scaling, "authentic 320×240" mode | open | |
| Dynamic/coloured lighting, dynamic shadows, water reflections | decision | Large. Consult the PD port first. #105 is about the fork's version of reflections, not ours. |
| HD texture packs: loader + texnum dumper; model packs | decision | #100. There is a staged design. Step 1 (texture identity registry) touches `src/game`, so it needs Rule-2 framing. |
| Animation smoothing (keyframe slerp) | open | |

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
| LAN netplay | decision | — | Depends on split-screen. Determinism groundwork is D117 (not achieved). |
| MP bots | decision | #111 | Not N64 (GoldenEye's MP is human-only; Simulants are Perfect Dark). birdturtle `simulants` is an outside prototype. |
| Co-op campaign | decision | — | Not N64. thepont `feature/coop` is an outside prototype. Listed under Not planned unless this changes. |
| Cheats beyond the N64 cheat menu | decision | #113 | Only in scope as an opt-in toggle. |

## 7. Tooling, infrastructure, security

| Item | Status | Refs | Notes |
|---|---|---|---|
| Security review follow-ups | open | maintainer's local security review | Three Low hardening items remain (local-input only; details are kept out of this public file). Done on `release/v0.4.1`: the Medium item (self-hosted workflow input handling), actions pinned by SHA, `pyinstaller` pinned to 6.22.3, and the ROM no longer stored in `actions/cache` by `selfhosted.yml` (fork PR workflows can restore base-branch caches; no cache entry ever existed, checked 2026-09-30; it is now staged from the runner's `_rom-seed` dir only). The Dependabot action bumps (#1/#2) would now need to land as SHA pins, not tag bumps. |
| Deterministic 1964-vs-port comparison harness | open | — | Would replace by-eye/by-ear fidelity checks. Highest-leverage tooling item. |
| Scripted-playthrough harness | open | `GE_INPUTSCRIPT` | Input script + `GE_PCDUMP` + crash-log check, for unattended regression runs. The tool exists; the harness doesn't. |
| Golden baselines | open | `tools_pc/verify.sh`, D117 | bunker1 has been noisy since M-84, so the per-patch gate is unreliable. Re-base it, extend to all 21 levels × both platforms, and finish `GE_DETERM`. |
| Self-hosted CI runner link failure | open | D244 | Needs a `pacman -Syu` on the runner (maintainer-only access). Release tag builds use GitHub runners, so it doesn't block a release. |
| Remove temporary scaffolding before a release | partial | [`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md), D302 | The 2026-09-30 pass removed D207S, D157, D252POOL, D245V, D288, D309, the D318B suite, the D320 repro harness and `scan_op12/13.py`, demoted the D318 watchdog to detect-and-log, and cached the per-frame `getenv` sites. **Left:** dead registry rows (D116, D51, D56, D60-D63, D69*, D71LOG, D85DUMP, D86-D88, D90, D96, D104, D154, D178); closed-finding TEMPs (`GE_D306C`, `GE_DYNTEXHASH_OFF`, `GE_FORCEALARM`, `GE_D204_OLD`, `GE_D252`, `GE_D243X2/X4`, `GE_D318T`, `GE_RSEED_LV`, `GE_D235_*` once D235 closes); the now-inert sky tile-capture/shift machinery in `sky.c` (`skyPortCaptureTile`, `skyPortPickShift`, `GE_D245_FIXEDSHIFT`); and the M-183/M-185 clamps, which need a full cutscene re-check first. |
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
#88/#95 address model is port_addr. Still owed:

1. **Split-screen:** approve the D417 Rule-2 change (`PROP__PORT_SIGNED`) when the branch is ready to merge.
2. **Outside PR review order:** final approval of the order proposed in §5a.
3. **GE+ gameplay options (§5b)** and a port-native third-person camera: in or out?
4. **Enhanced visuals (§5c):** build our own, adopt parts of thepont's stack, or skip?
5. **Beyond-N64 multiplayer (§6):** netplay, bots, co-op, cheats. In scope after feature-complete, or not?
6. **D433 gunshot timbre, D323 authored mips:** decided per gap during the Stage A fidelity triage.

## Not planned

Online (non-LAN) multiplayer, matchmaking, leaderboards, anti-cheat,
cross-play · Steamworks, achievements, cloud saves (no storefront) · ray
tracing, HDR, frame generation, stereo 3D · remake-scope assets (new
models, music or voice) · new movement mechanics (jump, roll, melee) · the
007 Plus ROM patch as such (#110) · co-op (unless decision 7 changes it) ·
Windows 7 (#98, pending decision).
