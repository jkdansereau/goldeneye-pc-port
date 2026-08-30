# PORT-LEARNINGS — recurring N64→PC bug classes

One-screen index into `docs/PCPortResearch.md` §F. **Every investigation
subagent reads this first and appends any new generalisable quirk.**
Details, evidence, and the fix for each instance live in §F under the
cited Dxx label.

## A. Pointer-width struct growth (32→64) — the dominant class

A decomp struct with pointer fields, or one pun-allocated into a fixed
N64-sized hole / hardcoded byte count, is **larger on x86-64**. Reading it
from ROM bytes misaligns; allocating it N64-sized overruns adjacent
state.

- Symptom: garbage pointer deref, corrupted neighbour field, delayed
  fault far from the cause.
- Fix: store embedded ROM addresses as `u32`, cast at use site (PD ground
  truth); or give real inline storage / `sizeof()`-based alloc under
  `#ifdef PORT`.
- Instances: D53.2 (ModelSlot pun), D56 (watch Model raw offsets), D57
  (rwdata record count), D67 (image_entry), D79 (bg_room_data), D98
  (struct player alloc), D100 (player.model inline Model), D101/D102
  (ModelNode*/weapon Model puns), D115 (gunfire THROW* raw offsets),
  D119 (`weapons_held[]->chr` punned as `ChrRecord*` to read
  `.act_*.attack_item` — aliased `WeaponObjRecord.weaponnum` at 0x80 on
  N64 via act-union@0x2C+84; act union moves to ~0x38 on PC).
- **Open landmine:** raw hardcoded-offset accessors into `struct player`
  / `struct hand` — see `docs/AUDIT-M6-player-offsets.md`.
- **D126 corollary — a trailing runtime list pointer in a ROM-serialized
  record.** If a fixed-size ROM record ends with a `T *next` (or `*child`,
  `*parent`) that game code *writes* while walking the setup stream
  (linked-list build), that field is 4B/end-of-record on N64 but widens to
  8B and 8-aligns on PC — the store spills past the N64 record size into
  the *next* record and silently corrupts a polymorphic walk downstream
  (type byte clobbered → wrong strides → wrong command indices → wrong
  pointer resolution, crashing far away). Offline converter (`d*_emit.py`)
  MUST emit these at the real PC `sizeof` with the pointer slot widened +
  the matching `#ifdef PORT` `sizepropdef`/stride. Instances: `d88_propdefs.py`
  types 30/32/33/35 (`criteria_*` / `setup_objective_text` `->next`),
  22 (`TagObjectRecord.NextTag`, already handled). Check every record type
  the walk can see for a `set_parent_*` / `*_entry_parent` writer.

- **D132 corollary — a `union { T *ptr; s32 IndexN; }` field is 8B/8-aligned
  on PC even when the ROM image only ever stores the `s32`.** `LinkRecord`
  (`first`/`Index1`), `LockDoorRecord`, `SafeObjectRecord` etc. pack their
  index words tight (4B) in the N64 setup stream, but the compiler places
  each `IndexN` in the LOW 4 bytes of an 8-byte-aligned pointer slot
  (`Index1`@8 not @4, `Index2`@16 not @8, then a trailing `*next`). A
  converter that lays them at N64 4-byte offsets makes `pdef->Index1` read
  `Index2`'s value and `Index2` read 0 → the record's validity guard fails
  and switch-doors / dual weapons / locked doors / safes silently never
  initialise (usually non-crash, because the failed guard also skips the
  pointer stores). Fix: emit each `IndexN` at PC offset `8 + 8*(N-1)` and
  size the record with the widened unions + trailing `*next`
  (`d88_propdefs.py` types 14/19/38/44 — proposed in §F D132, not yet
  applied). Grep every ROM-serialized record for `union {` with a pointer
  arm. NB `[u16 ID][s16 x]` NOT in a union (e.g. `TagObjectRecord`) stays
  at offset 4 — only the pointer union forces the 8-align.
- **D130 corollary — in-place N64→PC struct-array re-layout aliases when the
  stride grew less than the read span.** A ROM struct array widened on PC
  (`fontchar` 24→32B, `romdataFixupFont`) is often re-laid-out *in place*:
  for glyph `i`, `dst = base + PCstride*i`, `src = base + N64stride*i`, so
  `dst − src = (PCstride−N64stride)*i`. For small `i` that delta is *less
  than the per-element field-copy read span*, so `dst` overlaps `src`, and a
  forward field-by-field `*dst_k = f(*src_k)` loop overwrites a not-yet-read
  `src_j` (`j > k`). Result: later fields of the first few elements come out
  as `f(f(earlier field))` — e.g. glyph 1's `width` became `bswap(bswap(index))`.
  Iterating elements backward does NOT help (the overlap is *within* one
  element). Fix: read every source field of the element into locals first,
  then write. Grep every `romdataFixup*` / in-place relayout for a
  read-write loop whose `dst`/`src` can alias for low indices.
