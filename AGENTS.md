# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **Read `docs/PCPortResearch.md` first** — architecture, GE-specific RSP deltas, phased plan, and review findings (A1–C2 in §11).

## Non-negotiables

1. **N64 build untouched.** `Makefile`, `tools/`, `rsp/`, `ld/` belong to the N64 build. Never modify them for the PC port.
2. **Game code compiles unmodified.** `src/game/*.c` + the curated `src/*.c` list compile as-is for the host. All N64 hardware dependencies are satisfied by the `port/` layer. If a game file seems to need a change, stop — the fix belongs in `port/`.
3. **Region macros mirror the Makefile.** `CMakeLists.txt` `REGION_DEFS` must match the N64 Makefile's per-region macro set exactly (finding A1). Divergence = silent branch divergence + link failures.
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles: `libultra/audio`, `libultrare/audio` (drvrNew/env/reverb), `libultra/gu`, and `libultrare/io/vitbl.c` only. All other `io/` + `os/` files are excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** — the authoritative reference for which GBI commands GE emits (modified fast3d, 1545 lines). We do not run it on PC; `port/fast3d/` replaces it. Use it to validate the software RSP's command decoding and the custom CC/RM modes.

## Critical files

| File | Role |
|---|---|
| `docs/PCPortResearch.md` | Research + plan + findings (A1–C2). Read first. |
| `CMakeLists.txt` | PC build (parallel to the N64 Makefile). Source list + `REGION_DEFS` live here. |
| `port/src/` | Shims: `libultra.c` (OS API), `gesched.c` (scheduler), `n64stubs.c` (boot/TLB/FPU/rmon), `random.c` (PRNG ported verbatim from `random.s`), `ucode.c` (microcode segment markers), `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). The main Phase 2 work. |
| `rsp/graphics/gmain.s` | GE's RSP ucode — ground truth for GBI/CC/RM. |
| `PD_PORT_CHECKOUT` | Perfect Dark PC port checkout — ground truth for the port layer. Its `port/fast3d/` is the starting point for ours. |

## Build

```sh
./build-pc.sh ntsc-final   # or pal-final / jpn-final
```

Needs CMake + SDL2 + zlib + OpenGL (MSYS2 packages: see `build-pc.sh` header). ROM goes in `./data/` (not distributed).

## Verification ritual (after any build-affecting change)

1. **Undefined symbols.** Every symbol referenced by the compiled set (see `CMakeLists.txt`: `SRC_GAME`, `SRC_ENGINE`, `SRC_LIBAUDIO`, `SRC_LIBULTRARE_AUDIO`, `SRC_LIBULTRARE_DATA`, `SRC_GU`, `SRC_PORT*`) must be defined exactly once in the compiled set or in `port/`. Symbols that live in EXCLUDED files (`libultra/io/*`, `libultrare/io/*` except `vitbl.c`, `libultra/os/*`, `libultrare/os/*`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) must be provided by `port/src/libultra.c`, `gesched.c`, `n64stubs.c`, `random.c`, or `ucode.c`.
2. **Duplicates.** No symbol defined twice across the compiled set (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers).
3. **Syntax.** Every touched file must parse; once the toolchain is installed, `./build-pc.sh` is the final word.

Run `/linkcheck` for this sweep. Record new findings in `docs/PCPortResearch.md` §11 style (next label after the last used).

## Phase status

- **Phase 0 (scaffolding):** done, committed.
- **Compile+link milestone:** all ~235 TUs compile and `ge007.x86_64.exe` links clean (`ninja ge007 -k 0`, 236/236). Findings D7–D17 in `docs/PCPortResearch.md` §11; asset stubs in `port/src/assetstubs.c`, fast3d no-ops in `port/src/gfxstub.c` (delete when real fast3d lands).
- **Phase 1 (boot to window):** remaining — ROM loading in `romdata.c` (currently a TODO), run the binary, wire gfx stubs to a real window.
- **Phase 2 (rendering):** bring in PD fast3d (replaces `port/src/gfxstub.c`), verify custom CC/RM modes against `gmain.s`, port the scheduler; replace asset stubs with ROM-backed data (D16).
- **Phase 2 (rendering):** bring in PD fast3d, verify custom CC/RM modes against `gmain.s`, port the scheduler.
- **Phase 3 (audio + input):** libaudio → SDL; decide the ASP strategy (C2: `aspMain*` dummies in `ucode.c`).
- **Phase 4 (saves + polish):** file-backed EEPROM; PFS/motor already no-op stubs.
