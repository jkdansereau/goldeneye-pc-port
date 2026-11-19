# D588

<!-- from the old section-F/H table row -->
| D588 | **Windows x86_64 SIGSEGV (deterministic 3/3) in `texReadBits` under `lvlStageLoad`: the #95 `texSetBitstring` re-base maps the native worker-thread stack (low-32 in the D131 image-rel range `[0x40M,0x70M)`) onto the PE image; fix = full pointer-width arg (`u8 *`) + widen `rzipGetSomething()` to `u8 *` on the PORT build (N64-identity, Mac-neutral)** — full `## D588` entry at file tail | FIXED 2026-10-07 (macos branch) |

## D588: Windows x86_64 SIGSEGV in `texReadBits` (deterministic 3/3) — the #95 `texSetBitstring` re-base mis-maps the native worker-thread stack into the D131 image-rel range — FIXED 2026-10-07 (macos branch)

Symptom: the worktree `ge007.x86_64.exe` (head c675ee99, post-#95 re-apply
D613) SIGSEGVs deterministically (3/3) in a worker thread at
`texReadBits` (`src/game/image_bank.c`) under `texInflateNonZlib` →
`texLoad` → `texLoadFromDisplayList` → `lvlStageLoad`. Fault addrs are of the
form `imagebase + (native_stack_low32 − 0x40000000)` — e.g. `0x161ccebc1`
= `0x140000000 + (0x61ccebc1 − 0x40000000)` — i.e. inside the PE image but
past its end (unmapped).

Root cause: the #95 re-apply (D613) changed the `PORT` body of
`texSetBitstring` from the D441 base-0 zero-extend `(u8 *)(uintptr_t)(u32)pos`
to `portN64ToHost((u32)pos)`. The callers pass a **live host pointer** —
texLoad's stack `compbuffer`, or the DRAM mempool cursor returned by
`rzipGetSomething()` (itself `(s32)(uintptr_t)s_rz_nextin`, truncated to 32
bits). On Windows the libwinpthread game/render thread's native 8 MB stack
lives at `0x618d…`–`0x620d…`, whose low 32 bits fall **inside the D131
image-relative encoding range `[0x40000000, 0x70000000)`**. `portN64ToHost`
therefore re-maps that low-32 onto the PE image (`imagebase + (low32 − 0x40M)`),
landing past the image's end → `texReadBits` dereferences an unmapped page
and faults. The M2 re-base was correct for in-window (macOS) pointers, whose
low 32 bits are genuine N64 offsets; it is wrong for a **native** (out-of-
window) pointer that merely *collides* with the image-rel range.

Fix (rule-2 §A1 pointer-width; N64-identity): take the argument at **full
pointer width** on the PORT build and drop the truncate-then-re-base.
`src/game/image_bank.{c,h}`: `void texSetBitstring(u8 *pos)` under
`#ifdef PORT` (N64 keeps `s32 pos`; on MIPS pointer == s32 width), body
`img_curpos = pos;`. `port/src/rzdecomp.c` + `src/game/decompress.h`:
widen `rzipGetSomething()` to return `u8 *` (the live `s_rz_nextin` cursor)
under `#ifdef PORT` so the cursor is not truncated to s32 before reaching
`texSetBitstring` (N64 build keeps the s32 form in `src/game/decompress.c`,
which is EXCLUDED from the PC build). Full width is identity at base 0
(Windows/Linux) and, for an in-window pointer, identical to the old re-based
value on macOS — so this is Mac-neutral and strictly correct on all three
platforms.

Also audited every other `portN64ToHost((u32) …)` M2 re-base in the tree: all
the remaining ones (`memp.c`, `mema.c`, `model.c`, `chr.c`, `bondview*.c`,
`chraction.c`, `initactorpropstuff.c`, `title.c`, `othermodemicrocode.c`,
`language.c`) operate on *stored N64 u32 fields / DRAM addresses*, which is the
correct M2 usage. `texSetBitstring` (and its `rzipGetSomething` feeder) were
the only two that re-based a **live** host pointer.

Verification: Windows build 251/251 clean; the previously-3/3-crashing
`-level_33` (dam) run now passes `lvlStageLoad`, renders 300 frames, and exits
0 on `GE_QUITFRAME` with no fault/backtrace. Mac build unaffected (in-window
stack ⇒ D588 changes no Mac behaviour here). Cross-tag D131 (image-rel
range), D441 (base-0 zero-extend), D613 (#95 re-apply that introduced the
re-base).

---
