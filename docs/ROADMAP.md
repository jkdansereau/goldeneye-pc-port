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
to the original look and feel (see the *Original N64 preset* in §5).

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
| **`All unlocked` can permanently write fake unlocks into your save** | Save corruption | Back up `data/ge007.eep` before enabling it; don't use it on a save you care about | D387, §5a |
| `Game.SkipIntro` also skips the post-mission failure dossier | Minor | Leave SkipIntro off | D408, §2 |
| F10 overlay stretches in native widescreen (menus pillarbox correctly) | Cosmetic | — | D335b, §5a |
| Rareware logo shows a subtle texture-filtering artifact | Cosmetic | — | D75, §2 |
| Steam Deck: a first launch in Desktop Mode skips the Deck preset | Settings | First launch in Game Mode, or delete `ge007.ini` and relaunch | D283, §4 |
| ~30 fps on very low-end GPUs (e.g. Intel HD 400) | Performance | Set `Video.LowEndMode=1` in `ge007.ini` | D339, #92, §4 |
| No macOS or ARM builds | Platform | — | #88, #95, #101, §4 |
| Controller buttons can't be rebound in-game (keyboard/mouse can) | Missing feature | — | #109, §5a |

---

## 1. Feature completeness vs the N64 cartridge

| Item | Status | Refs | Notes |
|---|---|---|---|
| **2–4 player split-screen multiplayer** | parked | #99, D416–D423, branch `feat/mp-splitscreen-99` | The largest missing N64 feature. 2P has been live-tested (M&K + pad, respawn, 16:9 HUD). Before merge: Rule-2 review of `PROP__PORT_SIGNED` (D417) and a golden gate run on the maintainer's console. Open on the branch: D420 (stats screen shows a large number), D421 (gadget cycling off; needs a Rule-2 game-thread hook), D422 (no centred aim for P2+), D423 (black arrowhead in P2 view), and D428's sway accumulator is player-0-only. Untested: 3P/4P, pads-only mode, pause/watch/F10 during a match, MP audio, most stages, and the full N64 MP setup screens (scenarios, handicaps, teams). Needs per-pad seats (§5). An outside restore exists for reference: birdturtle `feature/local-mp-input`. |
| **PAL and JP ROM support** | decision | D258, D75 (pal/jpn sidecar regen), #85 | Conversion is broken at the source-data level (filelist naming). A repair path has been verified but not applied. Needs PAL/JP ROMs to test. #85 was closed as "retired for now", not won't-do, so public users currently have no tracker. Reopen it or file a fresh one when work starts. |
| Drop-in ROM converter, remaining parts | decision | #6, [`dev/release-dropin-rom-plan.md`](dev/release-dropin-rom-plan.md) | NTSC-U drop-in shipped. Left: per-region output names and re-exec guard (with PAL/JP), native C emitters to replace the PyInstaller converter. |
| Cut content (TCRF weapons/items) | decision | [`dev/CUT-CONTENT-BACKLOG.md`](dev/CUT-CONTENT-BACKLOG.md) | Not part of the shipped game. Tier 0 already works via the debug/cheat menus. Tier 1 (config-toggle grants) is small and port-only. Tier 2/3 (placing props, re-enabling unused MP maps) need game data changes and are out of scope for this repo. |

## 2. Accuracy (behaviour differs from the N64)

