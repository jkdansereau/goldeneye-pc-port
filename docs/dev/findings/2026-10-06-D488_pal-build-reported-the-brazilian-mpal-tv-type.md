# D488

<!-- from the old section-F/H table row -->
| D488 | **PAL build reported the Brazilian MPAL TV type instead of PAL (`osTvType`)** — full `## D488` entry at file tail | FIXED |

## D488 — PAL build reported the Brazilian MPAL TV type instead of PAL (`osTvType`)

`port/src/libultra.c` hard-codes `osTvType` (no hardware to read). Since the first scaffolding commit (`cd6cc158`, 2026-08-17) the PAL build set `OS_TV_MPAL` (2), with the comment "PAL consoles report MPAL" -- an inference from GE's shared code, which only ever tests for MPAL (`sched.c:287`, `init.c:237`, `fr.c` non-EU: MPAL LAN1 mode, else NTSC LAN1). Not copied from the PD reference, which hard-codes `OS_TV_NTSC` for every build and whose own game code tests `OS_TV_PAL` separately. A European N64 reports `OS_TV_PAL` (0); MPAL is Brazil's 60 Hz PAL-M console. So on real PAL hardware GE starts the scheduler in NTSC LAN1 and the EU `video_related_8` (`fr.c`, `#ifdef VERSION_EU`) then installs the PAL-table modes; the port took the Brazil-only MPAL branches instead. Never noticed because the PAL build was first compiled on 2026-10-02 (D258).
**Fix (port-only, maintainer go-ahead 2026-10-02):** `osTvType = OS_TV_PAL` for `REFRESH_PAL`, comment corrected; the port's 50 Hz tick now keys on `OS_TV_PAL` only (MPAL is 60 Hz; no build sets it). Not game logic (the value a console reports), so no Rule-2.
**Verified (headless):** PAL Dam frames 900/1200/1500 identical to the pre-change build (3/3); the boot sequence (legal, Nintendo, Rareware, gun barrel) identical to the post-D487 capture on every shared frame, timing unchanged; front-end mode select and mission select render, mode select identical to NTSC-U (the stamp running off the bottom is the shared 440x325 menu layout). US/JP unaffected (the change is under `REFRESH_PAL`).
**Maintainer PAL play check 2026-10-02:** "PAL seems fine" (no issues seen).
**Status:** FIXED.
