# D200

<!-- from the old section-F/H table row -->
| D200 | **`-level_09` segfault: reverb's `ALDelay.input/output` are u32 — `-d->output` zero-extends to a +4GB pointer offset on 64-bit (D3x class, FIXED).** `src/libultrare/audio/reverb.c` back-references the delay ring as `&r->input[-d->output]`; on N64 (s32 `ptrdiff_t`) the u32 negation wraps to a small negative index, on PC x86-64 it zero-extends to +4294967136 samples → `_loadBuffer`/`_saveBuffer` write 13–52KB wild, trampling the delay array / `r->base` / adjacent heap in a self-propagating loop until unmapped memory. Fixed with `(s32)` negation casts at 5 sites (PD ground truth `n_reverb.c` does exactly this); ramalign line gets `(s64)`. Verified: `-level_09` runs 120 s+ crash-free, DRAM guard window over the FX region sees zero OOB writes. | FIXED — 5-line cast in reverb.c (this session) |

## D200 — `-level_09` segfault: u32 reverb delay indices negate into +4GB offsets on 64-bit (fixed)

**Symptom.** `-level_09` reliably segfaulted within seconds of audio starting, in
`_filterBuffer` (`src/libultrare/audio/reverb.c`) — `d->lp` (a pointer into the AFX
delay array) resolved to garbage like `0x664d59be`.

**Root cause.** `ALDelay.input` / `ALDelay.output` are `u32`, and reverb.c computes
back-references into the delay ring as `&r->input[-d->output]` (and variants). On
N64, `ptrdiff_t` is s32, so `-160` (the u32 constant `0xFFFFFF60`) wraps to −160 and
the pointer arithmetic is correct. On PC x86-64, `ptrdiff_t` is s64: the u32 negation
**zero-extends** to +4294967136 samples, so `out_ptr` flies forward ~8GB — verified
exactly: `0x706d30a0 (r->input) + 0xFFFFFF60·2 = 0x2_706D2F60`, the observed wild
address. The subsequent `aSaveBuffer`/`aLoadBuffer` then write 13–52KB of wild data,
corrupting the delay array, `r->base`, and adjacent heap blocks — a self-propagating
loop that grows each frame until it hits unmapped memory.

**Fix (ABI/layout only, D3x class).** Five sites in `src/libultrare/audio/reverb.c`
cast to `(s32)` before negation so the offset sign-extends: `alFxPull` (`in_ptr`,
`out_ptr`), `_loadOutputBuffer` ×2 (`out_ptr`), plus the ramalign line, which uses
`(s64)out_ptr & 0x7`. Perfect Dark ground truth (`src/lib/naudio/n_reverb.c`) does
exactly this: `(s32)-d->input`, `(s32)-d->output`, `(s32)-(d->output - d->rsdelta)`,
`(intptr_t)out_ptr & 0x7`. No game logic changed; behaviour on N64 is identical.

**Verification.** `-level_09` now runs 120 s+ crash-free (previously segfaulted in
seconds); the delay pointers advance correctly (`out_ptr = input − 320B`, +0x140/frame).
A temporary DRAM guard window over the FX region, registered with the software mixer
(`port/src/mixer.c`), confirmed zero out-of-bounds writes after the fix. All
instrumentation removed; only the five casts remain in the diff.

**Sibling-risk audit.** Grep of `src/libultra/audio/*.c` + `src/libultrare/audio/*.c`
for negative pointer indexing: reverb.c is the only file with the pattern (its one
positive index, `&r->input[d->output]`, is safe — positive u32 zero-extension is
correct).

**Lineage.** D3x pointer-width reconciliation; same family as D177 (widened-pointer
high half) and D191 (32-bit field read as 64-bit).