| Item | Status | Refs | Notes |
|---|---|---|---|
| Auto-fire gunshot sound slightly differs from 1964/GEPD | open | D433, #114 | Cadence now matches the N64 rules exactly. The residual is timbre (resampler / release envelope suspected). Needs a precise by-ear description. |
| SFX voice cap and long-session audio decay | partial | D322, #87 | The Windows periodic garble is fixed. Still open: is the 8-voice cap faithful, and the progressive decay that #87 reported (not reproduced here). The owed GitHub housekeeping is in `docs/dev/notes/ISSUE-87-FOLLOWUP.md` (local). |
| Auto-fire gate at unstable 40–55 fps | partial | D427 | Exact at a steady 60 or 30 fps. At fluctuating rates it can flip between 1× and 2×, because `field_88C` is never rescaled. |
| Third-person Bond offset residual | partial | D173, D292 | Mostly fixed. A small drift remains about 1 time in 10. |
| Statue corner flicker at max FOV | partial | D294 | Room-pool residual after the black-ground fix. |
| Crash on save slots 1/3/4, plus a host BSOD (fltmgr.sys) | open | #94 | Needs a repro. Two maintainer retest requests (v0.3.0, v0.4.0) got no reply. Likely D299/D301 (save-slot crash, fixed) and D344 (BSOD on exit, fixed). Close as stale if there's still no reply, and reproduce locally rather than asking the reporter again. |
| SkipIntro skips the post-mission failure dossier | open | D408, D216 | Only affects the experimental `Game.SkipIntro` option. The audio-break half of D216 is also unrooted. |
| Front-end: Rareware logo filtering, cast-roll models | partial | D75 | Cosmetic. The other front-end logos are fixed. The cast-roll character models have never been verified. |
| Authored mip chains not sampled for explicit-LOD textures | parked | D323 | Distant textures use driver mips instead of Rare's hand-authored chain (heaviest on Statue/Aztec). Parked as a fidelity enhancement. |
| Approximations in the software RSP | open | `gfx_pc.cpp` `G_CCMUX_LOD_FRACTION`; `Video.WrapFix` (§F row `RC3 · D167`, D74 family) | The LOD-fraction CC input is an eyeballed approximation. The non-power-of-two wrap fix is still opt-in (default off), with unknown visible impact. Both need a comparison against 1964. |
| Latent 64-bit ABI hazards | open | #107, #108; ~396 `-Wpointer-to-int-cast` sites (count from a local review note; re-count with a `-Wpointer-to-int-cast` build); AUDIT-M6 `struct player`/`struct hand` raw offsets (porting-notes §A1) | Harmless today, because the arena sits low in the address space. They would bite on macOS/ARM/ASLR. The fixes fall under the AGENTS.md ABI/layout exception. **#108 concern:** widening the `sndPlaySfx` bound disables the guard on every platform; prefer a range check. Check #107 against D430/D431/D434, which touch the same `lightFindVertexBaseForTri` region. A read-only provenance pass over the 396 sites is a good delegate task. |
| Controller preset parity (Xbox "Jinx" 1.1, 1.2/1.3 selector) | partial | D394 | The gadget category and pad-initiated binding capture are still open. |

## 3. Verification debt (fixed; a live check is owed)

A single campaign pass with these on the checklist would clear most of them.

| Item | Refs | Check |
|---|---|---|
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
| macOS (Intel + Apple Silicon) / ARM64 Linux | open | PRs #88, #95; #101 | Outside drafts. The maintainer tests firsthand before merging. Needs the ABI-hazard sweep in §2. |
| Linux as a co-equal platform | decision | — | Does Linux carry the same sign-off gate as Windows (a full sweep of all 21 levels)? The last known Linux crash (D190) was closed as not-a-bug. |
| Low-end GPUs (~30 fps on Celeron/HD 400) | open | D339, #92 | Needs `GE_PERFSTAT` data from that class of hardware. The D372 low-end preset is the mitigation. |
| Steam Deck preset skipped when first launched from Desktop Mode | open | D283 | Workaround is documented in the README. Make the fix not depend on the env var. |
| Windows 7 | decision | #98 | Recommend won't-fix (toolchain). |

## 5. Modern-remaster layer (port features beyond the N64)

**The bar:** feature parity with a Nightdive-style remaster and with
GE+/Dab's mod where it's practical. Shipped so far: native widescreen and
ultrawide, FOV, MSAA/anisotropic/texture-filter options (including N64
3-point), draw and LOD distance, VSync and a frame cap, show-FPS,
keyboard/mouse rebinding with a GEPD-style layout, pad deadzone,
sensitivity, smoothing, southpaw and triggers, rumble, crosshair and HUD
customisation, HUD scale, music/FX volume, no-hit-flash, SkipIntro
(experimental), All unlocked (unsafe, see D387), and the F10 overlay.

