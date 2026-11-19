# D601

<!-- from the old section-F/H table row -->
| D601 | **Long-session hang at stage load (macOS playtest, Archives): the active sound-state list (`D_800243E4.node`) became cyclic, so `sndSetSfxSlotVolume`'s walk in `lvlStageLoad` never ends (game thread live-spins, audio thread contends, watchdog fires, black screen). Cause: unsynchronised list mutation between the game thread and amMain — `sndSetupSound` read the free-list head BEFORE taking the interrupt section, and `sndUnlinkClearSound` mutated both lists with NO section.** — full `## D601` entry at file tail | **FIXED + LIVE-CONFIRMED 2026-10-08** (macos branch; `#ifdef PORT` guard, D152/D285 class; evidence = live lldb read of the hung process; maintainer full-campaign completion incl. Archives, no hang) |

## D601: cyclic sound-state list at stage load (unsynchronised sndSetupSound / sndUnlinkClearSound) — FIXED 2026-10-08 (macos branch; live confirmation owed)

**Symptom.** Maintainer's ~1 h macOS playtest (225,697 rendered frames, flat 60 fps on
D600): loading Archives gave a permanent black screen. Log: `D152: osSetIntMask lock
stuck >2s -- stealing from owner=<mainThread> depth=3`, then `kernel heartbeat: no frame
rendered for 8002/13018/18035 ms` (frames frozen at 225697), `D204 audio queue full`.

**Evidence (live process, lldb via Terminal + `sample`).** The game thread was inside
`lvlStageLoad -> sndSetScalerApplyVolumeAllSfxSlot -> sndSetSfxSlotVolume` (snd.c walk),
spinning on `alEvtqPostEvent`; amMain contended in `sndPlayerVoiceHandler ->
alEvtqNextEvent`. Reading the list from the hung process (`scratch/ldb_hang.py`):
`g_sndAllocatedVoicesCount = 3`, but the active list `D_800243E4.node.next` is a 4-node
**cycle** (the tail's `next` points back at the first node instead of NULL), so
`while (item != NULL)` never terminates. `s_imHeld/Owner/Depth` = 0/0/0 at capture (the
2 s steal had already reshuffled ownership: consequence, not cause).

**Cause.** The port runs the N64 audio manager (amMain) as a real preemptible thread; the
N64 relied on interrupt masking, emulated by the global recursive `osSetIntMask(OS_IM_NONE)`
section. Two list mutators were not covered by it:
1. `sndSetupSound` (game thread via `sndPlaySfx`) read the free-list head
   `g_sndPlayerSoundStatePtr` BEFORE taking the section, so a concurrent free (below) made
   it pop a stale node and relink one state into the active list twice -> cycle.
2. `sndUnlinkClearSound` (amMain, via `sndDisposeSound`) unlinked from the active list and
   pushed onto the free list with NO section at all; the game thread's guarded code
   therefore had nothing to exclude. (Same one-sided-exclusion class as D152
   `sndSetSfxSlotVolume` and D285 the preempt scan.)
Rare (needs a play-SFX on the game thread to land inside a dispose on amMain), hence ~1 h.

**Fix.** `src/snd.c`, both under `#ifdef PORT`, concurrency-correctness only (N64 build
byte-identical): `sndSetupSound` takes the section before reading the free-list head and
releases it before returning; `sndUnlinkClearSound` runs its whole body inside the section
(recursive, so nested callers are unaffected). **Rule-2 bucket call (flag for the
maintainer):** treated as the D152/D285 precedent class (add the missing OS_IM_NONE guard
that the port's preemptive audio thread needs; no logic change), not a bucket-(c) behaviour
change, so no sign-off was requested; veto = revert the two guards.

**Verification.** Windows build clean. Mac M3 (rebuilt with the guards): scripted Dam fire 6000 frames,
Archives 3500 and Cradle 3500 frames all rc 0, no crash log, 0 `D152` lines, 0 watchdog
heartbeats (smoke only; the race is rare). A deterministic repro does not exist; the confirm is
the next long playtest (no `D152 ... stealing` line, no heartbeat). Stage-load robustness
alternative not taken: a bounded walk / cycle guard in `sndSetSfxSlotVolume` would hide any
future corruption rather than prevent it.

**Status: FIXED 2026-10-08 (macos branch; LIVE-CONFIRMED 2026-10-08 (maintainer completed the full campaign on Agent, Archives among the levels — the stage-load hang never recurred; no `ge007.crash.log` in the session)).**
