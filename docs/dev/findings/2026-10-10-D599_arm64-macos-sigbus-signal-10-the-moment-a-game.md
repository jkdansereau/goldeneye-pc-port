# D599

<!-- from the old section-F/H table row -->
| D599 | **arm64 macOS: SIGBUS (signal 10) the moment a game controller is connected — `padTabPub` was declared `SDL_atomic_t` (4 bytes) but accessed through `SDL_AtomicGetPtr/SetPtr((void **)&padTabPub)` (8-byte atomic pointer ops). On arm64 the 4-byte-aligned object faults in the 8-byte atomic; on x86 it silently overlaps the adjacent `padTabSig`. Port-layer fix: declare it `void *`.** — full `## D599` entry at file tail | **FIXED 2026-10-08** (macos branch; Mac M3 re-run with a connected Xbox pad: menu + Dam clean, pad table rebuilds; Windows/Linux builds not re-run) |

## D599: arm64 macOS SIGBUS on controller connect — `padTabPub` type/size mismatch with SDL_Atomic{Get,Set}Ptr — FIXED 2026-10-08 (macos branch)

**Symptom.** Maintainer connected an Xbox Series X controller during a live
macOS playtest (menu): immediate `ge007.crash.log`, SIGNAL 10 (SIGBUS),
backtrace `padTabGet+140 <- inputComputePad <- contSnapshotFromKeyboard <-
osContStartReadData <- joyPoll`, fault address = `&padTabPub`.

**Root cause.** `port/src/input.c`: `static SDL_atomic_t padTabPub;` is a 4-byte
`int` struct, but `padTabGet` publishes/reads a `PadTable *` through
`SDL_AtomicSetPtr/GetPtr((void **)&padTabPub)`, i.e. 8-byte atomic accesses.
The object sits at a 4-aligned (not 8-aligned) address (`...e2c`), and the
arm64 8-byte atomic load faults (SIGBUS). `padTabGet` is only reached when a
pad is present (`pads[idx] ? padActHeld(&padTabGet()->...)`), which is why the
earlier no-pad sweeps and live passes never hit it (Launch E's pad item E3 was
conditional on a pad being attached). On x86_64 the same code does not fault but
overlaps the next object (`padTabSig`) with the pointer's upper half: a latent
memory-overlap bug on Windows/Linux too, benign only by layout luck.

**Fix.** `static void *padTabPub;` (pointer-sized and aligned). Call sites
unchanged. Port layer only; no game code. Behaviour change on x86: `padTabSig`
is no longer clobbered by the published-pointer store.

**Verification (Mac M3, Xbox Series X pad connected).** Menu 1200 frames and
Dam 1500 frames: rc 0, no crash log, `input: pad table rebuilt` logged.
Windows/Linux not rebuilt for this one-line change; build + a pad run owed there.
Also found while looking: the other `SDL_atomic_t` objects in port/ are all
int-style accesses (audio.c, input.c, optionsoverlay.c) — no further pointer misuse.

**Status: FIXED 2026-10-08 (macos branch).**
