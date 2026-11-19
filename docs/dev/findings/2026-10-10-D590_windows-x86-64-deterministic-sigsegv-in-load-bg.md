# D590

<!-- from the old section-F/H table row -->
| D590 | **Windows x86_64 deterministic SIGSEGV in `load_bg_file` (bg-load worker thread, 4/4 incl. a plain run) — the D588 bug class at a different site: `load_bg_file` reads the stack `header` array (full-width host `bg_addr_t` on PORT) through `PORT_N64PTR`, which truncates to 32 bits and re-maps the worker-stack address onto the PE image (`fault = imagebase + (low32 − 0x40000000)`); fix = drop `PORT_N64PTR` for the full-width operands (`ptr_bg_data`/`ptr_bgdata_offsets`/`gptr_stan`) — plain casts, N64-identity; 5/5 Dam runs exit 0; `PORT_ADDR_STRICT` Dam run clean (path-coverage caveat)** — full `## D590` entry at file tail | FIXED 2026-10-08 (macos branch) |

## D590: Windows x86_64 deterministic SIGSEGV in `load_bg_file` (bg-load worker thread) — the bg-load path re-based a full-width host `bg_addr_t` through `PORT_N64PTR`, mapping the worker-thread stack header array onto the PE image — FIXED 2026-10-08 (macos branch)

Symptom: the `macos` branch Windows build (tree clean at `eed6b774`, after a
full 251-object reconfigure) SIGSEGVd **deterministically (4/4, incl. a
plain run with no probe env vars)** in `load_bg_file`
(`src/game/bg.c:897`, `exe+0x729b`, `mov 0x4(%rdx),%edx`), on the **bg-load
worker thread** (`portThreadWrapper` → `bossEntry` → `bossMainloop` →
`lvlStageLoad` → `load_bg_file`). Fault addr `0x0000000161ecfb74`; thread
stack `0x614d0000..0x61cd0000`, `RSP=0x61ccbab8`. The last *working* run
(`dam_win2.log`, exe built at `08c7246a`) predated the full rebuild; the
crash blocked the D589 Mac-vs-Win texture A/B (it needs the Win-side rNNN
pool dumps). Full state at the time: `docs/dev/notes/HANDOFF-WIN-LOADBG-CRASH.md`.

Root cause: **the D588 bug class, at a different site.** `load_bg_file`
holds the stage header in a local stack array (`s32 header[0x10]`) and
publishes it as `ptr_bg_data = (bg_addr_t)header` — on the PORT build
`bg_addr_t` is a **full-width `u8 *`** (D457). Line 897 then read the header
through `((s32 *)PORT_N64PTR(void, ptr_bg_data))[1]`; under PORT
`PORT_N64PTR` expands to `portN64ToHost((uint32_t)(expr))` — i.e. it
**dropped the top 32 bits of a live host pointer and re-based the
remainder**. The arithmetic is exact: the `GE_D590HDR` probe (below) shows
`header = 0x61ecfb70` on the bg-load worker stack; low 32 bits
`0x61ecfb70` fall inside the D131 image-rel window
`[0x40000000, 0x70000000)`, so `portN64ToHost` remaps it to
`imagebase + (0x61ecfb70 − 0x40000000)` = **`0x161ecfb70`** — the fault
address `0x161ecfb74` is that value + 4, i.e. exactly the faulting load
(`word[1]` of the header). The layout-shift of the full rebuild put the
worker stack deterministically in the colliding range (D588 already
recorded Windows worker stacks at `0x618d…`–`0x620d…`); the earlier
working run sat outside it. The handoff's "disassembly puzzle" (faulting
PC on the `image_rel==0` branch with `R8=1`) is immaterial — the fault
address pins the cause; a tail-merged / mis-attributed PC is the likely
explanation.

Fix (rule-2 §A1 pointer-width exception, N64-identity): in
`src/game/bg.c` `load_bg_file`, drop `PORT_N64PTR` wherever the operand is
already a full-width host pointer and use the plain cast (a no-op under
N64, where the macro is the identity and `bg_addr_t` is `s32`):
- line 897: `((s32 *)ptr_bg_data)[1]` (`ptr_bg_data` = the stack `header`);
- line 906-907: `(struct StanPrefixRecord *)gptr_stan` / `(u8 *)gptr_stan`
  (`gptr_stan` is a host pointer per D457);
- line 919: `data = (s32 *)ptr_bg_data` (reassigned from
  `mempAllocBytesInBank`);
- lines 939-957: `((s32 *)ptr_bgdata_offsets)[2..4]` (the same stack
  `header`).
Left in place (legitimate 32-bit N64 operands): line 3096
(`SEGMENT_OFFSET(gdl[...].dma.addr) + (u32)vertices`) and every other
`PORT_N64PTR` use whose operand is a stored N64 u32 field / DRAM address.
Mechanism check (kept, `GE_D590HDR`-gated, one line): log
`(uintptr_t)header` and its low32 at the `ptr_bg_data = (bg_addr_t)header`
site; a correct run prints `low32=0x61ecfb70 in-window=1` on the Dam
repro. If `in-window=0` ever appears while the pre-fix build still
crashed, the D588-class diagnosis would be wrong.

Verification (Windows, `scratch/wt-macos/build-pc`, `-level_33` (Dam),
`GE_QUITFRAME=300 GE_RSEED=0x0123456789abcdef GE_INPUTSCRIPT=20:START`):
- pre-fix: 4/4 SIGSEGV (`ge007.crash.log`, fault `0x161ecfb74` /
  `0x161ccebc1` across runs);
- post-fix normal build: **5/5 exit 0**, 300 frames rendered, orderly
  `GE_QUITFRAME` quit, no FATAL/backtrace, `D590HDR header=…61ecfb70
  in-window=1` confirming the collision mechanism;
- `PORT_ADDR_STRICT` build (`build-pc-strict`): Dam run exit 0 with **zero
  strict reports** besides the injected `GE_ADDRSTRICT_SELFTEST` negative
  (proving the detector is live). Caveat: strict validates that a re-based
  u32 maps to a *known N64 region*; a truncated live pointer whose low32
  lands in-window (this bug class) passes it, so a clean strict run is
  path-coverage evidence (Dam, 300 frames), not a proof that no other
  full-pointer-through-`PORT_N64PTR` sites exist. The wide audit (all
  `PORT_N64PTR` uses on full-width pointers) remains owed (D588 rule: only
  stored N64 u32 fields / DRAM addresses may feed the re-base).
- Platform scope: same defect class on the macOS build (an out-of-window
  stack pointer there is benign because the window base is 0 and the low32
  is the genuine N64 offset), but the plain cast is identity-correct on all
  three platforms.

Also fixed in the same commit: `port/src/port_addr.c` `PORT_ADDR_STRICT`
build on Windows — the unconditional `#include <execinfo.h>` (POSIX-only)
broke the mingw build; the include now matches the existing
macOS/Linux guard on the backtrace block (Windows strict runs still log
the bad address, without a native backtrace).

Cross-tag D588 (the same bug class, `texSetBitstring`/`rzipGetSomething`
sites — the canonical §A1 cross-tag for this defect), D457 (the
full-width `bg_addr_t`/`gptr_stan` retype that made the operand
full-width), D131 (the image-rel window), D441 (the address model), D589
(the texture A/B this crash was blocking). §A1 cross-tag added to
`docs/porting-notes.md` (incl. the fault-addr diagnostic tell:
**fault addr == imagebase + (low32 − 0x40000000)** ⇒ a live host pointer
was truncated and re-based).
