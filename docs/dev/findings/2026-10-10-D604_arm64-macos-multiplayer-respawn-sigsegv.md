# D604

<!-- from the old section-F/H table row -->
| D604 | **arm64 macOS multiplayer respawn SIGSEGV: `mp_respawn_handler` kept the spawn pad's `stan` in a `StandTile *` local (PORT) but three D441 casts `(uintptr_t)(u32)(start_stan)` truncated that 64-bit pointer to 32 bits, dropping the window base; `bondviewYPositionRelated` then dereferenced bare `0x7016ff30`. (My D603 audit had wrongly called these casts a no-op.)** — full `## D604` entry at file tail | **FIXED + LIVE-CONFIRMED 2026-10-08** (macos branch; maintainer 2P respawn OK; identity at PORT_ADDR_BASE==0) |

## D604: macOS multiplayer (re)spawn crash — `(u32)` cast truncates a pointer-typed `start_stan` — FIXED 2026-10-08 (macos branch; live confirmation owed)

**Symptom.** Maintainer, live 2P (`D416 split=1 players=2 stage=38`, P1 keyboard/mouse + 1
controller): SIGSEGV (11) on pressing Enter as P1, fault addr `0x7016ff36` (x0 = `0x7016ff30`, a
bare N64-form address), backtrace `mp_respawn_handler + 200 <- maybe_mp_interface <- lvlRender`.
(Entered after D603 fixed the preceding `modelSetAnimFrame` crash in `playerTick`.)

**Cause.** `src/game/bondview2.c` `mp_respawn_handler` declares, under PORT, `StandTile *start_stan`
(correct: `PadRecord.stan` is a host pointer at pad+0x30). Three consumers still carried the D441
casts written for an `s32` local — `(StandTile *)(uintptr_t)(u32)(start_stan)`. Applied to a
**pointer**, `(u32)` TRUNCATES to the low 32 bits, then zero-extends: the arm64 window base
(16 TiB) is lost and `bondviewYPositionRelated` / `change_player_pos_to_target` /
`prop->stan` receive a bare `0x70xxxxxx`. Disassembly of the crashing build shows it:
`ldr w21, [x0, #0x30]` (32-bit load of the pointer). Identity on Windows/Linux (base 0) so it never
showed there. The solo twin in `bondview_r.c` has no such casts, which is why only multiplayer
respawn hit it.

**Fix.** Use `start_stan` directly at all three sites (`#ifdef PORT` branches only). Rebuilt
disassembly: `ldr x21, [x0, #0x30]`. **Lesson / audit rule:** a D441 `(uintptr_t)(u32)(x)` cast is only
correct when `x` is an `s32`/`u32` slot (zero-extend, base-0 tier). On a pointer-typed operand it is a
truncation. The D603 audit (and the D587 handoff list) classed these by "s32-held" without checking
the operand type; the other sites were re-checked by type: `chrprop.c:1311` (`s32 unk44` param ->
ModelNode*, N64-form: needs `PORT_N64PTR` if ever dereferenced on the Mac; not reached yet),
`front.c:8620` (`s32` menu video buffer, not read by the GL port), `rsp.c:251/256` (`s32` output
buffer bounds, unused by the software RSP), `mema.c:582` and `model.c:6028` (`s32` slots, run
every level). No other pointer-typed operand under a `(u32)` cast was found.

**Verification.** Windows build clean; Mac rebuilt: 2P and 4P headless 2000 frames rc 0, no crash.
**The faulting path is a respawn (press Enter after dying in MP) and is not reachable headlessly**;
the disassembly (64-bit load) plus the maintainer's live 2P retest (die, press Enter) is the proof.

**Status: FIXED + LIVE-CONFIRMED 2026-10-08 (maintainer, macOS 2P: Enter/start/respawn with keyboard+mouse P1 and 1 controller works).**