- **A negative/OOB index into a widened struct array is fatal on PC, benign
  on N64.** `chars[*text - 0x21]` with a control byte (`*text < 0x21`) reads
  before `chars[0]`. On N64 that hits the adjacent 4-byte-field kerning table
  (small values → a garbage-but-TMEM-valid glyph); on PC the struct's 8-byte
  `pixeldata` reads a wild pointer → fast3d AV. Latent in `textrelated.c`
  (`textRender*`/`textMeasure` ASCII paths) — a `GLYPH_IDX` clamp fixes it if
  a level actually feeds a control byte to the HUD text (none do yet; D130
  was a font-relayout bug, not this).

- **D131 corollary — `osVirtualToPhysical()` truncates a compiled-symbol
  pointer to 32 bits.** The port shim is `(u32)(uintptr_t)va` (`libultra.c`).
  For a runtime DRAM pointer (`0x70xxxxxx`) the cast is lossless; for a
  **compiled module symbol** (`.bss`/`.rodata` matrix, e.g.
  `&dword_CODE_bss_8007A100` in `explosionRenderPropSmoke`) it drops the
  `0x1_00000000` module high word, so the GBI w1 becomes `0x40xxxxxx` and
  `seg_addr()` hands fast3d a wild pointer → AV in `gfx_sp_matrix` /
  `gfx_sp_vertex`. Same class as D94 (`chraction.c:1243`), but in a DL word.
  ~30 latent sites (`grep 'osVirtualToPhysical(' src/game/{explosion,glass,glass2,blood_animation,bondview2}.c`),
  each armed only when that effect first draws. Fixed once in `seg_addr()`:
  restore the high word for a fallthrough `w1 ∈ [0x40000000, 0x70000000)`
  (module is fixed-based at `0x140000000`; DRAM/KSEG0/segmented/phys are all
  handled in earlier branches). `gSPDisplayList(&globalDL_0xNNN)` is NOT
  affected — the port's `Gwords.w1` is 64-bit and `gDma1p` stores the full
  pointer; only `osVirtualToPhysical` truncates.

## B. 16-byte PC `Gfx` / `Vtx` vs 8-byte N64

Any buffer reservation, `memcpy` size, slot stride, or pool budget
expressed in N64 `Gfx`/`Vtx` units is **half-size** on PC.

- Instances: D50.6 (texCopyGdls copied only w0 of each 16-byte slot),
  D58 (DL reserve 0x100→0x200), D85 (`bgWidenRoomGdl` 8→16 + bswap),
  D95 (2× master-DL buffer + raised mempool ceiling).
- **Also bites RAW hardcoded struct-stride writes, not just Gfx/Vtx.**
  D128: `sub_GAME_7F0B37EC` did `((u8*)g_BgPortals)[(portal<<3)+6] |= 2`
  — the N64 `bg_portal_data_entry` is 8B (`ptr@0, cr1@4..cb2@7`); on PC
  the widened `offset_portal` ptr makes it 16B (`cr1@8..cb2@11`), so the
  write landed in the middle of another portal's pointer. Fix = use the
  struct accessor under `#ifdef PORT` (every other site already does:
  `g_BgPortals[portal].controlbytes1 |= PORTALFLAG_SPECIAL`). Grep for
  `<< 3` / `* 8` / `+ 6` style raw offsets into any struct that gained a
  pointer field.

## C. Big-endian rodata / ROM data read on little-endian PC

ROM assets and compiled-in `.rodata` are big-endian. Anything not run
through a converter or a runtime bswap fixup reads scrambled.

- `f32` values are BE **word pairs** — a naive byte reversal off-by-one
  corrupts every float: D73 (sinf/cosf `du` pairs → `DVAL()` macro),
  D112 (`d43_emit.py put_f32` `src[doff:doff+4][::-1]`).
- Header offset tables / pointers: D54 (cseq ALMidiHdr), D68
  (Globalimagetable), D87 (ramromfilestructure), D88 (Usetup* tables).
- Negative-terminated index chains (`PointUsage[]`) in converted model
  rodata cycle forever if element endianness/stride is wrong: D120
  (opcode-0x18 collision record, `d43_emit.py` — guarded, not fixed).
- Packed bitfields cross byte boundaries differently: D78 / D83
  (StandTile id/room, header mid/tail).
- **Rule:** prefer an offline sidecar converter (D43, D69, D88 pattern,
  `tools_pc/d*_emit.py`) over a runtime fixup for a whole format.
