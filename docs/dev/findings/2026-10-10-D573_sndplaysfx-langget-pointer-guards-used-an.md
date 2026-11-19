# D573

<!-- from the old section-F/H table row -->
| D573 | **`sndPlaySfx` / `langGet` pointer guards used an absolute [64 KiB, 16 GiB) literal; replaced by `portAddrIsMapped()` (DRAM arena views + mapped cart image)** — full `## D573` entry at file tail | FIXED 2026-10-07 (from #108, italoarruda; Windows build + golden: egypt PASS, dam A/B identical to baseline) |

## D573: SFX / text-bank pointer guards test the real address windows (#108) — FIXED 2026-10-07

Reported and first fixed by italoarruda (#108). `sndPlaySfx()` (C7/D127,
`src/snd.c`) and `langGet()` (D129, `src/game/language.c`) both reject a
garbage pointer read out of game data with the literal test
`p < 0x10000 || p >= 0x400000000ULL`. That bound is only right while every
real address sits below 16 GiB. With the arena moved (macOS `PORT_ADDR_BASE`
on the `macos` branch, #95; the D441 above-4-GiB tier) every real `ALSound*`
and text bank fails it, so all SFX go silent and text lookups return NULL.
#108 raised the bound to 0x8000_0000_0000, but that drops the guard's own
documented catch (`0x0000_5622_0001_0001`, below the new bound), and it left
`langGet()` unchanged.

**Fix.** `portAddrIsMapped(const void *)` (declared in
`port/include/portaddr.h`, defined in `port/src/romdata.c`) is true only inside
the two DRAM arena views (`PORT_DRAM_V1_BASE` / `PORT_DRAM_K0_BASE`, each
`PORT_DRAM_SIZE`) or the mapped cart image (`CART_BASE`, ROM + model/bg
sidecars, the same length `romdataCartAddrValid()` uses). The low 64 KiB lies
outside every window, so NULL-ish values are still rejected. Both guards now
call it. It follows the window constants, so a moved arena needs no change
here, and it is stricter than the old literal: an unmapped address under
16 GiB is now rejected too. Every legitimate pointer is inside a window: the
SFX/instrument banks are allocated from the music heap (`music.c`,
`alHeapAlloc` on a mem-pool block) and the text banks from the mem pool
(`_fileNameLoadToBank` / `_fileNameLoadToAddr`), both in the DRAM arena. When
#95 lands, its `portAddrIsInWindow()` and this helper should become one
function (Track C header unification). No game logic changes; both guards sit
in existing `#ifdef PORT` blocks.

**Verified.** Windows `ntsc-final` build clean (no new warnings).
`verify.sh egypt` PASS. `verify.sh dam` reports REGRESSION at frame 1500
(7416 px, 2.414% over tol): the same count, byte for byte, on a baseline build
of the same branch without this change (run twice), so that is pre-existing
Dam drift, not D573.
