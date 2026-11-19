# D477

<!-- from the old section-F/H table row -->
| D477 | **CI8 palette re-imports measured and ruled out as a CPU-side hitch cause: decode+upload averages ~0.008 ms/frame (worst frame 0.44 ms); the cache already holds the animated palettes (1314 misses, 10 re-misses); one CI8 texture is drawn with several palettes per frame, so an in-place update path would thrash** — full `## D477` entry at file tail | MEASURED 2026-10-02 (local); premise of the D475/D476 follow-up refuted on the dev box; GPU/driver-side cost unmeasured |

## D477 — CI8 palette re-imports measured: not a CPU-side hitch cause (D475/D476 follow-up premise refuted)

**Question:** D475/D476 named CI8 textures with game-animated palettes as the main remaining source of the Statue/Cradle hitches and proposed a GPU palette lookup or an in-place `glTexSubImage2D` update path.
**Probe:** `GE_CI8PROF=1` on branch `probe/ci8-churn` (TEMP, never merge): per-CI8-bind counters in `import_texture` (binds, misses, distinct palette hashes, re-misses of an already-seen palette, same-frame binds with a different palette) and timers around the miss path (decode vs `upload_texture`), plus the worst CI8 frame per 300-frame window.
**Run:** Statue `-level_22`, maintainer's ini (1600x900, MSAA 8x, `TextureFilter = 2`, DD 400, FOV 135, vsync), `GE_INPUTSCRIPT="200:SLEFT;1000:SNONE;1050:SRIGHT;1800:SNONE;1850:SUP;2100:SLEFT;2400:SNONE"`, `GE_QUITFRAME=2420`, with `GE_PERFSTAT=1`; RTX 5090 dev box; rc=0, ini/eep restored. One run.
**Results:**
- CI8 miss path costs ~0.008 ms/frame on average in the busiest window (frames 300-900, ~1.9 misses/frame); the worst single frame spent 0.44 ms on CI8 imports. Non-CI8 misses are smaller still. CPU-side, this cannot make a gfx task ~15 ms late.
- The cache absorbs the animation: 1314 CI8 misses over 153 textures, only 10 re-misses of a palette already seen (the top texture `0x701a2920`, 32x48: 137 misses, 134 palettes, 3 re-misses). Misses fall to 0 after ~frame 1800.
- The same CI8 texture is bound with different palettes within one frame in most frames (`0x70155ac0`: 1902 of 2400 frames; `0x701a2920`: 8166 same-frame palette switches). An in-place single-texture update path would therefore re-upload every frame; **do not build it**.
- Spikes: 3 frames > 33 ms in 2420 (frames 3, 459, 762), each with render-thread `dl`/`run` 0.2-0.6 ms; none coincides with the worst CI8 frame (577).
**Not measured:** driver-deferred work (first-draw allocation of a new texture, `glGenerateMipmap` forced by the three-point filter on every upload) shows up neither in these CPU timers nor in the averaged GPU timer. The D475 association between the bursts and palette re-imports was temporal, not a measured cost.
**Conclusion:** GPU palette lookup / in-place update are not justified by this data. Next step if the hitches still matter: a per-frame timeline (game-thread build + limiter decision, render-thread misses/new textures/shader compiles, per-frame GPU timestamps, swap time) over >=5 runs, PresentMon for what reaches the screen, and a disable-falsifier per suspect. Low-end hardware may weigh these costs differently (a `GE_PERFSTAT` run there is still owed, ROADMAP §4).
