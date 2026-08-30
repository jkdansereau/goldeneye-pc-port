# Local-agent handoff tasks (Qwen 27B) — M-20

The two M-19 tasks (D63 scaffolding removal in `libultra.c`; PORT-LEARNINGS
§E entries) are **done and committed** (`1fc3cff6`). Below are three fresh
tasks.

**All three are pure static analysis** — read code, produce a table, write
a doc section. **No builds. No running the game. No `.c` edits.** Deliverable
is a markdown write-up appended to `docs/PCPortResearch.md` §F (add the new
label to the §F index at the top of the file) plus, where noted, one line in
`docs/PORT-LEARNINGS.md`.

Disjoint from the active session work (intro-render investigation in
`port/fast3d/`, propDef converter audit in `tools_pc/d88_propdefs.py`). **Do
NOT touch `port/fast3d/*`, `tools_pc/d88*`, or any `.c`/`.cpp` file.**

READ FIRST for all tasks: `docs/PORT-LEARNINGS.md` (whole file — it is one
screen), then the specific §F entries named in each task (jump via the §F
index; do NOT linear-read `PCPortResearch.md`).

---

## Task A — `osVirtualToPhysical()` latent-truncation site audit (D131 corollary)

**Background:** `docs/PORT-LEARNINGS.md` §A "D131 corollary". The port shim
`osVirtualToPhysical(va)` returns `(u32)(uintptr_t)va`, which silently drops
the high 32 bits of a **compiled module symbol** pointer (`.bss` / `.rodata`
address, image-based at `0x140000000`). D131 fixed the one live instance
(`explosionRenderPropSmoke`) centrally in `seg_addr()` in
`port/fast3d/gfx_pc.cpp`, which now restores the module high word for a
fallthrough w1 in `[0x40000000, 0x70000000)`.

**Task:** grep every call site of `osVirtualToPhysical(` across `src/`
(`grep -rn 'osVirtualToPhysical(' src/`). For each, produce a table row:

| file:line | argument expression | arg is: runtime-DRAM ptr / compiled symbol / segmented / unknown | reaches a GBI w1 (`gSPMatrix`/`gSPVertex`/`gSPDisplayList`/`gDma*`)? | covered by D131 `seg_addr` fix? | residual risk |

Classify "compiled symbol" when the argument is `&someGlobal`, a `.bss`/
`.rodata` array/struct address, or a function-local `static`. Classify
"runtime-DRAM ptr" when it is the result of a heap/bank allocation
(`mempAlloc*`, `malloc`, a `->` deref of loaded data). Note which are armed
only when a specific effect first draws (explosion / glass / blood /
bondview2 — the §A note lists these).

**Deliverable:** the table + a short "residual risk" summary (are there sites
that reach fast3d NOT through `seg_addr`, e.g. a raw `memcpy` or a
non-w1 use?) → `docs/PCPortResearch.md` §F, new subsection
**"D131 corollary — osVirtualToPhysical site audit (M-20)"**, label added to
the §F index.

**On expiry:** commit the partial table.

---

## Task B — in-place ROM-struct relayout aliasing audit (D130 corollary)

**Background:** `docs/PORT-LEARNINGS.md` §A "D130 corollary". `romdataFixupFont`
(`port/src/romdata.c`) re-lays-out a ROM struct array (`fontchar` 24→32B) *in
place*: `dst = base + PCstride*i`, `src = base + N64stride*i`, and for small
`i` the two overlap by less than the per-element read span, so a
field-by-field copy loop clobbers a not-yet-read source field. D130 fixed
that one loop by staging all fields into locals first.

**Task:** find every other in-place N64→PC struct-array relayout. Search:
`grep -rn 'romdataFixup\|Fixup\|relayout\|in.place\|24.*32\|->32B' port/src/`
and read `port/src/romdata.c` fully (it is the main offender file). For each
loop that (a) reads struct fields from a source offset and writes to a
different-stride destination **in the same buffer**, tabulate:

| file:line (fn) | struct | N64 stride | PC stride | loop direction | dst/src overlap for low i? | reads all fields before writing? | verdict (SAFE / ALIASING BUG / needs staging) |

**Deliverable:** the table → `docs/PCPortResearch.md` §F, new subsection
**"D130 corollary — in-place relayout audit (M-20)"**, label in the §F index.
If you find a concrete ALIASING BUG, describe the fix (stage fields into
locals, as D130 did) in prose in the write-up — **do not edit the `.c`
file**; flag it for the integrator.

**On expiry:** commit the partial table.

---

## Task C — negative / OOB glyph-index audit (PORT-LEARNINGS §A last bullet)

**Background:** `docs/PORT-LEARNINGS.md` §A final bullet: `chars[*text - 0x21]`
with a control byte (`*text < 0x21`) indexes before `chars[0]`. Benign on
N64 (hits the adjacent 4-byte kerning table → garbage-but-valid glyph);
**fatal on PC** — the widened `fontchar` has an 8-byte `pixeldata` at a
higher offset, so a negative index reads a wild pointer → fast3d AV. No
level is known to feed a control byte to HUD text yet, so this is latent.

**Task:** in `src/game/textrelated.c` (and any other file that indexes a
`fontchar`/`chars` array — grep `chars[` and `- 0x21` / `- 33` across
`src/game/`), enumerate every glyph-index expression on the `textRender*` /
`textMeasure*` ASCII paths. For each, tabulate:

| file:line (fn) | index expression | can the byte be `< 0x21` or `>= last glyph`? (what feeds it — dialogue? HUD? menu? filename?) | guarded already? | consequence on PC if OOB |

**Deliverable:** the table + a recommendation: where exactly a
`GLYPH_IDX` clamp macro (`clamp to [0, nglyphs-1]`, or skip the glyph) would
go, expressed as a prose description of the one-line `#ifdef PORT` guard per
site — **do not edit `.c`**. → `docs/PCPortResearch.md` §F, new subsection
**"Glyph-index OOB audit (M-20)"**, label in the §F index. Add one terse
line to `docs/PORT-LEARNINGS.md` §A noting the audit is done and where the
clamp sites are.

**On expiry:** commit the partial table.

---

## Verify (all tasks)

`git diff --stat` shows **only** `docs/PCPortResearch.md` and (Task C)
`docs/PORT-LEARNINGS.md` changed. No other files. Markdown still renders.
`git status` shows no deleted files. Commit message per task, e.g.
`docs: M-20 Qwen — D131 corollary osVirtualToPhysical site audit`.
**No `Co-Authored-By` / `Claude-Session` trailers** (repo policy).
