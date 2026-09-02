# Audio Plan — Phase 3 (libaudio → SDL)

Status: **draft for evaluation** (written read-only; no code changes made).
Supersedes the audio paragraphs of §6 in `docs/internals.md` as the plan of record
for closing **D77** (no audible music/sound on PC). Companion docs: `docs/HANDOFF.md`,
`docs/porting-notes.md`.

---

## Contents

1. [Goal and scope](#1-goal-and-scope)
2. [Current state](#2-current-state-verified-by-reading-not-assumption)
3. [The GE pipeline in one paragraph](#3-the-ge-pipeline-in-one-paragraph)
4. [How the PD port does it](#4-how-the-pd-port-does-it-ground-truth-comparison)
5. [Architecture options](#5-architecture-options-evaluate-all-recommendation-marked) — options A/B/C + sub-decisions A1–A3
6. [DMEM layout](#6-dmem-layout-ido-from-srclibultraaudiosynthinternalsh5771)
7. [Shims and ABI fixes required](#7-shims-and-abi-fixes-required) — incl. [7.1 `K0_TO_PHYS` bug](#71-bug-found-k0_to_phys-is-unshimmed-and-corrupts-pointers-on-pc)
8. [Phase 0 — recon](#8-phase-0--recon-do-first-informs-everything)
9. [Open questions](#9-open-questions-decide-before-phase-1)
10. [Phases and milestones](#10-phases-and-milestones)
11. [Verification ritual (audio-specific)](#11-verification-ritual-audio-specific-on-top-of-the-standard-one-in-agentsmd)
12. [Risks / unknowns](#12-risks--unknowns)
13. [Key references](#13-key-references)

---

## 1. Goal and scope

Make GoldenEye's audio audible on the PC port with 1:1 fidelity to the N64 pipeline:
music (3 cseq tracks) and SFX through the real libaudio synth, out of SDL2.

In scope:
- Executing the per-frame **Acmd stream** that stock IDO libaudio emits (the part that
  ran on the RSP under GE's `aspMain` microcode).
- SDL audio device + queue plumbing (`port/src/audio.c`, AI shims in `libultra.c`).
- One latent ABI fix in the port shim headers (`K0_TO_PHYS`, §7.1).

Out of scope:
- MP3 playback (naudio-only, PD-specific; GE never emits it).
- Input (done — D118), saves (Phase 4).
- Any change to game logic. All work lives in `port/` except the narrow, documented
  shim-header exception in §7.1 (same class as D78).

## 2. Current state (verified by reading, not assumption)

Working already:
- **The whole CPU side of libaudio runs.** `src/libultra/audio/*` +
  `src/libultrare/audio/{drvrNew,env,reverb}.c` compile and execute; the audio thread
  (`amMain`, `src/audi.c`) gets retrace messages from the real scheduler (`src/sched.c`,
  compiled — Phase 2 decision), calls `alAudioFrame()`, which fills a 3000-word Acmd
  list and posts an `M_AUDTASK` to the scheduler. Bank trees, cseq fixups (D54, D36/D37)
  are done. Sample DMA from cart works via `amDmaCallback` → `osPiStartDma`
  (synchronous ROM-image shim in `libultra.c`).

Dead (the three gaps):
1. **Acmd list never executed.** `port/src/libultra.c:1108` `osSpTaskStartGo()` has an
   explicit Phase-3 no-op branch for `M_AUDTASK`. The output buffer (`info->data`) is
   never filled.
2. **AI shims are stubs.** `libultra.c:794–810`: `osAiSetNextBuffer` discards the
   buffer, `osAiGetLength` returns 0 (so the per-frame sample-count feedback loop in
   `amHandleFrameMessage`, `audi.c:527`, runs blind).
3. **No SDL device.** `port/src/audio.c` `audioInit()` is a TODO stub; and nothing
   calls `audioEndFrame()` (zero callers repo-wide).

Existing PORT guards to revisit when audio lands:
- **C7 / D127** (`src/snd.c:859`): skips SFX whose resolved `ALSound*` pointer is out of
  range — added because the converted bank tree (`romdata.c afFixupInst`) yields fewer/
  rearranged `soundArray` slots than the N64 image on some levels (Surface1). May be
  masking a real bank-tree defect. See **Q1**.
- `src/audi.c:365`: `#if defined(PORT)` memcpy of `CUSTOM_FX_PARAMS_N` (C++-style
  initializer workaround) — harmless, keep.

## 3. The GE pipeline in one paragraph

Audio thread on each retrace: `amHandleFrameMessage()` (`src/audi.c:497`) hands the
*previous* frame's output to AI via `osAiSetNextBuffer(lastInfo->data, …)`, computes
this frame's sample count from `g_FrameSize - osAiGetLength()>>2` (clamped to
`g_MinFrameSize`), calls `alAudioFrame()` which walks the filter chains (decoder →
resampler → envmixer → [FX: GE's custom chorus/reverb] → aux bus → main bus → save)
emitting **Acmd words** into a 3000-word list, then posts an `M_AUDTASK` (ucode =
`aspMain`). The real scheduler (`sched.c:439,485`) queues it in the audio list and runs
it through `osSpTaskLoad/osSpTaskStartGo`. On the N64 the RSP executes the Acmd stream
against 4 KB of DMEM, writing interleaved s16 stereo to `info->data`; AI DMA then plays
it at 22050 Hz. On PC we must replace "RSP executes Acmd stream" — that is the entire
problem.

**Opcode inventory** (grep-verified across `src/libultra/audio/*.c` +
`src/libultrare/audio/*.c` — 15 ops):

| Op | Emitted by | PD reference code? |
|---|---|---|
| `aSetBuffer` (×20) | load, mainbus, resample, save, reverb | yes (`a*Impl` pattern; different arg ABI) |
| `aMix` (×7) | mainbus, reverb | yes — `aMixImpl` |
| `aLoadBuffer` (×7) | load, reverb | yes — `aLoadBufferImpl` |
| `aClearBuffer` (×7) | auxbus, mainbus, load, reverb | yes — `aClearBufferImpl` |
| `aSetVolume` (×5) | env.c (Rare envelope) | yes — `aSetVolumeImpl` |
| `aSaveBuffer` (×4) | save, reverb | yes — `aSaveBufferImpl` |
| `aDMEMMove` (×4) | load, resample, reverb | yes — `aDMEMMoveImpl` |
| `aResample` (×2) | resample.c, reverb.c | yes — `aResampleImpl` (SSE4.1/NEON/scalar) |
| `aLoadADPCM` (×2) | load.c, reverb.c | yes — `aLoadADPCMImpl` |
| `aSetLoop` | load.c:479 | yes — `aSetLoopImpl` |
| `aSegment` | synthesizer.c:231 | n/a (no-op on CPU) |
| `aPoleFilter` | **reverb.c:402 (GE custom FX)** | **NO — PD's stub is empty ("never gets called?")** |
| `aInterleave` | save.c | yes — `aInterleaveImpl` (naudio variant takes no args) |
| `aEnvMixer` | env.c:410/414 | yes — `aEnvMixerImpl` |
| `aADPCMdec` | load.c:483 | yes — `aADPCMdecImpl` (SSE4.1/NEON/scalar) |

PD reference = the Perfect Dark port `port/src/mixer.c` +
`port/include/mixer.h`. Reusable: the DSP *math* (ADPCM 8-coefficient loop filter,
resample table + interpolation, envelope ramping, clamps). **Not** reusable as-is:
signatures and argument conventions (naudio ABI ≠ IDO ABI, §5.3).

## 4. How the PD port does it (ground-truth comparison)

1. naudio is Rare's own library; its `n_abi.h` has a `#ifndef PLATFORM_N64` branch that
   redefines every `n_a*` macro as a **direct C call** into `port/src/mixer.c`
   (`mixer.h` `#undef`s the macros and redirects to `a*Impl`). All DSP therefore runs
   **inline on the audio thread at task-build time**, inside `amgrFrame()`.
2. The `M_AUDTASK` is an empty shell: `pdsched.c:233` receives it with the comment
   `// send this into the mixer` and an empty body.
3. PD **replaced the scheduler** (`pdsched.c`) and drives audio from retrace:
   `schedAudioFrame()` → `amgrFrame(); audioEndFrame();` (×`diffframe60`). Same thread
   for build and flush ⇒ no locking in their SDL handoff.
4. SDL device layer: queue-based, exactly what our scaffold already copied —
   `osAiGetLength` = queued **bytes** (`audioGetBytesBuffered()`),
   `osAiSetNextBuffer` = stash pointer, `audioEndFrame` = `SDL_QueueAudio` with a
   `queueLimit` backpressure drop. Device opened at 22020 Hz (their TODO admits this
   "might cause trouble" — it is a silent −9-cent detune vs their 22050 synth rate).
5. DMEM: ~3 KB static buffer, **naudio-specific** offsets (main L/R @0x4E0/0x650,
   aux @0x7C0/0x930) — different from IDO's layout (§6).
6. PD-only extras: `aPlayMP3Impl` (minimp3; GE never emits), `aPoleFilterImpl` empty
   stub (GE *does* emit it).

**Why we can't copy it wholesale:** GE compiles the real `src/sched.c` and stock IDO
libaudio, whose macros only *write words* into the Acmd list — there is no build-time
hook unless we shadow `PR/abi.h`. Something must execute the stream. Two further ABI
facts:

- **IDO's implicit "current buffer":** e.g. `aADPCMdec(pkt, f, s)` carries only flags +
  a state pointer; in/out DMEM offsets and sample count come from the preceding
  `aSetBuffer` (`load.c:482–483`, `resample.c:90–91`). naudio spells everything out per
  call. Whatever option we pick, the implementation must model current-buffer state.
- **Addresses in the stream are RAM pointers** (`K0_TO_PHYS(f->state)`,
  `osVirtualToPhysical(e->state)`, `f->dramout`) — resolvable on PC *except* for the
  `K0_TO_PHYS` bug in §7.1.

## 5. Architecture options (evaluate all; recommendation marked)

### Option A — CPU-side Acmd interpreter at task time 【RECOMMENDED】

Decode the 64-bit command words from `t->t.data_ptr` inside the existing `M_AUDTASK`
branch of `osSpTaskStartGo()` (`libultra.c:1108`), dispatching each op to an
implementation in `port/src/mixer.c` operating on a static 4 KB "DMEM".

- Pros: zero game/header/build changes; treats the stream as data exactly like fast3d
  does with GBI (project-consistent); natural logging hook (`GE_DAUDIO=1` dump of op +
  args per frame) for debugging and for validating against the microcode disassembly;
  execution point matches N64 timing (DSP runs when the RSP would have run it).
- Cons: must write the word decoder (~50 lines, trivial — op is bits 24–31 of w0 per
  `include/PR/abi.h`) and the current-buffer state machine; one scheduler tick of extra
  latency vs build-time execution (inaudible).

### Option B — Shadow `PR/abi.h`, redirect macros to direct C calls (the PD trick)

Add `port/shim/PR/abi.h`: `#include "include/PR/abi.h"` then `#undef` the 15 `a*`
macros and `#define` them as direct calls into `mixer.c` (PD's `mixer.h` does exactly
this for naudio). DSP then runs inline during `alAudioFrame()` on the audio thread; the
`M_AUDTASK` branch stays a no-op, exactly like PD.

- Pros: battle-tested pattern (shipped in PD); no decoder; slightly less latency.
- Cons: shadows a core PR header for every TU that includes it (only libaudio uses these
  macros on PC, so blast radius is small but nonzero — must verify include-path
  ordering in `CMakeLists.txt`); stream becomes invisible at runtime (no logging hook —
  you'd instrument inside each Impl); the shim must exactly mirror IDO arg order, which
  differs from PD's naudio redirects line-by-line.

### Option C — Run the real `aspMain` microcode 【NOT RECOMMENDED】

Point `aspMainTextStart/DataStart` (`port/src/ucode.c`) at the ROM microcode and execute
it with an RSP emulator. Maximum fidelity by construction, but requires a 16-bit-MIPS
RSP core + DMEM + interrupt model for one component whose entire job is the 15 ops we
can implement directly. Keep the `ucode.c` dummies permanently (their header comment
anticipates this decision). The microcode is still *used* — as a **reference to
disassemble**, not to run (§8, Phase 0).

### Sub-decision A1 — where samples hit the SDL queue 【RECOMMENDED: immediate】

- **Immediate:** `osAiSetNextBuffer()` shim calls into `audio.c` and `SDL_QueueAudio`s
  right there (on the audio thread), with the existing `queueLimit` backpressure drop.
  Lock-free (single writer); matches the moment N64 AI DMA starts; `audioEndFrame()`
  disappears entirely. Safe because `NUMBER_OUTPUT_BUFFERS=3` rotation guarantees the
  handed-off buffer isn't rewritten before it is queued.
- **Deferred (PD-style):** stash in `osAiSetNextBuffer`, flush from a frame-end hook.
  In GE this crosses threads (audio thread sets; scheduler/gfx path would flush) ⇒ needs
  a mutex or accepts PD's unlocked pattern, and needs a new `audioEndFrame()` call site
  that doesn't exist today. Only worth it if immediate queueing shows a problem.

### Sub-decision A2 — device sample rate 【RECOMMENDED: 22050】

Request exactly `OUTPUT_RATE` (0x5622 = 22050 Hz), stereo s16, as PD's `audio.c`
scaffold already comments. Do **not** copy PD's shipped 22020 (detune). If a platform
rejects 22050, let SDL convert (`SDL_OpenAudioDevice` with allowed-conversion) rather
than changing the requested rate.

### Sub-decision A3 — timing and thread priorities 【RECOMMENDED: inline + host priorities; no worker thread】

Execution stays on the scheduler thread (Option A's execution point). If Phase 2/3
milestones show underruns (`audio: ai out of samples` spam — verification step 3) or
frame-time spikes attributable to synthesis cost, the **first lever is host thread
priorities**: `SetThreadPriority` / `pthread_attr_setschedparam` with the scheduler
thread time-critical (it now owns both fast3d and the mixer) and the tick thread
normal — a ~10-line fix in `libultra.c`. A dedicated audio worker thread consuming
`M_AUDTASK`s is explicitly **not** the next step: it adds a cross-thread handoff for a
problem priorities may fully solve, and the kernel's priority semantics are already
fake (D24 — see the backlogged bug-class entry in `docs/BACKLOG.md`), so any deeper
redesign starts there. Do **not** consider rewriting toward PD's single-threaded
inline-`amgrFrame()` model: it works for them only because they adapted game-side lib
code (direct `schedSubmitTask` calls, gutted `pdsched.c`); for GE it means editing
`src/audi.c` / scheduler submission points — a non-negotiable #2 violation. Rationale
recorded in `docs/BACKLOG.md` (Architecture decision).

## 6. DMEM layout (IDO, from `src/libultra/audio/synthInternals.h:57–71`)

```
AL_MAX_RSP_SAMPLES = 160   (per-chunk; alAudioFrame builds the list in ≤160-sample chunks)
offset 0     AL_DECODER_IN / AL_RESAMPLER_OUT / AL_TEMP_0   (320 B)
offset 320   AL_DECODER_OUT / AL_TEMP_1                     (320 B)
offset 640   AL_TEMP_2                                      (320 B)
offset 1088  AL_MAIN_L_OUT                                  (320 B)
offset 1408  AL_MAIN_R_OUT                                  (320 B)
offset 1728  AL_AUX_L_OUT                                   (320 B)
offset 2048  AL_AUX_R_OUT                                   (320 B)   → max extent 2368 B
```

Static 4 KB buffer in `mixer.c` (round number, headroom). **Not** PD's naudio offsets.
GE's custom FX (`reverb.c`) uses `AL_TEMP_0/1/2` as scratch plus RAM-backed delay lines
via `aLoadBuffer`/`aSaveBuffer` to `r->base` — all plain host pointers on PC.

## 7. Shims and ABI fixes required

### 7.1 【BUG FOUND】 `K0_TO_PHYS` is unshimmed and corrupts pointers on PC

`port/shim/PR/R4300.h` redefines `PHYS_TO_K0` (identity) but **not** `K0_TO_PHYS`.
Stock: `#define K0_TO_PHYS(x) ((x)&0x1FFFFFFF)` (`include/PR/R4300.h:65`). Game RAM
lives at the V1 view `0x70xxxxxx` (`port/src/dram.c`); masking gives `0x10xxxxxx` — an
invalid host pointer. Hit sites: `load.c:483` (`aADPCMdec(..., K0_TO_PHYS(f->state))`)
and `load.c:479` (`aSetLoop(ptr++, K0_TO_PHYS(f->lstate))`) — the mixer would receive
garbage state addresses for every ADPCM voice. Fix: extend the existing shim with
`#undef K0_TO_PHYS / #define K0_TO_PHYS(x) ((u32)(x))` (identity; V1 addresses are live
host pointers). Inert in the N64 build (no `-DPORT`) — same class as the D78 precedent.
Record as a new Dxx finding when implemented (next free label after D127).

### 7.2 AI shims (`libultra.c:794–810`)

- `osAiSetFrequency(hz)` → return `hz` (already does; 22050 flows into
  `alconf->outputRate` and the frame-size math — correct).
- `osAiSetNextBuffer(buf, size_bytes)` → queue to SDL per A1.
- `osAiGetLength()` → **bytes** currently queued (`SDL_GetQueuedAudioSize`); the game
  does `>> 2` for frames (`audi.c:527`). PD returns bytes too. This closes the
  feedback loop that sizes each frame's synthesis.
- `osAiSetConvert` — not called by GE; leave stub.

### 7.3 Misc

- `port/src/audio.c`: fill in `audioInit()` (SDL_InitSubSystem(AUDIO), open device per
  A2, unpause); add the queue function per A1; keep `Audio.BufferSize`/`Audio.QueueLimit`
  config entries; add an `Audio.Enabled` toggle (PD's `g_SndDisabled`) so the level
  sweep can run silent.
- `port/src/mixer.c`: rewrite as the op implementations + (for Option A) the decoder /
  current-buffer state machine. `mixerInit/Destroy` reset DMEM + op state.
- Threading note: mixer runs on the scheduler thread (`sched.c __scMain` →
  `osSpTaskStartGo` inline); with A1 there is exactly one shared object (the SDL queue,
  internally locked) — no port-side locks needed.

## 8. Phase 0 — recon (do first; informs everything)

1. **Disassemble `bin/aspboot.text.bin`** (+ `.data.bin`; markers in `src/aspboot.s`) —
   GE's real ASP microcode, 16-bit MIPS RSP. This is the ground truth for:
   - exact `aSetBuffer` current-buffer semantics (which subsequent ops consume it, and
     how),
   - `aADPCMdec` state layout (what the pointer in w1 points at — IDO `ADPCM_STATE` +
     data addressing),
   - **`aPoleFilter`** — the one op with no PD reference code; implement from this,
   - any DMEM convention not derivable from libaudio usage.
   Methodology mirrors how `rsp/graphics/gmain.s` is used for graphics. A small
   16-bit-MIPS disassembler script (`tools_pc/`) is sufficient — we decode, not execute.
2. **Acmd stream logger** (`GE_DAUDIO=1`, in the Option-A decoder or a temporary probe
   in the `M_AUDTASK` branch): dump op + decoded args per frame for the first N frames.
   Confirms the 15-op inventory at runtime, catches surprises (e.g. ops we didn't grep),
   and gives before/after artifacts for validating the implementation.

## 9. Open questions (decide before Phase 1)

- **Q1 — C7/D127 bank-tree guard (`src/snd.c:859`):** root-cause the bogus
  `ALSound*` resolutions now (likely an `afFixupInst` layout/stride defect in
  `port/src/romdata.c`, same family as D37's bank-tree re-layout), or keep the guard as
  a documented stopgap and file a separate finding? Keeping it means some SFX stay
  silently missing on affected levels (Surface1) even after audio lands.
- **Q2 — Option A vs B** (§5): A is recommended (no header shadowing, stream logging,
  project-consistent with fast3d); B is the PD-proven fallback if current-buffer
  semantics prove fiddly at task time. Note for the evaluator: this is a **low-stakes,
  cheap-to-revisit decision** — both options share 100% of the DSP implementation code
  (§6 layout, all op bodies); only the ~50-line front end (word decoder vs macro
  redirects) and the execution point differ. If Phase 0's disassembly surfaces anything
  that makes B look safer, switching costs an afternoon, not a re-architecture.
- **Q3 — Pole-filter fidelity bar:** implement `aPoleFilter` strictly from the
  microcode disassembly (recommended), or accept a devkit-docs approximation and verify
  by ear? The chorus is audible in most music; a wrong filter coefficient shows as tonal
  difference, not crash.

## 10. Phases and milestones

- **Phase 0 — Recon** (§8): microcode disassembly notes + Acmd log artifacts committed
  to `docs/` (or `tools_pc/`). Exit: current-buffer semantics + pole filter documented;
  op inventory confirmed at runtime.
- **Phase 1 — Plumbing.** SDL device opens @22050 stereo s16 (A2); A1 queue path live;
  real AI shims (§7.2); `K0_TO_PHYS` shim fix (§7.1); `Audio.Enabled` toggle. Mixer still
  a no-op is fine. **Milestone: device drains, `osAiGetLength` feedback loop visible in
  logs, clean start/stop, silence (not crash).**
- **Phase 2 — Core ops.** Implement `aSetBuffer/aLoadBuffer/aClearBuffer/aDMEMMove/
  aADPCMdec/aResample/aEnvMixer/aMix/aSetVolume/aLoadADPCM/aSetLoop/aInterleave/
  aSaveBuffer` (+`aSegment` no-op) in `mixer.c`, reusing PD's DSP math where the ABI
  maps. **Milestone: SFX audible (gunshots, doors); dry music plays.** Expect at least
  one Dxx-class finding here (state-layout or endianness surprise is the usual bug
  class — see `docs/porting-notes.md`).
- **Phase 3 — FX.** `aPoleFilter` + validate the full `reverb.c` chorus path against
  the disassembly. **Milestone: music matches N64 character (chorus/reverb present, no
  pumping); close D77.**
- **Phase 4 — Polish & bookkeeping.** Resolve Q1; remove/adjust temporary probes and
  `GE_DAUDIO` defaults; latency/buffer tuning (`Audio.BufferSize`, `QueueLimit`);
  host thread-priority tuning per A3 *only if* underruns were observed in Phases 2–3;
  record findings in `docs/internals.md` §F/§H (next Dxx after D127) + update the
  §F index, `AGENTS.md` Phase 3 status, `docs/HANDOFF.md`; append recurring bug classes
  to `docs/porting-notes.md`.

## 11. Verification ritual (audio-specific, on top of the standard one in AGENTS.md)

1. Boot → title: menu music audible within a couple of seconds; no click at start.
2. Enter BUNKER1 (`-level_09`): gunfire/door SFX present and roughly level-correct;
   level music plays.
3. No `audio: ai out of samples` spam from `amHandleDoneMessage` (`audi.c:596`) — that
   print is the built-in underrun detector.
4. Run 2–3 levels incl. one with heavy SFX overlap; watch `SDL_GetQueuedAudioSize`
   (log it) staying below `queueLimit` in steady state.
5. PAL build (`pal-final`) spot-check: 50 Hz retrace drives the same path; frame-size
   math uses `MAYBE_FRAME_RATE` — confirm no underruns there too.
6. `Audio.Enabled=0` → fully silent, zero audio CPU cost, nothing else affected.

## 12. Risks / unknowns

| Risk | Mitigation |
|---|---|
| IDO current-buffer semantics misread → subtle wrong output (no crash) | Phase 0 disassembly is the arbiter; Acmd logger + by-ear checks per phase |
| `aPoleFilter` has no PD code | Disassembly (Q3); it's one small IIR — bounded effort |
| ADPCM state layout differs from assumption → garbage/cratchy samples | Phase 0 documents the struct from `load.c` usage + microcode; scalar fallback path makes debugging easy |
| C7/D127 hides a bank-tree bug that audio makes *audible* (wrong SFX, not just missing) | Q1; keep the guard until root-caused |
| SDL queue backpressure drops under load (slow machines) | `queueLimit` drop is graceful (silence gap, no crash); A3: raise scheduler-thread host priority before buffer tuning; PD shipped the same design |
| Synthesis cost on the scheduler thread starves gfx frame time / causes underruns | A3: host thread priorities first; a dedicated audio worker thread only if priorities provably insufficient (and revisit D24's fake-priority semantics before designing one) |
| Cross-region: PAL sample-count math differs | Verification step 5 |

## 13. Key references

| File | What it gives us |
|---|---|
| `src/audi.c` | audio thread, frame-size feedback loop, task construction (`OUTPUT_RATE` @22) |
| `src/sched.c:439,465,485` | real scheduler's M_AUDTASK path → `osSpTaskStartGo` interception point |
| `include/PR/abi.h:260–407` | IDO Acmd word formats (the decoder's spec) + A_* opcodes |
| `src/libultra/audio/synthInternals.h:57–71` | DMEM layout constants |
| `src/libultrare/audio/reverb.c`, `env.c`, `drvrNew.c` | GE custom FX (`alFxNew`), pole filter + chorus, envelope ops |
| `bin/aspboot.text.bin` / `.data.bin` (+ `src/aspboot.s`) | **ground-truth ASP microcode to disassemble** |
| `pd_port/port/src/mixer.c`, `port/include/mixer.h` | DSP math reference + the Option-B pattern |
| `pd_port/port/src/audio.c`, `pdsched.c:233–260`, `libultra.c:150–175` | PD wiring (queue design, empty AUDTASK branch, AI shims) |
| `port/src/libultra.c:794–810, 1108` | the three dead seams this plan fills |
| `port/src/dram.c`, `port/shim/PR/R4300.h` | address model (V1/V2 views) + the K0_TO_PHYS gap |
