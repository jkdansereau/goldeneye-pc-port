# D587

<!-- from the old section-F/H table row -->
| D587 | **macos sweep: 9 of the first 13 levels SIGSEGV at one site — `if_actor_able_set_on_path` derefs the raw N64 patrol-path address in the s32 `pathid` slot (ours' D441 zero-extend is base-0-only); adopted theirs' `PORT_N64PTR` rebase** — full `## D587` entry at file tail | FIXED 2026-10-07 (macos branch) |

## D587: macos sweep — 9 of the first 13 levels SIGSEGV at the same site: `if_actor_able_set_on_path` derefs the raw N64 patrol-path address held in the s32 `pathid` slot — FIXED 2026-10-07 (macos branch)

The arm64 sweep's first crash family is uniform: fault addrs `0x701b…`–`0x7021…`
(N64 DRAM, unmapped on the Mac window) at `if_actor_able_set_on_path + 64`
under `ai` → `chrlvActionTick` → `chrTick` → `propsTick` → `lvlRender`.
`src/game/chraction.c` carried ours' D441 zero-extend `(struct patrol_path
*)(uintptr_t)(u32)(pathid)` — base-0-only (the resolver kept ours because ours
does not *truncate*, it zero-extends, which is only correct with
`PORT_ADDR_BASE == 0`). Adopted theirs' rebase: `PORT_N64PTR(struct patrol_path,
pathid)` (identity at base 0; `portN64ToHost` maps the DRAM slot into the
window on the Mac; `pathid == 0` yields NULL, same as the old cast). Rule-2
class §A1 (address-carry at the game-side carrier, per D612's reasoning).
Windows build re-verified clean. Remaining `(uintptr_t)(u32)` D441 sites stay
under the task-6 audit until the sweep reaches them.