The modern defaults (native widescreen, overscan crop, 250% draw/LOD
distance, bilinear filtering) are **deliberate**. They are not a fidelity
defect.

Background research lives in the maintainer's local notes (gitignored):
`GEPORT-REFERENCE-DEEPDIVE.md`, `MODERN-PORT-FEATURES-BACKLOG.md`,
`MODERN-OPTIONS-PLAN.md` and `PD-LEGACY-SURVEY.md` under `docs/dev/notes/`.

### 5a. Before 1.0

| Item | Status | Refs | Notes |
|---|---|---|---|
| **"Original N64" preset toggle** | open | — | One switch that snaps every value the port has changed back to N64 values: draw/LOD distance, culling, texture filter (3-point), 4:3, no overscan crop, stock FOV, N64 aim. It's cheap, because each is already a config key. This is the port's answer to the XBLA/Nightdive "original vs enhanced" switch. |
| `All unlocked` writes fake unlocks into the real save | open | D387 | **The only data-integrity bug.** Fix it or gate the option harder. Until then, the README warns users to back up the EEPROM. |
| Settings units and values cleanup | decision | D357 ([plan](dev/D357-SETTINGS-VALUES-PLAN.md)) | Awaiting sign-off. Its "Now" column is partly stale: the deadzone is already split L/R, and draw/LOD defaults are 250, not 150. Covers FOV in degrees, sensitivity shown as ×, MSAA 16×, filter naming, volume step and the FPS preset grid. |
| Gamepad button rebinding UI | open | PR #109, D394 | Keyboard/mouse rebinding has shipped. Review #109 under the outside-PR policy. |
| Per-pad tuning (device index, hot-plug seats) | open | — | Needed for split-screen anyway. |
| Full-gamepad menu navigation audit, Xbox/PS button glyphs | open | — | Menus are only partly audited for pad-only use. There are no glyphs yet. |
| Mute and auto-pause on focus loss; PNG screenshots to `screenshots/` | parked | D231, PR #56 (closed unmerged), `docs/dev/notes/parked/0001-QoL-*.patch` (local) | Deprioritised by the maintainer on 2026-09-25. F12 today is the dev PPM dump. |
| F10 overlay polish | open | D335b, #90 | Pillarbox the overlay in widescreen; colour experimental rows red; warn on unknown `ge007.ini` keys (promised in #90; today they are only logged). |
| Master volume, audio output device select | open | — | |
| Frame rate above 60 (120/144/uncapped with interpolation) | decision | [`dev/UNLOCKED-FPS-PLAN.md`](dev/UNLOCKED-FPS-PLAN.md) | The menu cap is {30, 60} (the ini accepts 0 = uncapped, but the sim ticks at the VI rate). The plan's premise predates the D248 real-60 fix, so re-measure first. Must stay compatible with netplay determinism. |

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
| Security review follow-ups | open | maintainer's local security review | Four open hardening items: one Medium (self-hosted CI workflow input handling), the rest Low (local-input only). Details are kept out of this public file. Also: pin Actions by SHA, pin `pyinstaller`, take the Dependabot action bumps (#1/#2). |
| Deterministic 1964-vs-port comparison harness | open | — | Would replace by-eye/by-ear fidelity checks. Highest-leverage tooling item. |
| Scripted-playthrough harness | open | `GE_INPUTSCRIPT` | Input script + `GE_PCDUMP` + crash-log check, for unattended regression runs. The tool exists; the harness doesn't. |
| Golden baselines | open | `tools_pc/verify.sh`, D117 | bunker1 has been noisy since M-84, so the per-patch gate is unreliable. Re-base it, extend to all 21 levels × both platforms, and finish `GE_DETERM`. |
| Self-hosted CI runner link failure | open | D244 | Needs a `pacman -Syu` on the runner (maintainer-only access). Release tag builds use GitHub runners, so it doesn't block a release. |
| `build-pc.sh` TMP self-heal fails in-script | open | AGENTS.md | A standalone `.ps1` workaround is documented. |
| Remove temporary scaffolding before a release | open | [`dev/GE-ENV-PROBES.md`](dev/GE-ENV-PROBES.md), D302 | Probes: D207S, D157, D252POOL, D245V, D236 (RAW/RM/ORDER/ALPHA/ZFIX/BT), D288, D309, D318B/D320 repro, `GE_OBJT`. Tools: `tools_pc/scan_op12.py`, `scan_op13.py`. Also demote the D318 watchdog to detect-and-log (D329 removed the cause), remove the M-183/M-185 clamps after a full cutscene re-check, and grep hot paths for `getenv` (D302 repeat offender). |
| `crash.c` backtraces | open | — | The EBP walk dies at frame 1. Switch to `CaptureStackBackTrace`/`StackWalk64`. |
| `romdata.c` VirtualAlloc fallback hardening | open | D179 tail | Minor. |
| Findings log and doc size | open | — | `findings.md` is about 2 MB and growing. It needs the deferred porting-notes TOC and docs-budget script (context cost for every session). |
| Branch and PR housekeeping | open | — | Push local `main` (5cccbcef, post-v0.4.0 bookkeeping). Prune the ~20 squash-merged branches: keep `feat/mp-splitscreen-99`, `land/modloader-*` and `research/cut-content-backlog`, and first confirm the single unique commits on `feat/unlocked-fps-phase1-audit` and `docs/m138-security-disclosure` landed. PR #62 (mouse dt-decouple) stays held until mouse feel is fundamentally better. |
| Docs site: Web 1.0 phase 2 | decision | — | Phase 1 shipped. Phase 2 (centred serif page) is plan-only. |