- **Python slice-assignment width is a silent corruptor** (D125):
  `out[a:a+8] = b"\x00\x00\x00\x00"` on a `bytearray` does NOT raise — it
  deletes `(8 - len(rhs))` bytes, shrinking `out` and shifting everything
  after `a` down. A widened pointer/`stan` slot filled with a 4-byte
  literal shrank `d88_emit.py`'s output 4 B per pad; once `len(out)` fell
  below a later region's write offset, `out[off:off+n] = data` clamped to
  an empty range at the current end and *inserted* there — string blobs
  drifted, boundpad names truncated (`p138d2`→`8d2`), `stanPackId` rejected
  → door `model=NULL` → crash. When emitting into a pre-sized bytearray,
  every `out[a:b] = rhs` MUST have `len(rhs) == b - a`; grep the emitter
  for width-mismatched slice writes when a converted region lands wrong.
- A one-shot "sync patched values from the ROM copy into a compiled
  shadow array" pass must key its slot-detection on a field that is still
  intact *at sync time*. D124: `gimgSyncCompiledGlobalDLs` looked for the
  `0xABCDxxxx` IMAGESEG marker in the ROM copy — but `texLoad()` had
  already overwritten every one of those with a real pointer, so the sync
  silently copied nothing and the compiled `globalDL_0xNNN` explosion
  DLs kept their link-time markers (latent on *all* levels; only tripped
  when an explosion/smoke/particle DL is first drawn). Detect the slot
  from the *destination* (compiled) array, which still holds the marker.
- A polymorphic-record converter with a per-type handler table + a
  "generic" fallback silently corrupts any type the table forgot: the
  fallback's word-granular bswap is wrong for sub-word fields. D122 —
  `d88_propdefs.py` had no arm for 6 `inherits ObjectRecord` propDef types
  (TINTED_GLASS/VEHICHLE/AIRCRAFT/TANK/AUTOGUN/AMMO); the generic arm
  bswap32'd the `[s16 obj][s16 pad]` word as one u32, putting the model id
  in the wrong half → OOB table deref. When you add a converter, enumerate
  *every* `type` byte a level can emit and assert the handler set is
  total; `[u16|u16]` / `[s16|s16]` packed words need a half-swap
  (`_hh_word`), never a 32-bit swap.
- **D123 corollary:** a pointer-width tail slot is **not always
  runtime-populated**. Some hold a small *integer id* in the ROM image
  that game code reads via that same field before overwriting it
  (`VehichleRecord/AircraftRecord.ailist`: `prop.c:1764/1786` does
  `x->ailist = ailistFindById(x->ailist)`). A converter that lumps every
  pointer-width tail slot into one "widen 4→8B, emit zeroes" bucket
  destroys the id → `ailistFindById(0)` silently returns global list 0
  (`GAILIST_AIM_AT_BOND`) → `ai()` runs a CHR aim list with a NULL chr →
  NULL deref in `chrIsNotDeadOrShot`. Before zeroing a widened slot,
  confirm the setup code writes it unconditionally; if it reads-then-writes,
  emit the value (`_bswap32` into the low 4 bytes, LE). Decide per field,
  not per type.

## C2. Port-layer / SDL shims

- `#include <PR/os.h>` in a port `.c`/`.h` that also sees `<errno.h>`
  breaks: `OSContStatus`/`OSContPad` have a `u8 errno;` field vs errno.h's
  macro. libultra.c wraps the include in `#pragma push_macro("errno")` /
  `#undef errno`; cleaner for a new module is to duplicate the handful of
  `CONT_*` bits it needs (D118 `input.c`).
