# Split-screen co-op campaign (Switch) — design

Date: 2026-11-14. Status: approved by the maintainer 2026-11-14, including
the rule-2 derogation below; revised the same day after an independent
review (site count, seam shape, downed-player rules).

Items marked **(decided)** were stated by the maintainer. Items marked
**(call)** are the author's choices and are open to correction.

## Intent

Let two people play the solo campaign together on one Switch, in split
screen. The fork tracks an upstream that keeps `src/game` byte-faithful to
the decomp, so the feature must cost almost nothing at merge time: hooks in
the game code, logic in the port layer.

## Goals

- Two players, one console, horizontal split, any solo mission. **(decided)**
- A dead player can be revived by the partner: press A on the body, then
  wait 10 seconds. **(decided)**
- Enemies can be made tougher to compensate for two guns. **(decided, "to
  look into")**
- Minimal `src/game` footprint: one-line hooks only. **(decided)**
- With the option off, behaviour is identical to today, frame for frame.
  **(call)**

## Non-goals

- 3 or 4 players. Network or two-console play.
- Rebalancing levels, new enemy placement, new objectives.
- Co-op specific HUD art, new models, a second playable character skin
  beyond what multiplayer already provides.
- Upstreaming. This is a fork feature; it only has to merge cleanly.

## Anti-goals

- Any edit that changes solo or versus behaviour when co-op is off.
- Multi-line logic inside `src/game`. If a hook needs more than a call and
  a condition, the logic belongs in `port/src/coop.c`.
- Reformatting or restructuring upstream code around a hook.

## Constraints

- AGENTS.md rule 2 forbids behavioural `src/game` edits. Co-op is a
  deliberate, fork-local derogation, limited to guarded one-line hooks and
  recorded in `docs/dev/findings.md`. **(approved)** It also covers a hook
  in `src/boss.c`, where the stage's player count is consumed
  (`boss.c:561`–`567`).
- The engine has no mode flag. "Mission or versus" is inferred from
  `getPlayerCount()` (`src/game/player.c:105`): 157 call sites in 30 files,
  43 of them in `bondview2.c`. Roughly 60 are rule decisions; the rest size
  render and memory budgets per viewport and must keep seeing the true
  count.
- That one test stands for at least four different questions: which rules
  apply, whether the player's third-person body exists, which camera is
  used, and how death is handled. Co-op needs the solo answer to some, the
  versus answer to others, and a new answer to a few.
- Switch memory and frame time with two views of a solo level are unmeasured.

## Approaches

**A. Thin seam (chosen).** A port function `coopIsMission()` is consulted at
the rule sites instead of the raw player count, each as a one-line guarded
edit. All co-op behaviour lives in a new `port/src/coop.c`. Cost: about 60
one-line edits plus a handful of hooks; merge conflicts are rare and
trivial. Chosen because it matches "just hooks" and keeps the off state
identical to upstream.

**B. No `src/game` edits (remap `getPlayerCount` per file at compile
time).** Rejected: `lv.c`, `prop.c` and `gunfire.c` mix render-budget sites
and rule sites in the same file, so a per-file remap gives wrong budgets or
wrong rules.

**C. Port the outside prototype (thepont `feature/coop`).** Rejected: heavy
`src/game` edits, conflicts at every upstream merge.

## Design

### The seam

`port/include/coop.h` exposes:

- `coopActive()` — co-op is on for the current stage.
- `coopIsMission()` — true in solo, and true in co-op. Replaces
  `getPlayerCount() == 1` at rule sites; `!coopIsMission()` replaces
  `getPlayerCount() >= 2` at rule sites.
- Where a site needs an answer that is neither solo nor versus, a named
  predicate for that question (body, camera, death) or `coopActive()`.

Hook shape, the only form allowed in `src/game`: a one-line **expression
substitution**. Many sites are a ternary or a term inside a compound
condition, so replacing a whole `if` does not work. The comparison is
replaced by a macro defined once (under `#ifdef PORT` it calls the port;
otherwise it expands to the original expression).

Known rule sites to convert (the implementer classifies all 157):

| Site | What it decides |
|---|---|
| `prop.c:1311` | loads the `mp_` setup instead of the mission setup |
| `bondview_r.c:482`, `bondview2.c:8902` | random spawn pad |
| `bondview_r.c:554` | versus camera instead of intro (third answer) |
| `bondview2.c:685` | solo removes the player's body; co-op needs it (third answer) |
| `bondview2.c:9422` | death: solo fails the mission, versus respawns (third answer) |
| `bondview2.c:10013` | a dead player's collision bounds (third answer) |
| `bondview2.c`, ~30 further sites | classify one by one |
| `lv.c:528`, `:1125` | stage init branch; versus end-of-game rules |
| `chraction.c:2450`, `:2604` | damage rules, including the enemy-health modifier |
| `bondinv.c:280`–`:697` | dual wielding |
| `propobj.c:10396`, `:10429`, `:10703`, `:11148` | pickup rules |
| `cheat.c:820` | solo or versus cheat mask |
| `file2.c:1348` | save on mission end |
| `gun.c`, `gunfire.c`, `explosion.c`, `chrprop.c:2364`, `initBondDATA.c:240` | classify one by one in the spike |

Each site is classified as *rule* (convert), *budget* (leave), or *unsure*
(test both ways in the spike).

### Starting a co-op mission

**(call)** A "Co-op campaign" toggle in the existing options overlay
(`port/src/optionsoverlay.c`). When it is on and a second controller is
connected, launching a mission from the normal solo menu allocates two
players. No front-end menu edits. Both players spawn at start pad 0, the
second offset so they do not overlap. Both players are Bond with the solo
defaults; there is no character select. **(call)** A `GE_COOP=1`
environment switch forces co-op with two players without a second
controller, for test builds.

### Guards and objectives

- Guards target the nearest living player. **(call)** How guard AI resolves
  "Bond" is not yet traced; `chrai.c`/`chraction.c` reference
  `g_CurrentPlayer` only about 20 times, so a single hook that selects the
  nearest living player before each guard's tick is the working hypothesis.
  The spike confirms or replaces it.
- Objectives are shared. Either player can complete one; the mission ends
  when either player triggers the exit. **(call)**
- Inventories stay per player, as in versus. **(call)** Risk: objectives
  that test "Bond holds item X" may only look at one player. The spike
  lists them; the fix is a port-side "either player holds it" check.

### Death and revive

- When a player's health reaches zero they go down: the normal death
  animation plays, their view stays on the death camera, and the mission
  does **not** fail. **(call for the camera; revive itself is decided)**
- The partner stands at the body and presses A. A 10-second countdown shows
  on both views. Walking away or going down cancels it. **(decided: A, 10 s;
  call: cancel conditions)**
- At zero the downed player stands up where they fell with half health, no
  armour, and their inventory intact. **(call)**
- If both players are down at the same time, the mission fails exactly as a
  solo death does. **(call)**
- Guards neither target nor damage a downed player. **(call)**
- Level scripts that test "Bond is dead" (`chrai.c:4391`) fire only when
  both players are down. **(call)**
- A body the partner cannot reach (a fall, a scripted kill) is not
  teleported; such cases are listed during the level pass. **(call)**
- Hooks needed: one where solo death sets the mission-failed state
  (`bondview2.c:9422`), one in the per-player tick for the A press and
  countdown. Everything else, including the body position, countdown and
  respawn, is in `coop.c`. The stand-up reuses the versus state reset
  (`bondview.c:1435`, `bondview2.c:8889`) but not its random-pad move
  (`bondview2.c:8902`).

### Tougher enemies

The engine already scales player damage against guards with
`g_AiHealthModifier` (`chraction.c:2606`, set per difficulty at
`lv.c:984`, `:999`, `:1028`, `:1057`). **(call)** A "Co-op enemy health" option (100 %, 150 %,
200 %; default 150 %) divides that modifier after level init, from
`coop.c`. No extra `src/game` edit beyond the seam at `chraction.c:2604`.

### Pause, cutscenes, saves

- Either player can open the watch; it pauses the game for both. **(call)**
- Intro and outro cameras play on player 1's view; player 2 sees the same
  camera or a held frame, whichever the spike shows is cheaper. **(call)**
- Mission completion saves to the active file as a solo completion does.
  **(call)**

## Order of work

1. **Spike (throwaway).** Force two players with the mission setup on Dam,
   on the console. Measure memory and frame time; list what crashes or
   misbehaves; classify the `getPlayerCount()` sites; trace how guards pick
   their target. Output: a findings entry, no kept code.
2. Seam and co-op toggle: a mission is playable by two, mission fails when
   either dies.
3. Guard targeting and shared objectives.
4. Down and revive.
5. Enemy health option.
6. Cutscenes, pause, saves; full pass over the 20 missions.

Step 1 can change this design; revisit the document after it.

Revised 2026-11-14: the maintainer chose to implement steps 2 to 5 first,
build-verified only, and to test on the console afterwards. The site
classification of step 1 is done as part of that work; the memory and
frame-time measurement is still owed.

## Open questions for the maintainer

- There is no pass/fail frame rate yet; it waits for the first console
  measurement.

## Decided after the first revision

- **Performance fallback (decided):** if two views of a solo level are too
  heavy for the console, co-op may reduce draw distance and other render
  costs (resolution, anti-aliasing). Nothing is implemented until the
  console measurement says it is needed, and which lever first is chosen
  from that measurement.
- **Pause (decided):** in co-op, Start does not pause the game; the watch
  opens on the presser's view while play continues. Fallback if that needs
  more than one-line hooks: both players are paused. `GE_COOP_PAUSE=1`
  restores pause-both for comparison.
- **Abort and restart (call):** either player may abort or restart the
  mission from the watch.

## Left to the builder

- Exact hook placement and the final site list.
- Countdown display (text line, as the existing HUD messages).
- Second-player spawn offset.
- Per-level fixes that the full pass turns up.

## Verification

- Co-op off: the existing golden-frame comparison stays unchanged.
- Co-op on: tested on the console by the maintainer, level by level; the
  second controller cannot be driven from the emulator harness.
