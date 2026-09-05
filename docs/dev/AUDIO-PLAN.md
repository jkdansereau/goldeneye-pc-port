# AUDIO-PLAN — Phase 3 / ROADMAP B3 (libaudio → SDL)

Status: plan of record for the audio track, 2026-09-05. Companion to
`ROADMAP-1.0.md` §B3 ("audio (Phase 3)", "audio track parallel from M-50 →
v0.4.0"). Independent of B1/B2 — can start immediately.

---

## Where things stand (verified 2026-09-05)

More is already in place than "audio not started" suggests:

- **The whole CPU-side audio engine already compiles and runs.**
  `src/audi.c`, `src/snd.c`, `src/music.c` are in `SRC_ENGINE`; IDO libaudio
  (`src/libultra/audio/*.c`) + Rare's naudio "New" driver
  (`src/libultrare/audio/*.c`: drvrNew/env/reverb) are in the link. The
  `amMain` audio thread runs per VI-retrace; D54/D54b already killed its
  SIGSEGVs — so synthesis bookkeeping is live (D77: "music runs in code but
  no audible output").
- **The scheduler side needs zero changes.** `osSpTaskStartGo`
  (`port/src/libultra.c:1237`) already has an M_AUDTASK branch that completes
  the task and posts `OS_EVENT_SP`, so `amMain`'s reply-message handshake
  works. The branch is currently a no-op — exactly where the audio ucode's
  job used to go.
- **The two TODO stubs are the actual gap:** `osAiSetNextBuffer()` /
  `osAiGetLength()` in `port/src/libultra.c:852-862` discard everything
  (`osAiGetLength` returns 0), and `port/src/audio.c` never opens an SDL
  device. `main.c:141-142` already calls `audioInit()` / `mixerInit()`.
- **The PD reference is local** at `C:\Users\james\Source\Repos\pd_port`,
  with the full audio stack: `port/src/audio.c` (75 lines, SDL device +
  queue), `port/src/mixer.c` (~720 lines, software RSP-audio-ucode), and the
  wiring trick in `include/PR/abi.h`.

## Architecture — PD's macro-swap trick, adapted

On N64, `alAudioFrame()` only *builds* an acmd list; the RSP audio ucode
(`aspMain`) executes it. PD's PC port never runs that ucode: under
`#ifndef PLATFORM_N64`, `include/PR/abi.h` swaps each acmd-building macro
`aXxx(pkt, ...)` for a call to a CPU implementation (`aXxxImpl`) operating on
a software DMEM array. libaudio/naudio compile unmodified; every "command"
executes inline at build time; the M_AUDTASK that reaches the scheduler is an
empty formality (PD's `pdsched.c` audio branch is literally empty).

**Critical catch — GE and PD use different acmd ABIs** (the AGENTS.md
"same family ≠ identical format" case):

| | GE (`include/PR/abi.h`) | PD (naudio "New") |
|---|---|---|
| `aADPCMdec` | `(pkt, flags, statePtr)` — params from an `aSetBuffer` context + `ADPCM_STATE[16]` | `(pkt, state, flags, count, inofs, outofs)` — inline args |
| `aLoadBuffer` / `aSaveBuffer` | `(pkt, dramAddr)` only — dest/count from the `aSetBuffer` context | explicit dest + count args |
| `aInterleave` | `(pkt, lDmem, rDmem)` | no args |
| Opcodes | 16: adds `aSetBuffer`, `aPan`, `aSegment`; no `aDisable` / `aPlayMP3` | 15 incl. MP3 (PD-specific) |

GE's context model (from the call sites in `src/libultra/audio/`):
`aSetBuffer(flags, dmemin, dmemout, count)` sets a working-buffer context;
subsequent `aLoadBuffer(dramAddr)` / `aADPCMdec(flags, statePtr)` /
`aResample(flags, pitch, statePtr)` operate within it; the tail is
`aInterleave(AL_MAIN_L_OUT, AL_MAIN_R_OUT)` → `aSaveBuffer(f->dramout)`.

So this is **not** "copy `mixer.c`": port PD's mixer as the *DSP algorithm
reference* (ADPCM decode, linear resample, mix math, env-mixer / pole-filter
are the same Rare/Nintendo DSP blocks) and write GE-ABI wrappers that
maintain the `aSetBuffer` context and read params from the state structs.

**GE's full emittable opcode set** (static audit of every call site in
`src/libultra/audio/*.c` + `src/libultrare/audio/*.c`): ClearBuffer,
LoadADPCM, DMEMMove, SetBuffer, LoadBuffer, SetLoop, ADPCMdec, Resample,
Interleave, SaveBuffer, Mix, SetVolume, EnvMixer, PoleFilter, Segment — plus
`aPan`, defined in abi.h but apparently uncalled. PD's mixer covers the DSP
core for all of them except the trivial bookkeeping ones: SetBuffer /
LoadBuffer / SaveBuffer are context tracking + memcpys; Segment is a no-op on
PC (`amDmaCallback` already resolves ROM addresses against the mapped image —
`osVirtualToPhysical` is identity, `libultra.c:1295`).

## Phases

### Phase 0 — Ground truth (research, ~half day, delegable)

1. **Disassemble GE's `aspMain` microcode from the ROM** and document each
   opcode's exact semantics in findings.md §F (new Dxx): flag-bit meanings
   (`A_FIRST`, `A_CONTINUE`, …), count units (bytes vs samples), where the
   interleaved output lands before `aSaveBuffer`, how `aSetLoop` state gets
   restored, `ENVMIX_STATE[40]` layout. This is the audio analogue of using
   `rsp/graphics/gmain.s` as GBI ground truth — we don't run the ucode, but
   it is the spec. (`port/src/ucode.c`'s header already flags this "DECISION
   NEEDED in Phase 3"; CPU-bypass means the dummy `aspMainTextStart` /
   `aspMainDataStart` markers stay forever — document that decision there.)
2. **Opcode inventory probe** (port-only, zero game-code change): a
   `GE_AUDIOTRACE` env var in the existing M_AUDTASK branch of
   `osSpTaskStartGo` that walks `t->t.data_ptr` for `data_size/8` words and
   histograms `w0 >> 24`. Confirms the static call-site audit and catches
   anything emitted dynamically.

### Phase 1 — Device plumbing (small, lead work)

- Implement `port/src/audio.c::audioInit`: SDL device at **22050 Hz** (GE's
  `OUTPUT_RATE = 0x5622`, not PD's 22020), `AUDIO_S16SYS`, stereo. Keep the
  existing `Audio.BufferSize` / `Audio.QueueLimit` config registrations.
- Wire `osAiSetNextBuffer` → `SDL_QueueAudio` guarded by `queueLimit`;
  `osAiGetLength` → `SDL_GetQueuedAudioSize()`. Faithful mapping: N64's
  `AI_LEN_REG` ("bytes synthesized but not yet played") ≙ SDL queued-bytes,
  and the game's self-tuning `frameSamples = g_FrameSize −
  osAiGetLength()>>2` math (`audi.c:527`) keeps the virtual FIFO at ~736
  samples (NTSC) with no game-code changes. Queue inline in
  `osAiSetNextBuffer` (same thread, once per frame) rather than PD's deferred
  `audioEndFrame`. Overflow safety is already self-limiting: a full queue
  makes `frameSamples` clamp to `g_MinFrameSize`, and the `queueLimit` check
  drops rather than crashes.
- Add a `GE_AUDIODEBUG` probe logging queue depth + synthesized samples every
  60 frames; catalog it in `docs/GE-ENV-PROBES.md`.
- **Exit:** links 243/243, `/linkcheck` clean; headless `-level_09` 60 s
  crash-free; debug probe shows the queue filling while intro music "plays"
  (still silent — acmds don't execute yet).

### Phase 2 — The mixer (core work, ~1–3 days)

- Port PD's DSP cores into `port/src/mixer.c`: ADPCM decode (coefficient-book
  handling via `aLoadADPCM`), linear resampler, stereo mix with `0x8000`
  scaling, env-mixer + pole-filter (reverb/chorus driven by
  `CUSTOM_FX_PARAMS_N`, `audi.c:169`), set-volume ramps. Add the GE-ABI
  context layer: a static `{dmemin, dmemout, count}` updated by
  `aSetBuffer`, consumed by LoadBuffer / SaveBuffer / ADPCMdec / Resample; a
  shared software DMEM array across Impls. Replace the current 31-line stub
  interface (`port/include/mixer.h`) with PD-style `aXxxImpl` declarations.
- Edit `include/PR/abi.h` with an `#ifdef PORT` split (N64 path
  byte-identical): PC includes a port `mixer.h` whose macros match **GE's**
  arg arities. This is the one non-`port/` edit — same documented
  ABI/layout-exception class as D78 / D135, logged in §F with its own label.
- Unknown / unused opcodes (`aPan`, `aSegment`): log-and-no-op under
  `GE_AUDIOTRACE`; implement only if Phase 0 shows them live.
- All DSP runs on the existing `amMain` thread inside `alAudioFrame` — no new
  locks, no scheduler changes.
- **Exit:** opcode histogram fully covered; headless run clean; queue probe
  non-zero.

### Phase 3 — Verification & tuning (user-gated listen tests)

- Listen checklist: menu music → briefing → in-level music transitions →
  gunfire / explosions → **positional SFX** (walk around a source) → weapon
  one-shots → level changes → **mission-fail fade** (the D152 `s_imLock`
  scenario — retest killing Trevelyan in Facility with live audio, since the
  fade hammers the event queue; the D152 self-healing lock mitigation is
  already in place but must be re-proven under load).
- Compare reverb / chorus character against N64 reference footage (that's
  what `CUSTOM_FX_PARAMS_N` is for).
- Regression: `GE_PCDUMP` framediff vs goldens unchanged (audio must not
  touch rendering) + 60 s `-level_09` soak.
- Docs: findings §F entry, porting-notes quirk ("classic-libultra vs
  naudio-New acmd ABI delta"), ROADMAP B3 status, HANDOFF update.

## Risks

1. **Ucode-semantics mismatch** (count units, flag bits) → garbled or
   wrong-tinted audio, not crashes; mitigated by Phase 0 disassembly +
   iterative listen checks. Main schedule risk.
2. **Timing coupling with B1** (D193 family): low — audio frame size is
   feedback-driven off `osAiGetLength`, robust to scheduler jitter; re-verify
   after B1 lands.
3. **Performance:** ~736 samples/frame of CPU DSP is trivial on x86-64 (PD
   shipped exactly this).
4. **D152 lock storm under live audio** — covered by the explicit Phase 3
   retest.

## Delegation shape & effort

Per `docs/dev-process.md`:

- Phase 0 disassembly → Claude research subagent (judgment-heavy, read-only +
  findings write-up).
- Phase 1 → lead or mechanical worker (build-verifiable).
- Phase 2 DSP-core port → candidate for the local Qwen once the semantics doc
  exists (mechanical, diff-checkable against PD), with the ABI mapping and
  review kept by the lead.
- Phase 3 listen tests → user.

Total ~3–6 focused sessions. Milestone: ROADMAP "audio track (parallel from
M-50) → v0.4.0".

## Key file map

| File | Role in this plan |
|---|---|
| `src/audi.c` | Game audio manager — compiles unmodified; source of `OUTPUT_RATE`, frame-size math, `CUSTOM_FX_PARAMS_N` |
| `include/PR/abi.h` | GE's acmd accessors (16 opcodes, context ABI) — the one non-`port/` edit (`#ifdef PORT` split) |
| `src/libultra/audio/*.c`, `src/libultrare/audio/*.c` | CPU synth + naudio New driver — compile unmodified; call sites define the opcode set |
| `port/src/audio.c` | SDL device + queue (implement Phase 1) |
| `port/src/mixer.c` | Software acmd interpreter + software DMEM (implement Phase 2) |
| `port/include/mixer.h` | Stub interface → replace with `aXxxImpl` declarations |
| `port/src/libultra.c:848-863` | `osAi*` shims — wire to audio.c (Phase 1) |
| `port/src/libultra.c:1237` | M_AUDTASK branch — stays a completing no-op; hosts `GE_AUDIOTRACE` |
| `port/src/ucode.c` | Dummy `aspMain*` markers stay forever under CPU-bypass — document decision |
| `C:\Users\james\Source\Repos\pd_port\port\src\{audio,mixer}.c`, `include\PR\abi.h` | PD ground truth: device glue + DSP cores + the macro-swap pattern |
