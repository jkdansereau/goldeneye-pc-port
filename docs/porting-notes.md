# Porting notes — recurring N64→PC bug classes

A field guide to the bug classes that keep recurring when running big-endian
32-bit N64 game code, unmodified, on a little-endian 64-bit host. Terse by
design — each entry compresses a full investigation to a symptom, a fix, and a
grep heuristic for finding siblings. Each entry cites a `Dxx` label; the full
evidence and fix for that instance live in [`dev/findings.md`](dev/findings.md)
under the same label. Skim the section headers; read the classes relevant to
the task at hand.

If you are debugging a crash in this port, read this first — the odds are
good that you are looking at one of these.

## Contents

- [A. Pointer-width struct growth (32→64) — the dominant class](#a-pointer-width-struct-growth-3264--the-dominant-class)
- [B. 16-byte PC `Gfx` / `Vtx` vs 8-byte N64](#b-16-byte-pc-gfx--vtx-vs-8-byte-n64)
- [C. Big-endian rodata / ROM data read on little-endian PC](#c-big-endian-rodata--rom-data-read-on-little-endian-pc)
- [C2. Port-layer / SDL shims](#c2-port-layer--sdl-shims)
- [D. N64 hardware idioms fast3d does not emulate](#d-n64-hardware-idioms-fast3d-does-not-emulate)
- [D2. The HUD/model "X-mirror" — RESOLVED](#d2-the-hudmodel-x-mirror-d114d116--resolved-it-was-an-upside-down-capture)
- [D3. GCC/mingw makes an all-non-negative `enum` UNSIGNED](#d3-gccmingw-makes-an-all-non-negative-enum-unsigned)
- [D4. N64 "interrupts off" must be a real lock on PC](#d4-n64-interrupts-off-is-not-free-on-pc--it-must-be-a-real-lock)
- [D5. Loop bounds that assume linker adjacency of two globals](#d5-loop-bounds-that-assume-linker-adjacency-of-two-file-scope-globals)
- [E. Process / method notes](#e-process--method-notes)

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
  D140 (watch Model punned into a `struct player` field-run: PC
  `sizeof(struct Model)` grows so the "fields" that aliased `.render_pos` /
  `.scale` / `.animframe1` on N64 no longer overlap → NULL `render_pos` →
  pause-menu crash; fix = real inline `struct Model` + pool, redirect the
  named reads to the member — same as D100/D102),
  D119 (`weapons_held[]->chr` punned as `ChrRecord*` to read
  `.act_*.attack_item` — aliased `WeaponObjRecord.weaponnum` at 0x80 on
  N64 via act-union@0x2C+84; act union moves to ~0x38 on PC).
- **Open landmine:** raw hardcoded-offset accessors into `struct player`
  / `struct hand` — see `docs/dev/AUDIT-M6-player-offsets.md`.
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

- **D177 — a struct passed as `s32 *` and integer-indexed past a leading
  pointer member.** `stanCheckLinkedSpecialTile` takes the caller's
  `struct StandTileLocusCallbackRecord { s32 *rooms; s32 count; … }` typed
  as `s32 *outFlags` and does `outFlags[1] = 1`. On N64 `[1]` is `count`; on
  PC `rooms` is 8 bytes so `[1]` is its *high half* and `count` moved to
  `[2]` — the write is silently lost, the reader (`->count`) sees 0. Two
  tells for this class: (a) a function parameter typed `s32 *` / `u32 *` /
  `void *` that is really a named struct (check what the caller passes and
  what other consumers cast it to); (b) the matching stack local declared as
  a small "placeholder while matching" struct whose N64 size equals the real
  record — it silently under-allocates once any member widens. Fix both:
  cast to the real type + write fields by name, and declare the local as the
  real struct, under `#ifdef PORT`. (`[0]`/`rooms` often still works by luck
  on LE — low half at +0 — which masks the bug for one of the two fields.)

## B. 16-byte PC `Gfx` / `Vtx` vs 8-byte N64

Any buffer reservation, `memcpy` size, slot stride, or pool budget
expressed in N64 `Gfx`/`Vtx` units is **half-size** on PC.

- Instances: D50.6 (texCopyGdls copied only w0 of each 16-byte slot),
  D58 (DL reserve 0x100→0x200), D85 (`bgWidenRoomGdl` 8→16 + bswap),
  D95 (2× master-DL buffer + raised mempool ceiling).
- **D135 corollary — an unported GBI *parser* (not just a buffer size).** Code
  that walks a DL command stream with raw byte/word indices into an 8-byte N64
  `Gfx` (`*(s8*)gdl` for the cmd, `((u32*)gdl)[1]` for w1, `((u8*)gdl)[5..7]`
  for `G_TRI1` vtx indices, `((u32*)gdl)[0/1]` nibble reads for `G_TRI4`) reads
  the wrong bytes of the PC 16-byte `{u64 w0; u64 w1}` slot and desyncs on the
  first command → walks off the DL → wild deref. The N64 32-bit words survive
  in the LOW dword of each 64-bit field, so the mechanical port is: `u32 w0 =
  (u32)gdl->words.w0; u32 w1 = (u32)gdl->words.w1;` then replace every raw
  access with the equivalent shift/mask on w0/w1 (BE byte `i` of a word →
  `(word >> (8*(3-i))) & 0xff`). `gdl++` (advances by `sizeof(Gfx)`) is already
  correct. Watch `(s32)ptr` truncation in vtx-base math and `x | 0x80000000`
  KSEG0 folds (identity on PC — just drop the OR, and guard segmented w1).
  Instances: `bgTestHitOnObj` (`propobj.c`, FIXED); `bgTestRayIntersectionInRoom`
  + `bgTestBulletHitBackground` tail (`bg.c`, D154 — ported M-28, re-audited +
  bug-fixed M-30, playtest-gated).
  PD ground truth: `pd_port` uses `gdl->dma.cmd` + `GFX_W0_BYTE(i)`/`GFX_W1_BYTE(i)`
  macros (`3-i` / `11-i` on 64-bit LE).
- **D154 corollary — the PC `Gdma_le` shim's `.par` is NOT the N64 params byte.**
  When porting a room/model DL GBI parser, only `.dma.cmd` is safe to read
  through the `port/shim/PR/gbi.h` `Gfx` union: that shim lays `Gdma_le` out as
  `par:24` (bits 0-23 of word0 = the *packed length* from `gDma1p`), `cmd:8`
  (bits 24-31). The N64 `Gdma` has `cmd:8` (byte 0) then `par:8` (**byte 1**,
  bits 16-23 = the `((n-1)<<4)|v0` G_VTX params). So an N64 `((u8*)gdl)[1]` /
  N64-semantics `.dma.par` becomes `((u32)gdl->words.w0 >> 16) & 0xff` on PC —
  **`gdl->dma.par` gives bits 0-23 (length), and `& 0xf` on it is always 0**
  because `len == 16*n`. The original D154 port had exactly this bug (vtxoff
  forced to 0). Same trap latent in `bgBuildRoomVtxBounds` (`gdl.dma.par>>4&0xf`
  reads PC bits 4-7, not N64 bits 20-23 — currently tolerated). Rule: for
  anything but the opcode, extract the bit-field explicitly from
  `(u32)gdl->words.w0` / `.w1`, don't trust the named `.dma.*` sub-fields.
  Also: a `words.w0 << k >> m` bit-extract idiom that relied on 32-bit
  truncation on N64 must be `(u32)`-cast first on PC (`words.w0` is 64-bit).
- **Also bites RAW hardcoded struct-stride writes, not just Gfx/Vtx.**
  D128: `sub_GAME_7F0B37EC` did `((u8*)g_BgPortals)[(portal<<3)+6] |= 2`
  — the N64 `bg_portal_data_entry` is 8B (`ptr@0, cr1@4..cb2@7`); on PC
  the widened `offset_portal` ptr makes it 16B (`cr1@8..cb2@11`), so the
  write landed in the middle of another portal's pointer. Fix = use the
  struct accessor under `#ifdef PORT` (every other site already does:
  `g_BgPortals[portal].controlbytes1 |= PORTALFLAG_SPECIAL`). Grep for
  `<< 3` / `* 8` / `+ 6` style raw offsets into any struct that gained a
  pointer field.
  D141: `set_enviro_fog_for_items_in_solo_watch_menu` (`gunfire.c:1720`)
  indexed a `ModelNode*` array (`ModelFileHeader.Switches`) as
  `*(ModelNode**)((u8*)Switches + j + 0x48)` with `j += 4` — the `0x48`/
  `0x5c` and the step are 4-byte-pointer constants. PC 8-byte stride → wrong
  + misaligned slot → bogus non-NULL node → crash in `modelGetNodeRwData`.
  Fix: `Switches[18 + (j>>2)]` / `Switches[23 + (j>>2)]` under `#ifdef PORT`
  (`0x48/4=18`, `0x5c/4=23`). Grep every `(TYPE**)((u8*)arr + <const>)` and
  `arr[i << k]` where the array element is a pointer.

## C. Big-endian rodata / ROM data read on little-endian PC

ROM assets and compiled-in `.rodata` are big-endian. Anything not run
through a converter or a runtime bswap fixup reads scrambled.

- `f32` values are BE **word pairs** — a naive byte reversal off-by-one
  corrupts every float: D73 (sinf/cosf `du` pairs → `DVAL()` macro),
  D112 (`d43_emit.py put_f32` `src[doff:doff+4][::-1]`).
  - **D73 scope is now settled (M-32 D75 triage):** the whole
    `src/libultra/gu/` tree is endian-clean — only `sinf.c`/`cosf.c` used the
    `du` union and both are `DVAL()`-wrapped; `rotate/perspective/ortho/
    lookat/scale/translate/mtxutil/normalize/align` use plain float literals
    or pure integer bit-packing (`FTOFIX32`+shift/mask). The game's own
    `matrixmath.c` (`matrix_4x4_set_lookat*`, `_set_projection`,
    `_f32_to_s32`) is likewise native-LE (D114). **Do not re-audit gu or
    matrixmath for a "float endianness" bug** — if a matrix comes out wrong
    on PC the cause is upstream data, a struct-field pun (D100/D140/D156),
    never these files. (The "D114/D116 viewport mirror" example is withdrawn —
    M-33/D168 showed D114/D116 were an upside-down `GE_PCDUMP` capture, not a
    real flip.)
- Header offset tables / pointers: D54 (cseq ALMidiHdr), D68
  (Globalimagetable), D87 (ramromfilestructure), D88 (Usetup* tables).
- Negative-terminated index chains (`PointUsage[]`) in converted model
  rodata cycle forever if element endianness/stride is wrong: D120
  (opcode-0x18 collision record, `d43_emit.py` — guarded, not fixed).
- Packed bitfields cross byte boundaries differently: D78 / D83
  (StandTile id/room, header mid/tail).
- **Rule:** prefer an offline sidecar converter (D43, D69, D88 pattern,
  `tools_pc/d*_emit.py`) over a runtime fixup for a whole format.
- **D139 — `(u8)word` / `(s8)word` to grab "the first field" of a struct
  reads the WRONG byte on LE.** N64 code that does `(u8)obj[0]` (obj a
  `u32*`) to read a byte-3 field of a big-endian header word — e.g. the
  propDef type in `[u16 extrascale][u8 state][u8 type]` — gets the low byte,
  which is the *last* BE field. On LE it's `extrascale`. `cleanupObjects`
  did this for its walk-termination + type dispatch → never saw
  `PROPDEF_END` → ran off the blob → freed garbage → crash on stage unload.
  Fix: use the struct member (`pdef->type`) like every other consumer.
  Grep for `(u8)*`/`(s8)*`/`& 0xff` on a `u32`/`s32` that's really a
  serialized multi-field word — especially GBI-word and header-word reads
  that were never routed through a converter or `#ifdef PORT`.
- **D150 — `langGet()` NULL flows into a `str*` primitive.** The watch
  BRIEF/OBJECTIVES pages (and `front.c` mission text) build their strings with
  `strcpy`/`strcat(buf, langGet(id))`. `langGet` returns NULL on PC for any
  bank the current menu flow never loaded (D129/D143); N64 always resolves
  these ids so the decomp never guards. `strcat(buf, NULL)` then derefs 0.
  Fix = NULL-tolerant `strcpy/strncpy/strcat` under `#ifdef PORT` in `str.c`
  (NULL src → empty string, NULL dst → return). **Gotcha:** these are
  `__nonnull__` **builtins** to GCC — a plain `if (src == NULL)` on the
  parameter is deleted as provably-dead (confirmed in `-Og` disassembly:
  no `test` emitted). Launder the pointer through an empty `__asm__("":"+r"(p))`
  (`GE_IS_NULL()`) so the guard survives. Same class as the D143 textRender
  NULL guards. Audit any hand-rolled libc primitive in `src/` that a port
  NULL can reach.
- **D157 — a small value in the LOW BYTE of a BE `s32` word, read as `s8` at
  the word's last offset, reads 0 on LE after the converter byte-swaps it.**
  Same family as D151/D139. `struct objective_entry.difficulty` is `s8` at
  offset 0xF; the underlying type-23 propDef word 3
  (`MissionObjectiveRecord.MinDificulty`) is a BE `s32` whose value (0..3) sits
  in byte 0xF on N64. `d88_propdefs.py` `_bswap32`'s the word (correct — it IS a
  32-bit field), moving the value to byte 0xC on LE, so the `s8`@0xF read yields
  0 for *every* objective. Consequence was total: `get_difficulty_for_objective`
  → 0 for all → `objectiveIsAllComplete()` on Agent evaluated objectives that
  should be difficulty-gated out → never TRUE → `end_of_mission_briefing()` (the
  campaign-unlock EEPROM write) never fired → **no solo level ever unlocked the
  next.** Fix: reorder the struct's post-swap tail under `#ifdef PORT` so the
  named byte reads offset 0xC. **Grep every struct that reads a sub-`s32` field
  as `s8`/`u8`/`s16`/`u16` at a NON-zero in-word offset — after a converter
  bswap that offset is wrong.** Offline tools have the same trap
  (`dump_objectives.py` read byte 0xC = BE high byte = always 0).

- **D159 — an N64 "pre-swap for the RDP" texture massage is poison on PC because
  fast3d doesn't emulate the RDP.** `texSwapAltRowBytes` (`image.c`) pairwise-swaps
  the 8-byte (`u32`) groups of every **odd** texture row before upload, to cancel
  the N64 RDP's odd-line TMEM address XOR (address bit 2) that fires during
  4-byte-word `gDPLoadBlock` loads of I/IA/RGBA16 formats. fast3d has **zero**
  odd-row handling, so the pre-swapped odd rows upload scrambled → an 8-texel
  "venetian blind" / interlace comb. Invisible on small or distant textures,
  glaring on large 1:1 front-end images (wallet-Bond photo, passport crest).
  Fix: `#ifdef PORT` no-op the function — fast3d wants a plain linear image.
  General rule: any `src/` routine whose comment or shape says "for the RDP /
  TMEM / N64 hardware" and that reorders/massages bytes is a **port-layer**
  candidate (same family as the K0-fold and interrupt-mask shims), not game
  logic — audit `image.c` / `tex.c` for others (`texAlignIndices` row padding is
  load-bearing and must stay; the swap is not).
- **D160 — a "propDef command-index walk desync" is only possible when `sizepropdef()`
  and the offline converter disagree on a record size.** Since D122/D126/D132 closed
  every gap, `d88_propdefs.PROPDEF_PC_BYTES[t] == sizepropdef()×4` for all 48 types,
  the propDef stream tiles byte-exactly, and `setupGetPtrToCommandByIndex` /
  `tagGetCommandIndex` (`object = sizepropdef(object)+object`, `sizeof(PropDefHeaderRecord)==4`)
  walk in lockstep with the converter. Before hypothesising a propDef walk desync for a
  new symptom (M-31 exhaustively re-verified: every record type, all 21 levels,
  byte-exact tiling), run the stride cross-check first — if it's clean, the bug is
  elsewhere. The Dam rappel cutscene (D148) reached this wall: data path proven intact;
  residual cause is runtime AI-script control-flow / `CAMERAMODE_POSEND` cinematic
  render (D75 family). `GE_D160=1` diagnostic ships; needs a live Dam-to-exit playthrough.
- **D151 — a ROM-serialized `s32` slot decoded by the struct as `[u16 hi][u16 lo]`
  reads zero on LE.** N64 code frequently splits a 32-bit setup-stream word into
  `u16 reserved; u16 realvalue;` where the useful value is always small and lands
  in the **low 16 bits of the big-endian word**. The offline converter correctly
  `_bswap32`'s the word (it IS a 32-bit field), which puts the value in the low
  bytes — but the struct still reads `realvalue` from the *high* offset → 0.
  Instances: `struct watchMenuObjectiveText.text` / `struct objective_entry.text`
  (propDef types 35 / 23) — every watch briefing + objective line rendered blank
  (`langGet(0)` → NULL). Fix: `#ifdef PORT` widens the field to a full `u32` at
  the word offset; converter unchanged. Grep every ROM-serialized struct for a
  `u16 reserved`/`u16 pad` immediately before a `u16` the runtime actually reads.
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

- **Audit for ROM segments loaded raw with no fixup at all** (D178). Every
  `_fileNameLoadToAddr()` / `_fileNameLoadToBank()` call site loads a raw
  big-endian ROM image; the ones with a `struct` overlay need a BE→LE pass
  and there is nothing in the load path that supplies one — the fixups are
  bolted on per call site (`langFixupLoadedBank()` in `language.c`,
  `romdataFixup*()` in `port/src/romdata.c`). The briefing segment
  (`Ubrief*Z` = `u16 brief[4]` + 10 × `{u16 textid; u16 difficulty}`) had
  none for a year, and its failure mode is silent and *split*: a swapped
  string id (`0x2C04` → `0x042C`) still passes `!= 0`, so the loop body
  runs and `langGet()` quietly returns NULL → **blank text, not a crash**
  (that was the whole of the D143 "briefing/objective text is blank"
  residual); and a swapped small enum (`0x0001` → `0x0100 = 256`) turns a
  `>=` difficulty gate into an always-false filter, so lines silently
  *vanish* rather than render wrong. When a screen renders its own
  chrome/headers correctly but the file-driven rows are empty, suspect the
  raw file, not the renderer: dump the first words of the loaded blob and
  look for a byte-mirrored constant you can recognise from the asset `.c`
  source in `assets/obseg/`.

## C2. Port-layer / SDL shims

- `#include <PR/os.h>` in a port `.c`/`.h` that also sees `<errno.h>`
  breaks: `OSContStatus`/`OSContPad` have a `u8 errno;` field vs errno.h's
  macro. libultra.c wraps the include in `#pragma push_macro("errno")` /
  `#undef errno`; cleaner for a new module is to duplicate the handful of
  `CONT_*` bits it needs (D118 `input.c`).
- GE's aim model is **mode-dependent** (`bondview2.c bondviewProcessInput`):
  in **hipfire** (`!insightaimmode`) yaw = analog stick-X ("natural turn")
  and pitch = **digital C-up/C-down only** (stick-Y is move fwd/back); in
  **aim mode** (R held) yaw *and* pitch are analog — stick pushed past ±60
  → proportional `(stick-60)/10` — and **C-up/C-down mean crouch/lean/zoom,
  not aim**. So the mouse→pad map must also be mode-aware: aim mode pushes
  the stick into the 61..80 band and emits **no** C-buttons (emitting C-down
  for "look down" while aiming = crouch, D118c); hipfire keeps the digital
  C-button pitch. `input.c` reads its own RMB/LShift state as the mode proxy
  (exact for hold-to-aim; a toggle scheme needs `g_CurrentPlayer->
  insightaimmode`). GE's native pitch is **inverted** (C-up → look down) —
  hide it so mouse-down looks down; `MouseInvertY` flips (D118, M-24).
- `osContGetReadData(pad)` must fill **one OSContPad per channel**
  (`MAXCONTROLLERS`-long array), not just controller 0 — joy.c passes the
  whole `samples[i].pads` array (D118).
- Controller state has one source: `port/src/input.c`. `libultra.c`'s SI
  section marshals `inputComputePad()` into `g_contPad[]`; it is driven by
  `osContStartReadData` (per logic tick), no separate `video.c` frame hook.

## D. N64 hardware idioms fast3d does not emulate

- **Appending a port-owned 2D overlay to the game frame: hook inside
  `gfx_run()`, not `videoSubmitCommands`.** `gfx_run()` (`gfx_pc.cpp`) is
  monolithic — it does `start_frame` → `gfx_run_dl` → `gfx_flush` →
  `end_frame` → `swap_buffers_begin` in one call, and the game DL is executed
  from `osSpTaskStartGo` (`libultra.c`), *not* through `videoSubmitCommands`
  (which is dead). So a second `gfx_run()` per frame is not possible (it swaps
  buffers). The clean seam is a one-line C hook between `gfx_run_dl(commands)`
  and `gfx_flush()` that runs a second `gfx_run_dl()` on a port-built DL when
  non-NULL (D184, F10 options overlay). The overlay DL is self-sufficient:
  `gDPPipeSync` + `gDPSetCycleType(G_CYC_1CYCLE)` + `gDPSetTexturePersp(G_TP_NONE)`
  + a full-screen `gDPSetScissor` + `microcode_constructor()` (the game's own
  2D combiner/rendermode prologue), then `gDPFillRectangle` / `textRender`
  (game symbols, extern'd like `input.c` externs `current_menu` — UI, not
  logic). 2D pixel space is `viGetX()` x `viGetY()`. Return NULL when the
  overlay is closed so the frame is byte-for-byte unchanged.

- **Verbatim RDP triangle commands chopped into `gImmp1(G_RDPHALF_1/_CONT/_2)`
  pairs** (GE's `skyRenderTri`/`skyRenderFull` hand-build an edge-walked
  `G_TRI_FILL`/`G_TRI_SHADE_TXTR` for the modified RSP ucode to reassemble) are
  silently dropped by fast3d — it has no RDP triangle rasteriser. Substitute a
  normal `gSPVertex` + `gSP*Triangle` batch behind `#ifdef PORT`: if the game
  code already screen-space-projected the verts, re-emit them under a
  `guOrtho(l,r,b,t,…)` + identity modelview matching the projection's pixel
  extents, `#else` keeps the stream verbatim (D176(a), M-46).
- Z buffer cleared by pointing the colour image at it + fill-rect → does
  nothing in fast3d; must emit `G_CLEAR_DEPTH_EXT` (D105).
- LOD / detail mip tiles: fast3d fabricates a crop when detail textures
  are off → force base tile (D107).
- **A CI-format tile drawn with the TLUT disabled (`G_TT_NONE`) is not a
  palette texture** — the N64 RDP feeds the raw TMEM texel straight into the
  colour pipe, i.e. a CI8+`G_TT_NONE` tile behaves as I8. fast3d's
  `import_texture()` dispatched purely on `tile.fmt` and did a palette lookup
  against a stale `rdp.palette` → garbage (D161, GE Depot ceiling = blue
  speckle). Fix: when `rdp.palette_fmt == G_TT_NONE`, route CI4/CI8 → I4/I8.
- **RC3 / D167 — GL `GL_REPEAT` wraps at the uploaded image size; the N64 RDP
  wraps a render tile at `1<<masks` (`= ceil(log2(dim))` for GE, `texDimensionToMask`).**
  Equal for power-of-two textures, so this is invisible almost everywhere — but a
  non-PoT wrapping surface (Depot's 65×65 / 96×48 room textures) repeats at the
  next power of two on console and one image-width too soon in the port ("textures
  repeat oddly", squashed pattern, wrong seam). fast3d didn't even keep `masks`/
  `maskt`. Fix lives behind `Video.WrapFix` (default OFF, `GE_WRAPFIX` env
  override): store the mask, fold the UV at `1<<mask` in the hoisted per-texunit
  pre-wrap block, clamp the no-real-texels `[dim,1<<mask)` overflow band to the
  edge. The overflow band is a TMEM smear on real hardware — not exactly emulable
  in a fixed GL sampler, so this is an approximation. Any new fast3d UV/wrap work:
  the tile `mask` fields are the wrap period, NOT the tile-window or image size.
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
  for wrapped textures on tris. Reworked M-30 (hoisted out of the vertex loop,
  indexed by texunit, gated on `Video.WrapFix`); RC3/D167 adds the non-PoT
  mask-period case to the same block. Still default OFF.

- **`assert()`-based `SUPPORT_CHECK` is a silent no-op in the release build.**
  `port/fast3d/gfx_pc.cpp:39` defines `SUPPORT_CHECK(x)` as `assert(x)`, and the
  PC build compiles with `NDEBUG`. Every `SUPPORT_CHECK` in the file therefore
  documents an assumption that is *never* enforced — when it is violated the
  code silently reads wrong data instead of aborting. Seven of them assert
  `full_image_line_size_bytes == line_size_bytes` in the `import_texture_*`
  family (D183); treat any `SUPPORT_CHECK` as a **TODO comment**, not a guard.
  Corollary for triage: "there is an assert for that, so it can't be happening"
  is never valid reasoning in fast3d.

- **Diagnosing a "wrong texture" needs the *raw importer input*, not the
  uploaded RGBA.** `GE_TEXDUMP` dumps the post-decode image, which cannot
  distinguish "the decoder is wrong" from "the source bytes are garbage".
  `GE_TEXRAW=1` (D183) dumps the bytes as handed to `import_texture_*`. The
  cheap offline test on such a dump: compute the mean vertical
  neighbour-difference at every candidate row pitch — a correctly-pitched real
  image has a sharp minimum at its true pitch (≈0.2–0.7 on a 0–15 nibble
  scale), a pitch/shear bug has the minimum at a *different* pitch, and genuine
  noise data is flat (~3.7) at every pitch. That three-way split settles
  decode-vs-pitch-vs-source-data in one pass with no rebuild.

## D2. The HUD/model "X-mirror" (D114/D116) — RESOLVED: it was an upside-down capture

**M-33 (finding D168).** There was no mirror. `gfx_opengl_dump_bound_fbo`
(`port/fast3d/gfx_opengl.cpp`) wrote `glReadPixels` output — bottom-row-first,
GL origin — straight into a top-row-first P6 PPM, so **every `GE_PCDUMP` and
F12 capture was vertically flipped**. Sessions M-6/M-7/M-8/M-11 kept finding
`textRenderGlyph` → GBI → fast3d → `buf_vbo` → GL **each verified
non-mirrored** — because nothing *was* mirrored; they were staring at
upside-down screenshots of asymmetric content (text, ammo digits, guard skins,
the Nintendo logo) and reading "inverted" as "X-mirrored". The developer
confirms the game renders correctly on real hardware.

The lesson worth keeping: **when every stage of a pipeline probes clean but
the output "looks wrong", suspect the observation tool before adding a
correction.** A cosmetic defect that no probe can localise after four sessions
is a strong signal that the defect isn't in the code. Fix: PPM writer now
emits rows top-to-bottom; `tools_pc/golden/*.png` were flipped to match.

## D3. GCC/mingw makes an all-non-negative `enum` UNSIGNED

The N64 toolchain treats `enum` as signed `int`; GCC on the PC target gives
an enum whose enumerators are all ≥ 0 an **unsigned** underlying type. Any
descending loop that relies on the counter going negative to terminate then
spins forever:

```c
for (s = SP_LEVEL_EGYPT; s >= SP_LEVEL_DAM /* == 0 */; s--)   // never ends
```

- Symptom: a silent hang (kernel-heartbeat stall, no crash log) inside a
  loop over an enum range; the counter holds a huge value in gdb.
- Instances: **D142** — `LEVEL_SOLO_SEQUENCE` in
  `fileGetHighestStageDifficultyCompletedForFolder` froze the SELECT FILE
  screen. `DIFFICULTY` was already safe (`DIFFICULTY_MULTI = -1`).
- Fix: add a never-used negative sentinel enumerator under `#ifdef PORT`
  (`SP_LEVEL__PORT_SIGNED = -1`) — forces the type signed, first real
  enumerator stays 0, `sizeof` stays 4, no stored value changes. Audit any
  `enum` used as a descending / `>= 0` loop counter or in ROM-serialized
  structs where signedness matters.

## D4. N64 "interrupts off" is not free on PC — it must be a real lock

`osSetIntMask(OS_IM_NONE) … osSetIntMask(saved)` on N64 makes a region
atomic w.r.t. every interrupt (audio, VI, SI). libaudio, the scheduler and
a few others use it as their **only** mutual-exclusion primitive. On PC the
"audio interrupt" is a real preemptible thread (`amMain`), so a no-op
`osSetIntMask` shim = no mutual exclusion = concurrent linked-list mutation.

- Symptom: hang (spin) inside a list walk that another thread is editing —
  e.g. **D147**: `alEvtqPostEvent` (main thread, via `sndPlaySfx` on a door
  close) vs `sndRemoveEvents` (`amMain`) on the same `ALEventQueue`.
- Fix: `osSetIntMask` → one process-wide **recursive** mutex.
  `OS_IM_NONE` acquires; the `OS_IM_ALL` token returned from that call is
  what every paired restore passes, so `OS_IM_ALL` releases; other specific
  masks (`OS_IM_VI`) pass through. Safe because the decomp never blocks
  while holding `OS_IM_NONE` (N64 contract).
- **D152 — the recursive-mutex model is fragile; it CAN deadlock.** The
  "decomp never blocks while holding `OS_IM_NONE`" assumption fails on
  heavy `ALEventQueue` paths: libaudio has unbalanced / early-`return`
  `osSetIntMask` calls, and a transient thread can acquire `OS_IM_NONE`
  and exit without the paired `OS_IM_ALL`, leaving `s_imLock` owned forever
  → every later `alEvtq*` on `mainThread` + `amMain` blocks in
  `pthread_mutex_lock` → hang. Seen on the **mission-failed audio
  fade-out** (`sndSetScalerApplyVolumeAllSfxSlot` → per-frame
  `alEvtqPostEvent` storm) = permanent black screen. **Mitigation shipped
  (M-28):** `osSetIntMask` now tracks owner/depth under a short bookkeeping
  mutex + condvar; a waiter blocked > 2 s **steals** the section and logs
  the stale owner + the stealing caller's return address. Self-heals any
  leak (worst case ~2 s audio hiccup). Proper narrow fix (dedicated
  `ALEventQueue` lock) still owed — do it once the steal-log names the
  leaking call site. §F D152.
- **D152 addendum (M-31 static audit).** Every `osSetIntMask(OS_IM_NONE)` in
  the *compiled* audio code was audited: `event.c` (`alEvtqNextEvent`,
  `alEvtqPostEvent` — its lone early `return` at `event.c:86` **does** restore
  first —, `alEvtqFlush`, `alEvtqFlushType`), `csplayer.c` `__CSPRepostEvent`,
  `synaddplayer.c` `alSynAddPlayer`, `snd.c` `sndRemoveEvents` / `sndSetupSound`
  / `sndDeactivateAllSfxByFlag`. **All balanced on every path** — the §F guess
  "libaudio has unbalanced early-return mask paths" is *not* borne out. Two
  real problems remain and were fixed `#ifdef PORT`:
  (1) `sndSetSfxSlotVolume` (`snd.c`) walks the live `ALSoundState` list and
  posts to the shared `ALEventQueue` **without** holding `OS_IM_NONE` — unlike
  its structural twin `sndDeactivateAllSfxByFlag`, which does. On PC that is an
  unguarded walk racing `amMain` *and* one lock acquire/release per matching
  sound; during the mission-failed fade (`sndSetScalerApplyVolumeAllSfxSlot`
  → `sndApplyVolumeAllSfxSlot` → this, per frame) that is a per-frame
  lock-acquire storm — exactly what the §F dump means by "hammers the queue
  hard enough that the lock is left owned." Fix: hold the mask once across the
  whole walk (nested `alEvtqPostEvent` then hits the recursive fast path);
  also wrap `sndApplyVolumeAllSfxSlot`'s slot loop so the whole update is one
  recursive hold.
  (2) The actual leak is the "transient thread acquired `OS_IM_NONE` and
  exited" case (the dump shows `New Thread`/`exited` churn). `portThreadWrapper`
  now calls `imThreadExitRelease()` after the thread's entry returns: if that
  thread still owns `s_imHeld`, release the orphaned section immediately (+
  `LOG_ERROR`). Removes the wedge *and* the 2 s steal hitch, and stops the
  re-wedge that happens when a new host thread reuses the dead thread's
  pthread id and `imAcquire` mis-detects recursion. Steal-lock stays as the
  last-resort backstop for a genuinely unbalanced same-thread path.
  **Rule:** when two sibling functions walk the same list and post to the same
  queue, they must take the same lock — grep for one holding `OS_IM_NONE` and
  the other not.

## D5. Loop bounds that assume linker adjacency of two file-scope globals

N64 decomp sometimes ends an array walk with `end = &nextGlobal;` where
`nextGlobal` is the *next* file-scope definition in the `.c`. The N64
toolchain emits `.data`/`.bss` in source order so `&nextGlobal ==
array + ARRAY_COUNT(array)`; mingw/GCC on the PC target **reorders**
globals, so `end` can land before the array (loop runs 0–1 times) or far
past it (walk off the end).

- Instance: **D164** — `constructor_menu00_legalscreen` (`front.c`) bounds
  the 12-line legal-screen text loop on `&legalscreen_MRD`, which mingw
  links 0x60 bytes *before* `legalpage_text_array` → only line 1 renders
  (== the D76 "disclaimer half-drawn" bug; it was never an image-table
  issue). Fix: `#ifdef PORT` uses `array + ARRAY_COUNT(array)`.
- Grep for `= &` / `(TYPE *)&` on the RHS of a loop-terminator compare, and
  any `for`/`while`/`do` whose end pointer is the address of a *different*
  symbol than the one being iterated.

## E. Process / method notes

- Investigation loop is: reproduce → env-gated capped probe → root-cause
  → narrow `#ifdef PORT` fix → visual verify (`GE_PCDUMP` +
  `tools_pc/pixcount.py` vs `docs/reference/n64-footage-*`).
- gdb **launch** mode is too slow for timing-dependent faults; gdb
  **attach** to an already-running process is fine and fast.
- Check the [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) for the PD analogue before
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
- **Anything blocking on the scheduler thread throttles the whole sim**
  (D186, cf. D134). The gfx task runs synchronously on `src/sched.c`'s
  thread, and that same thread delivers VI-retrace events; any `sysSleep` /
  busy-wait on that path (frame-rate cap, a lock, an asset load) stops
  retrace delivery, so every game thread blocked on `osRecvMesg(retraceQ)`
  stalls with it. On console the CPU never waits on RSP/RDP/VI. A PC frame
  cap must therefore drop *presented* frames without sleeping that thread —
  `Video.FpsCap < 30` is currently just refused (clamped to uncapped) as a
  stopgap.
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
- **D134 — a dropped `OS_MESG_NOBLOCK` "interrupt" event is a permanent
  stall, and it looks exactly like D117 flakiness.** The port's SP/DP
  task-done events shared the scheduler's 8-slot `interruptQ` with the 60 Hz
  VI pacemaker. A gfx task runs the whole frame *synchronously on the sched
  thread*, so a slow frame lets the pacemaker fill the queue and the done-post
  is silently discarded → `curRSPTask` never clears → no further frames
  (`frames=2` heartbeat, retrace queue pinned `valid=8/8 ret=-1`). Rule:
  classify every posted event as **droppable** (retrace — N64 drops these too)
  or **must-arrive** (task done, DMA done); a must-arrive event needs
  guaranteed delivery, and `OS_MESG_BLOCK` is wrong whenever the poster runs
  on the queue's only consumer thread (self-deadlock) — drop the oldest
  droppable message instead, and reserve slots in the pacemaker.
  **Before blaming a hang on D117 / machine load, run it N times and count**:
  this reproduced 2 of 3 boots on an idle machine and had been written off as
  sweep flakiness for ~8 sessions.
- **Watch item: `osYieldThread` = `Sleep(0)`.** The one place we fake
  cooperative behavior. If a level-sweep hot loop misbehaves under host load,
  this shim is the first thing to inspect. Keep as-is until then.
- **D155 — an unclamped per-frame `deltaFrames` from wall-clock `osGetCount()`
  spirals the sim into a "hang".** The N64 was VI-locked so `waitForNextFrame()`
  never returned more than ~2 elapsed frames. On the port `osGetCount()` is
  wall-clock (D117), so any real-time stall (asset load at a stage/cutscene
  boundary, host thrash) makes `nextFrameTime` → hundreds/thousands. That value
  flows through `speedgraphframes` → `g_ClockTimer` and drives *every*
  `modelTickAnim` `while (numticks--)` and `for (i=0; i<g_ClockTimer; i++)` loop
  in the sim → one render becomes seconds of catch-up → heartbeat "hang", and
  the slow frame feeds an even bigger delta next time → unrecoverable. Symptom
  in a hang dump: `mainThread` parked in `modelTickAnim`/`modelConstrainOrWrap
  AnimFrame` under `chrTick`/`playerTick`/`lvlRender` across every dump. Fix:
  clamp `nextFrameTime` under `#ifdef PORT` in `waitForNextFrame()`
  (`FRAMETIMING_PORT_MAX_CATCHUP` = 6). Any other place the port lets an N64
  "number of elapsed ticks" value run unbounded is the same latent trap — grep
  for `g_ClockTimer` / `speedgraphframes` / `lvupdate*` consumers that loop.
  §F **D155**.
- **D156 — a NaN / blown-up `f32` frame or speed drives an unbounded
  frame-stepping loop → hang.** `modelSetAnimFrame2WithChrStuff`'s `while (1)`
  (`model.c:3131`) walks one anim frame at a time from `framea` to `frameb`;
  `frameb` comes from `modelTickAnim`'s `frame += playspeed * speed` per tick.
  A cutscene anim transition with a near-zero blend/`timespeed`/`unkb0`
  denominator (`model.c:3436`/`:3477`, guarded `> 0` but not against tiny
  values) makes `model->speed`/`playspeed` huge or NaN → `frameb` huge/NaN →
  `floorFloatToInt` garbage → ~2^31 iterations → frozen (`frames=N` constant
  in the hang dump, distinct from D155's slowly-incrementing spiral). Fix:
  `#ifdef PORT` finiteness+magnitude guards (`!(x > -1e6f && x < 1e6f)` also
  catches NaN) on `frameb` before the loop and on `frame`/`frame2` before they
  reach `model` state. **When an N64 float pipeline feeds a loop bound or an
  array index on PC, guard it** — the console's fixed timestep + bounded
  anim/physics data never produced the degenerate value, so the decomp never
  checks. Suspect a misaligned `Model`/struct field (D100/D140 pun family) as
  the NaN source before blaming the data. §F **D156**.
