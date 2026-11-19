# D603

<!-- from the old section-F/H table row -->
| D603 | **arm64 macOS multiplayer (2P, stage 38) SIGSEGV in `modelSetAnimFrame` via `playerTick`: the player body-model animation pointer `anim` (an `s32` holding an N64-form address) was zero-extended `(uintptr_t)(u32)(anim)` — D441's base-0 tier — instead of re-based with `PORT_N64PTR`, so `modelSetAnimation` got the bare `0x707b2658`. §A1 s32-slot class, D587/D595 sibling.** — full `## D603` entry at file tail | **FIXED 2026-10-08, live confirmation owed** (macos branch; identity at PORT_ADDR_BASE==0; headless 2/3/4P + solo smoke clean but the death/firing-animation path is not reachable headlessly) |

## D603: macOS multiplayer crash — `playerTick` zero-extends an N64-form animation address — FIXED 2026-10-08 (macos branch; live confirmation owed)

**Symptom.** Maintainer, live, 2-player split-screen (`D416 split=1 players=2 stage=38`): SIGSEGV
(SIGNAL 11), fault addr `0x707b2658`, backtrace `modelSetAnimFrame + 164 <-
modelSetAnimation2 + 168 <- playerTick + 2192 <- propsTick <- lvlRender`. The fault address is a
bare N64-form DRAM address (`0x70xxxxxx`) with no window base — the D587/D595 signature.

**Cause.** `src/game/bondview2.c` `playerTick`: `s32 anim` is computed as
`g_bondviewBondDeathAnimations[..] + (s32)ptr_animation_table` (death anims) or
`fa->anim + (s32)ptr_animation_table` (firing groups), i.e. an N64-form address (the low 32 bits
of the live host pointer = the arena-relative value), or a firing-table word. The PORT call
converted it with D441's zero-extend, `(ModelAnimation *)(uintptr_t)(u32)(anim)`, which is only
correct where the window base is 0 (Windows/Linux). On arm64 macOS (base 16 TiB) the base is
missing, so `modelSetAnimation` -> `modelSetAnimFrame` dereferences the bare address.

**Fix (src/game/bondview2.c, `#ifdef PORT`, §A1 narrow exception: s32-slot pointer-width, no
logic/behaviour change).** `PORT_N64PTR(ModelAnimation, (u32)(anim))` — identity at
`PORT_ADDR_BASE == 0`. `players_cur_animation` and the s32 compares stay in N64 form.

**Verification.** Windows build clean. Mac M3 headless (GE_STARTMP/GE_MPVIRT, stage 38, both seats
scripted to move/fire): 2P, 3P and 4P 2000 frames rc 0, no crash log; solo Dam/Archives stress
clean. **Not reproduced pre-fix headlessly**: the faulting branch (a body-model animation change
for a real player, e.g. death/firing group) is not reached by idle or fire-only seats, so the live
re-test (including dying) is the confirmation.

**Audit of the remaining D441 `(uintptr_t)(u32)` sites (not changed).** `bondview2.c:8920/8939/8951`
were judged "a `StandTile *` local, no-op" — **WRONG, see D604**: `(u32)` of a pointer truncates. `model.c:6322` verified Mac-safe (D587 audit);
`objecthandler_2.c` (GDL diff arithmetic), `rsp.c` (output buffer), `mema.c:582` and
`model.c:6028` run on every solo level on the Mac. **`chrprop.c:1311`** (`shotdata->hits[i].unk44`,
a ModelNode pointer stored in an s32) and `front.c:8620` (menu video buffer) were not proven reached
and are the next suspects if a similar bare-`0x70xxxxxx` fault appears.

**Status: FIXED 2026-10-08 (macos branch). The same maintainer 2P session (after D604) ran through spawn/respawn with no crash; the death-animation branch itself is still only indirectly exercised.**
