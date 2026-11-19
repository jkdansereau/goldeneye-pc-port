# D613

<!-- from the old section-F/H table row -->
| D613 | **PR #95 (macos branch) re-applied onto `release/v0.5.1` — the stale-base merge the macos track carried as a behind-`origin/macos` pull; 3-way over the v0.5.1 tree, resolved by signature (keep ours unless the D441 `(uintptr_t)(u32)` truncation needs theirs' `PORT_N64PTR` rebase; memp always theirs; D573 guards supersede theirs)** — full `## D613` entry at file tail | FIXED 2026-10-07 (macos branch) |

## D613: PR #95 (macos branch) re-applied onto release/v0.5.1 — the stale-base merge the macos track had been carrying as a 5-commit-behind origin/macos — FIXED 2026-10-07 (macos branch)

*Labelled D585 on the macos branch; renumbered at the v0.5.1 merge (the interpolation work had used D579-D585).*

The macos track's #95 (danturn's macos-branch model work: `port_addr.c`
implementation, the D441 `PORT_N64PTR` rebase sites, the mema/front/etc.
arm64 fixes) sat on a stale base (5 commits behind at the pull, 45 ahead /
5 behind overall). Re-applied as a 3-way (`git apply -3`) over
`release/v0.5.1 @ 72c4f4c0`: 23 files clean, 37 conflicted (~110 markers),
resolved by signature: keep ours (v0.5.1) unless ours truncates an arena
address through the D441 `(uintptr_t)(u32)(slot)` pattern — then adopt
theirs' `PORT_N64PTR` rebase. Full-width base+offset carriers (ours' D457/D456)
are Mac-safe and kept. memp.c always theirs (pool start/end need
`portN64ToHost`; `poolAreaStart` is an N64 address carried from boss.c).
The FlyingParticles buffer stays ours (static C array → real host pointer;
rebasing a .bss address would truncate it). D573's `portAddrIsMapped`
supersedes theirs' `portAddrIsInWindow` guards; theirs' TEMP D65
`GE_D63` probe block dropped (v0.5.1 removed it). Header unified to the
D607 one-header model (`portaddr.h`, not `port_addr.h`). Post-resolver
mechanical fixes: boss.c include block + unclosed `#ifdef PORT` at the
rspGfxTaskStart block, image.c duplicate dst16/dst8 arms (theirs'
MACOS-TEXALIGN block declares all three at full width), mpmenu.c comma-
expression `#ifdef` arms restored, model.c PROMOTE N64 arm restored
(theirs' `portN64ToHost` landed in the wrong branch; PROMOTE is Mac-safe
as-is: the zero-extension cancels through `diff`). Windows build verified
clean (252 steps, fresh exe) before landing. Rule-2 class §A1 throughout
(pointer-width carriers, address-carry fixes at the game-side carrier per
D612's reasoning); no logic/behaviour change.
