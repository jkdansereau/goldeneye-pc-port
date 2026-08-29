# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **`docs/PCPortResearch.md` is the reference** — architecture, GE-specific RSP deltas, phased plan (§1–§10), and the Dxx finding log (§F + §H, indexed at the top of §F). Read the section you need; don't linear-read it. Current task lives in `docs/HANDOFF.md`.
- **Starting a fresh session?** Read `docs/HANDOFF.md` — the paste-ready brief with the immediate task, standing procedure, and environment gotchas.
- **Dispatching subagents?** `docs/AGENT-WORKFLOW.md` — task budgets/deadlines, file partitioning, pre-flight, the standard brief template. Every investigation subagent reads `docs/PORT-LEARNINGS.md` (recurring bug classes) first and appends to it.

## Non-negotiables

1. **N64 build untouched.** `Makefile`, `tools/`, `rsp/`, `ld/` belong to the N64 build. Never modify them for the PC port.
2. **Game logic is unmodified.** The decomp's control flow and behavior are ground truth for 1:1 fidelity — never change them. All N64 *hardware* dependencies are satisfied by the `port/` layer; if a game file seems to need a behavioral change, stop — the fix belongs in `port/`. **Narrow exception (ABI/layout only):** the 32→64-bit transition forces a small class of mechanical, semantics-preserving edits that cannot be isolated in `port/` — chiefly pointer-width reconciliation in ROM-serialized structs (a struct with a 32-bit-pointer field misaligns when read as 64-bit). These follow the PD ground-truth pattern (store the embedded address as `u32`, cast to a real pointer at the use site), change no logic or behavior, and are each documented in `docs/PCPortResearch.md` §F/D3x. No other game-code edits are permitted.
3. **Region macros mirror the Makefile.** `CMakeLists.txt` `REGION_DEFS` must match the N64 Makefile's per-region macro set exactly (finding A1). Divergence = silent branch divergence + link failures.
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles: `libultra/audio`, `libultrare/audio` (drvrNew/env/reverb), `libultra/gu`, and `libultrare/io/vitbl.c` only. All other `io/` + `os/` files are excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** — the authoritative reference for which GBI commands GE emits (modified fast3d, 1545 lines). We do not run it on PC; `port/fast3d/` replaces it. Use it to validate the software RSP's command decoding and the custom CC/RM modes.

## Critical files

