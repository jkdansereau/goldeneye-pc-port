# D453

<!-- from the old section-F/H table row -->
| D453 | **memp allocator API made 64-bit clean (D441 step 0; ABI/layout exception, §A1).** `mempCheckMemflagTokens` took the arena start as `s32`, `mempSetBankStarts` carried it in an `s32 mempStart`, and `mempAllocPackedBytesInBank` returned a pointer as `u32`; all truncate/sign-extend an arena at or above 0x80000000. Widened to `uintptr_t`/`void*` under `#ifdef PORT` (N64 arm unchanged). mema.c NOT changed (structural, see entry). | 2026-09-30 | fix/abi-memp-widen | [H] for memp, mema deferred |

## D453 — memp allocator API 64-bit clean (D441 step 0, ABI/layout exception, cross-tag §A1)

**Scope.** `src/memp.c`, `src/memp.h` only. AGENTS.md rule-2 narrow exception (pointer-width; no logic or control-flow change). Every edit is under `#ifdef PORT`; the N64 arm is byte-identical.

**Sites.**
- `mempCheckMemflagTokens(s32 poolAreaStart, s32 poolAreaSize)` -> `(uintptr_t poolAreaStart, s32 poolAreaSize)`; the pool-0 `start`/`end` are now assigned through explicit `(u8 *)` casts. (memp.h prototype matches.)
- `mempSetBankStarts`: `s32 mempStart` -> `uintptr_t` (was a sign-extending round trip of the `u8*` pool start); the per-bank `start`/`end` assignments cast `(u8 *)`.
- `mempAllocPackedBytesInBank` returned the allocation as `u32` (truncates the pointer); now returns `void *`. It has no callers in the tree today.
- Left `s32` on purpose: `poolAreaSize`, `bankstarts[]`, `mempLen`, `poolSizes` (sizes, not addresses).

**Falsifier (written before the run).** The change altered behaviour if, on the spawn run, the startup log differs beyond version/config lines, OR a `-level_09` frame-400 `GE_PCDUMP` differs structurally (different geometry/textures/lighting, not just the intro-camera phase), OR the build gains a warning in memp.c.

**Evidence.** `-Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion` over memp.c: 6 real warnings (lines 46, 47, 133, 137, 139, 290) -> 0 (5 header-only warnings unchanged). Build clean, no new warnings. `-level_09`, GE_QUITFRAME=420, before vs after: startup logs identical (modulo hash/config/timing lines); frame-400 dump same scene/textures/lighting but the intro camera is at a slightly different phase (3072x1728, dmean 12.1 - wall-clock camera motion, not a memp effect; no noise-floor rerun was in budget). **Confidence [M-H] on no behaviour change** (types only; all pool values < 2 GiB here so the value path is identical by construction).

**Left, not fixed.**
- mema.c: `memaspace.addr` is `s32`, `g_MemaHeapStart` is `s32`, `memaReset` stores `(uintptr_t)heapaddr` into it, `memaAlloc` returns `(void *)` of an `s32`/`u32` addr (mema.c:317, 572), and `end1/end2.addr = 0xffffffff` plus `curr->addr == -1` sentinels. Widening needs `memaspace` layout + sentinel changes, which is more than a type/cast edit. Fine for an arena < 2 GiB; wrong above. Needs its own step (and a decision on the sentinels).
- boss.c:268 `(u32)tlbmanageGetTlbAllocatedBlock() - (u32)start` and `start` type: size diff stays correct only if both ends share the same 4 GiB window; boss.c was out of scope. memp.c callers that store `mempAllocBytesInBank` results into `s32` (bg.c `ptr_bg_data`, bondview_r.c `resolution`) are other census rows.
