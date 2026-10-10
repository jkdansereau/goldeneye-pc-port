# GE_ diagnostic-probe catalog (keep vs strip before a `main` merge)

The `macos` track added a set of env-gated, diagnostic-only `GE_` probes to
triage the D589/D591/D592 wide-pixel + GL-layer work. All of them are **off
by default** (a `getenv` returns NULL → no code runs) and **change no game
behavior** when unset. The decision owed before any merge toward `main`
(§4, post-1.0) is, per probe: **keep** (a useful shipping debug tool) or
**strip** (diagnostic-only, no shipping value, or a `src/game` edit that is
not a port-layer fix). This file is that catalog — the tracker row is ROADMAP
§7 "GE_ probe catalog."

## The macOS-track texture / ABI probes

| Probe | Where | Gate | What it does | Disposition |
|---|---|---|---|---|
| `GE_TEXDUMP` | `port/fast3d/gfx_pc.cpp` (import) + `port/fast3d/gfx_opengl.cpp` (GL upload) | master, cached `getenv` | Per-import `GE_TEXI`/`GE_TEXN` log lines; `texdump/rNNN_*` normalized bins; GL-level `tNNN_*.ppm` + `.rgba32` (the exact `glTexImage2D` input, D592 Q1). Zero hot-loop cost when unset (the `getenv` is cached once). | **KEEP** — the standing cross-platform texture-triage tool (predates the macOS track; B2/D161). |
| `GE_TEXDUMPROW` | `port/fast3d/gfx_pc.cpp` | sub-gate (needs `GE_TEXDUMP`) | Dumps the FINAL post-transform source bytes keyed by the stable RAW source address (`texdump/row_<rawaddr>_f<fmt>_s<siz>.bin`) — the D592 cross-platform byte-compare key. | **KEEP** — cheap, off by default, and the raw-addr key is the right way to compare a specific texture across platforms (the positional rNNN index is not a stable key). |
| `GE_TEXRAW` | `port/fast3d/gfx_pc.cpp` | sub-gate (needs `GE_TEXDUMP`) | Writes the RAW source bytes handed to the importer (`texdump/rNNN_*.bin`). | **KEEP** — part of the same triage family. |
| `GE_TEXP` | **`src/game/image.c`** | independent `getenv` | D589 L2: logs pool-cursor state after each pool load. | **STRIPPED 2026-10-08** — `image.c` is back to the N64 build in this region; the pool-cursor dump served the D589 L2 cross-check only. |
| `GE_SIZEOF` | `port/src/main.c` | `#ifdef PORT` + `getenv` | Startup ABI-width canary (`sizeof` of key structs/pointers) for a quick 32/64-bit sanity check on a fresh platform. | **STRIPPED 2026-10-08** — the ongoing ABI-width check is the `tools_pc/abi_ratchet.py` CI ratchet; `main.c` also drops the `game/image.h` include it needed. |
| `GE_NPOT_NOGENMIP` (+ companion single-level sampler fix) | `port/fast3d/gfx_opengl.cpp` | `#ifdef PORT` + `getenv`, default off | The D592 NPOT `glGenerateMipmap` guard (skip mips on NPOT textures) + a companion that forced a single-level min filter under the guard. **REVERTED 2026-10-08 (`b80dcd08`)** when **D592 closed as a non-issue**: the "rock striping" was a VHF-metric false-positive (natural rock grain), and the `GE_TEXSTATE` probe showed the sampler state + NPOT mip level counts are byte-identical Win/Mac, so the guard had nothing to fix. | **REMOVED** — no longer in the tree (was `9bed5904` + companion `82dbea9c`). |
| `GE_TEXSTATE` | `port/fast3d/gfx_opengl.cpp` (upload + set_sampler) | `#ifdef PORT` + `getenv`, default off, needs `--debug-gl` (logs at `LOG_NOTE`) | D592 Q2: logged the **effective sampler state** for the bound texture + per-NPOT-shape mip level counts. Confirmed the sampler state **and** the NPOT mip levels are **byte-identical Win/Mac** (the Metal backend builds the full NPOT mip chain; the aniso extension is present on both), which is the core evidence that closed D592. | **STRIPPED 2026-10-08 (`b80dcd08`)** — one-off D592 GL-state diagnostic, removed with the guard when D592 closed as a non-issue. |
| `GE_KEYLOG` | `port/src/video.c` (`videoPumpEvents`) + `port/src/input.c` (`inputComputePadSlot`) | `getenv`, default off (writes `./ge_keylog.txt`) | D594: logs SDL key/mouse-button **events** on the main (host) thread (`E main KEYDOWN/KEYUP/MOUSEBT-…`) and the scheduler-thread **per-frame poll** (`P sched A/W/S/D/LMB/RMB`, change-only) to `./ge_keylog.txt`. The two streams together answer "do the key events reach the game, and does the *different* scheduler thread ever see them?" (the cross-thread poll is the D594 prime suspect). Validated on Windows (known-good input: poll logs the all-zero baseline; event lines fire on real key/mouse press). | **STRIPPED 2026-10-08** — both helpers and their two call sites removed (`video.c`, `input.c`). The D597 headless fire check is now "no SIGSEGV while firing" only (it no longer logs `LMB=1`). |

## Notes
- **Stripping is a mechanical, behavior-neutral edit**: delete the `getenv`
  + the env-gated block; nothing else references the probe (each is self-
  contained). `GE_TEXP`, `GE_SIZEOF`, and `GE_KEYLOG` are the ones still in the
  tree with a real "should not ship" character; the `GE_TEXDUMP` family is the
  kind of triage tool that earns its place. `GE_NPOT_NOGENMIP` (+ its
  companion) and `GE_TEXSTATE` are already gone (`b80dcd08`) with the D592
  close-out.
- The broader `GE_` set (the ~70 vars in `port/`: `GE_QUITFRAME`, `GE_PCDUMP`,
  `GE_RSEED`, `GE_DETERM`, the `GE_OPTIONSOVERLAY_*` UI probes, the `GE_ZF*`
  z-fog family, etc.) is the standing debug-tool set and is **out of scope**
  for this decision — it already ships. Only the **macOS-track additions**
  above need a keep/strip verdict.
- **Ordering:** strip (or consciously keep) `GE_TEXP` + `GE_SIZEOF` +
  `GE_KEYLOG` as part of the §4 post-1.0 macOS merge, *before* the branch goes
  near `main`; leave the `GE_TEXDUMP` family in. (`GE_NPOT_NOGENMIP` +
  `GE_TEXSTATE` are already stripped with the D592 close-out, `b80dcd08`.)