- GE aims with **digital C-buttons**, no analog-aim hook outside `src/`.
  Mouse-look is bridged by integrating the relative-mouse delta into a
  clamped per-axis accumulator and emitting a C-button per poll while
  |accum| ≥ 0.5, draining one unit → proportional press *dwell*. Non-linear
  (GE's own accel curve) but playable (D118).
- `osContGetReadData(pad)` must fill **one OSContPad per channel**
  (`MAXCONTROLLERS`-long array), not just controller 0 — joy.c passes the
  whole `samples[i].pads` array (D118).
- Controller state has one source: `port/src/input.c`. `libultra.c`'s SI
  section marshals `inputComputePad()` into `g_contPad[]`; it is driven by
  `osContStartReadData` (per logic tick), no separate `video.c` frame hook.

## D. N64 hardware idioms fast3d does not emulate

- Z buffer cleared by pointing the colour image at it + fill-rect → does
  nothing in fast3d; must emit `G_CLEAR_DEPTH_EXT` (D105).
- LOD / detail mip tiles: fast3d fabricates a crop when detail textures
  are off → force base tile (D107).
- K0 segment-address folds (`OS_K0_TO_PHYSICAL | 0x80000000`) and
  hand-inlined `BG_SEG_TO_PTR` need the `(u32)` 32-bit wrap the macro has
  (D58, D84).
- Portal near-plane: z==0 clip points project to ±1e20; x86-64 float
  garbage can come back `min>max` / non-finite and slip past a
  degenerate-box check that clamps to full-screen on N64 (D106).
- **D74 wrap-block is DEAD CODE** (`gfx_pc.cpp:1546/1551`): guard is
  `cms & G_TX_WRAP` but `G_TX_WRAP == 0`, so always false (line 1887 does
  it right with `cms == G_TX_WRAP`). And if it did run it indexes
  `tex_width2[i]` etc. (arrays `[2]`, per-texunit) with the *vertex* loop
  index `i` 0..2 → OOB. Latent; harmless for CLAMP glyphs, would matter
  for wrapped textures on tris. Fix deferred (M-11) — logged in
  `docs/GRAPHICS-BACKLOG.md`.

## D2. The HUD/model X-mirror (D114/D116) — DO NOT re-static-trace

Sessions M-6/M-7/M-8/M-11 all reached the same wall: `textRenderGlyph`
→ GBI → fast3d `gfx_dp_texture_rectangle` → `buf_vbo` → GL are **each
runtime-verified non-mirrored** (`[D116/vbo]` probe: x-left↔u=0), yet
glyphs render X-flipped. M-11 additionally confirmed the ammo digits use
the *same* `textrelated.c` path (via `gunfire.c:5906`), no separate
renderer. Every prior attempt to "fix" it drifts toward a global
S-swap that mirrors the whole screen — a bad trade for a cosmetic bug.
**Next attempt needs a RenderDoc/apitrace capture of one glyph texrect
OR the asymmetric-1-texel-texture experiment — nothing else.** Cosmetic,
deprioritised below level-progression / crash work. See
`docs/GRAPHICS-BACKLOG.md`.

## E. Process / method notes

- Investigation loop is: reproduce → env-gated capped probe → root-cause
  → narrow `#ifdef PORT` fix → visual verify (`GE_PCDUMP` +
  `tools_pc/pixcount.py` vs `docs/reference/n64-footage-*`).
- gdb **launch** mode is too slow for timing-dependent faults; gdb
  **attach** to an already-running process is fine and fast.
- Check `PD_PORT_CHECKOUT` for the PD analogue before
  writing anything new — same Rare engine family.
- Don't re-investigate a closed §F finding or re-derive a format spec
  that already has a converter.
- **The port is NOT frame-deterministic** (D117): `osGetCount()` is
  wall-clock on PC, and GE is a variable-timestep sim
  (`frametiming.c waitForNextFrame` → `deltaFrames` = 60 Hz ticks of real
  time elapsed per render), so "frame N" differs 15–40 % between runs. Use
  `tools_pc/framediff.py <ppmdir>` (structural: 16×12 grid mean-colour +
  non-clear-% + aHash, `--mask X0,Y0,X1,Y1` for the HUD, `--update` to
  refresh `tools_pc/golden/`) — NOT an exact compare. A `GE_DETERM=1`
  fixed-tick mode was assessed not-narrow (redesigns retrace/tick
  semantics); design is in §F D117 if someone picks it up, after which
  `framediff.py --exact` becomes usable.
- **D24-implications — host-scheduling nondeterminism / fake priority
  semantics:** the pthread kernel does not enforce N64's 0–31 priorities
  (`osYieldThread` = `Sleep(0)`), so interleavings impossible on console can
  occur, and frame timing has host jitter. When a flaky timing bug appears
  (especially Phase 3 audio underruns), reach first for host thread
  priorities (`SetThreadPriority`: scheduler thread time-critical, tick
  normal) and the deferred `GE_DETERM` mode (D117) — not for kernel changes.
- **"Regression vs. steady state" is decided by a build-and-compare, not by
  reading an old handoff line** (D133). Handoff prose like "the entire intro
  renders" is often aspirational — the author's intent, not a measured coverage
  number. Before bisecting a suspected render regression: `git worktree add` the
  commit whose handoff made the claim (COPY `data/`, never junction — see the
  data-dir-junction-hazard memory), build, capture the *same* `GE_PCDUMP`
  window, `pixcount.py`. A **pixel-identical** non-clear count on a static 2D
  screen (e.g. the legal/disclaimer framebuffer, 6677 px here) across two
  independently-built binaries proves the path is unchanged → not a regression,
  don't bisect. Clean up the worktree with `git worktree remove --force` (safe
  only because `data/` was copied).
- **Watch item: `osYieldThread` = `Sleep(0)`.** The one place we fake
  cooperative behavior. If a level-sweep hot loop misbehaves under host load,
  this shim is the first thing to inspect. Keep as-is until then.
