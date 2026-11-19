# D189

<!-- from the old section-F/H table row -->
| D189 | **Linux `-fstack-protector` aborts on the decomp's latent stack-buffer overruns (M-48) — FIXED.** — full `## D189` entry at file tail | FIXED (`CMakeLists.txt`, `src/game/stan.c` `#ifdef PORT`); PR #23 |

## D189 — Linux `-fstack-protector` aborts on the decomp's latent stack-buffer overruns (M-48) — FIXED.

**Linux `-fstack-protector` aborts on the decomp's latent stack-buffer overruns (M-48) — FIXED.** Two hit on `-level_09`: (1) `sub_GAME_7F0B1DDC` (`stan.c`) — local `StandTile *tileStack[39]`, bail check `if (cat >= 41)` runs only once per outer iteration so `cat` reaches `40 + pointCount` and the append writes past the end; BUNKER1 prop room-list setup legitimately visits ~40 tiles (confirmed in GDB: 30 valid distinct tile ptrs and climbing; `standTileStart=0x701ae160`, so **not** a pointer-truncation bug — the earlier "blob >4 GiB" triage premise was wrong). (2) `bondviewCalcIntroSwirlCamera` (`bondview2.c`) — `f32 pointbuf[10]` written `[-3 .. 11]` via `&pointbuf[i*3]`, i∈[-1..2]. Both harmless on N64 + the MinGW build (no stack protector); only Ubuntu gcc's `-fstack-protector-strong` default makes them fatal `__stack_chk_fail`. **Fix:** `-fno-stack-protector` globally (`CMakeLists.txt`) to match the N64/MinGW builds, + `tileStack[64]` under `#ifdef PORT` for the one cleanly-bounded case, + `stan.c:2225` link-resolve moved to the D177 `PORT_PTRADD` macro for consistency. A *hardened* Linux build would need the whole overrun class swept. **Verified:** Windows `-level_09` framediff 3/3; Linux `-O2` `-level_09` runs to frame 2100+ no crash. porting-notes.md §C.

**Status (from §F table):** FIXED (`CMakeLists.txt`, `src/game/stan.c` `#ifdef PORT`); PR #23