| File | Role |
|---|---|
| `docs/PCPortResearch.md` | Research + plan (§1–§10) + Dxx finding log (§F/§H, indexed at top of §F). Reference, not a linear read. |
| `CMakeLists.txt` | PC build (parallel to the N64 Makefile). Source list + `REGION_DEFS` live here. |
| `port/src/` | Shims: `libultra.c` (OS API), `gesched.c` (scheduler), `n64stubs.c` (boot/TLB/FPU/rmon), `random.c` (PRNG ported verbatim from `random.s`), `ucode.c` (microcode segment markers), `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). The main Phase 2 work. |
| `rsp/graphics/gmain.s` | GE's RSP ucode — ground truth for GBI/CC/RM. |
| `PD_PORT_CHECKOUT` | Perfect Dark PC port checkout — **standing reference for this project**: consult it whenever a remaining work item has a PD analogue (same Rare engine family). Port-layer ground truth (`port/fast3d/`, crash/system/video), plus copy candidates: `port/src/preprocess/` (N64→PC asset conversion at load; `filemodel.c` is the D43 near-analogue, same vma 0x5000000) and `mixer.c`/`input.c`/`fs.c` (Phase 3/4 — our stubs were scaffolded to be replaced by these). Caveats: port-layer files only; same family ≠ identical format — validate per field. Full audit: `docs/PCPortResearch.md` §2.4. |

## Build

```sh
./build-pc.sh ntsc-final   # or pal-final / jpn-final
```

Needs CMake + SDL2 + zlib + OpenGL (MSYS2 packages: see `build-pc.sh` header). ROM goes in `./data/` (not distributed).

## Verification ritual (after any build-affecting change)

1. **Undefined symbols.** Every symbol referenced by the compiled set (see `CMakeLists.txt`: `SRC_GAME`, `SRC_ENGINE`, `SRC_LIBAUDIO`, `SRC_LIBULTRARE_AUDIO`, `SRC_LIBULTRARE_DATA`, `SRC_GU`, `SRC_PORT*`) must be defined exactly once in the compiled set or in `port/`. Symbols that live in EXCLUDED files (`libultra/io/*`, `libultrare/io/*` except `vitbl.c`, `libultra/os/*`, `libultrare/os/*`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) must be provided by `port/src/libultra.c`, `gesched.c`, `n64stubs.c`, `random.c`, or `ucode.c`.
2. **Duplicates.** No symbol defined twice across the compiled set (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers).
3. **Syntax.** Every touched file must parse; once the toolchain is installed, `./build-pc.sh` is the final word.

Run `/linkcheck` for this sweep. Record new findings in `docs/PCPortResearch.md` §F/§H style (next Dxx label after the last used) and add the label to the §F index.

## Phase status

- **Phase 0 (scaffolding):** done, committed.
- **Compile+link milestone:** all ~235 TUs compile and `ge007.x86_64.exe` links clean (`ninja ge007 -k 0`, 236/236). Findings D7–D17 in `docs/PCPortResearch.md` §11; asset stubs in `port/src/assetstubs.c`, fast3d no-ops in `port/src/gfxstub.c` (delete when real fast3d lands).
- **Phase 1 (boot to window):** done — ROM loads/validates/maps at cart base 0x10000000 (`romdata.c`), SDL2 window + GL clear loop (`video.c`), absolute asset symbols from `scripts/gen_romassets.py` → `port/src/romassets_<r>.s` (findings D18–D23).
- **Phase 1.5 (boot to first frame):** DONE — boot chain complete AND real frames render (`[NOTE] frame N rendered in … us`). D34–D42 closed the blockers on top of D31–D33: ANIM_DATA lvalues, music seq-table ABI+endianness, music-heap sizing, libaudio bank-tree re-layout, implicit-declaration prototype shim (D38), Globalimagetable rebasing + true segment size (D39), ModelHitEntry pool sizing (D40), cross-thread GL context release (D41), rsp task-settings toggle (D42). ROM dump verified against the No-Intro good dump (byte-identical, header CRCs re-checked — `tools_pc/romverify.c`).
- **Phase 2 (rendering):** IN PROGRESS. fast3d integrated; the **entire
  intro renders** (logos → gun barrel → cast). BUNKER1 (`-level_09`)
  **loads, renders recognisably** (textured rooms, skeletal guards) and
  **survives a firefight** crash-free. Now pivoted **breadth-first** —
  see `docs/PLAN-linear-level-sweep.md` (the plan of record): make
  boot→menu→level-select functional, then sweep all 21 solo levels for
  load+render+no-crash.
  - **Cross-level blocker:** D88.4 — `Usetup*Z` `propDefs` record stream
    not byteswapped by `tools_pc/d88_emit.py` (crashes in `setupDoor`).
  - **Cosmetic defects are parked** in `docs/GRAPHICS-BACKLOG.md`
    (D75 3D model transforms, D76 disclaimer, D77 audio, D114/D116 text
    X-mirror). Below crash/level work.
  - **Recurring sub-task:** ABI/layout fixes per the D32 procedure
    (§H) as new asset types are touched — the classes are catalogued in
    `docs/PORT-LEARNINGS.md`.
  - Full finding history: `docs/PCPortResearch.md` §F + §H (D31–D121,
    indexed at top of §F); session narrative in `docs/HANDOFF-ARCHIVE.md`.
- **Phase 3 (audio + input):** input layer DONE (D118 — `port/src/input.c`, SDL keyboard/mouse/gamepad → N64 pad; polish bugs D118a/b open). Audio remains: libaudio → SDL, copy-and-adapt from PD (`mixer.c` 722 lines vs our 31-line stub; §2.4); decide the ASP strategy (C2: `aspMain*` dummies in `ucode.c`).
- **Phase 4 (saves + polish):** file-backed EEPROM — build on PD's `fs.c` (file-backed save dir); PFS/motor already no-op stubs.