## Community forks (not ours; reference only)

| Fork | What it has | Relevance |
|---|---|---|
| thepont/goldeneye-pc-port `feature/graphics-final-stack`, `fix/original-water-fidelity`, `feature/coop` | ORIGINAL/ENHANCED/REMASTER render mode, lighting, post-FX, bump/shadow/DoF, water reflections, vector HUD font, 2P co-op | Source of #105. Heavy `src/game` edits. **Their D323–D326 and "D329" labels clash with ours**, so relabel anything ported. |
| birdturtle `feature/local-mp-input`, `simulants` | Local MP restore; first MP bot | Cross-check for #99 and #111. |
| kaziema `vita` | PS Vita port | Platform reference. |
| mattymattmattmatt/goldeneye-pcvr-port `vr/stereo-headaim` | VR stereo + head aim | Platform reference. |

## Decisions owed (maintainer)

1. **PAL/JP:** pursue it (needs those ROMs; also unblocks drop-in Part B), or stay NTSC-only?
2. **Split-screen:** approve the D417 Rule-2 change, then merge `feat/mp-splitscreen-99`.
3. **D387:** fix `All unlocked`'s save leak, or restrict the option?
4. **D357:** sign off the settings units/values plan (after refreshing its stale table).
5. **GE+ gameplay options (§5b)** and a port-native third-person camera: in or out?
6. **Enhanced visuals (§5c):** build our own, adopt parts of thepont's stack, or skip?
7. **Beyond-N64 multiplayer (§6):** netplay, bots, co-op, cheats. In scope after feature-complete, or not?
8. **Linux co-equal status** and **release cadence / tag policy**.
9. **Outside PRs:** #107 and #108 (with the guard concern), #109, #88/#95. Review order.
10. **D433 gunshot timbre, D323 authored mips:** worth chasing, or accept?

## Not planned

Online (non-LAN) multiplayer, matchmaking, leaderboards, anti-cheat,
cross-play · Steamworks, achievements, cloud saves (no storefront) · ray
tracing, HDR, frame generation, stereo 3D · remake-scope assets (new
models, music or voice) · new movement mechanics (jump, roll, melee) · the
007 Plus ROM patch as such (#110) · co-op (unless decision 7 changes it) ·
Windows 7 (#98, pending decision).
