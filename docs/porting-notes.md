---
title: Porting Notes
description: A field guide to the recurring N64-to-PC bug classes hit while running unmodified big-endian game code on a 64-bit host, each with its symptom and fix.
---

# Porting notes — recurring N64→PC bug classes

A field guide to the bug classes that keep recurring when running big-endian
32-bit N64 game code, unmodified, on a little-endian 64-bit host. Terse by
design — each entry compresses a full investigation to a symptom, a fix, and a
grep heuristic for finding siblings. Each entry cites a `Dxx` label; the full
evidence and fix for that instance live in
[`dev/findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md)
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

- Symptom: garbage pointer deref, corrupted neighbor field, delayed
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

**A1. Raw-byte aliases into a union (`act_ubytes.padding[N]`) — the silent
variant.** The nastiest members of this class do not crash: a literal byte
index into a union that overlays pointer-bearing structs still *reads*, it
just reads the wrong byte, so the game runs and merely behaves wrong. Grep
for hardcoded indices into any `padding[]`/`u8[]` union arm.

- **D209** (the D193 root cause, and the highest-impact instance found so
  far): `self->act_ubytes.padding[45]` aliased `act_gopos.unk59`, the
  SPEED tier. `act_gopos` = `{coord3d@0, StandTile *@12, waypoint *@16,
  waypoint *[6]@20, u8 curindex@44, u8 unk59@45}` on N64; at 64-bit the
  three pointer members widen and `unk59` moves 45 → 81, so the literal 45
  lands on **byte 5 of `waypoints[1]`**. With a low-4GB arena
  (`0x00000000_70xxxxxx`) that byte is **always 0x00** — a stable, plausible
  value, not garbage. Effect: the locomotion-animation selector received
  tier 0 forever and every AI character in the game was bound to a *walk*
  animation; since GE travel is anim-root-motion driven, all AI moved at
  walk pace on every level. No crash, no log, no NaN — it took a runtime
  probe comparing the *commanded* tier (named field, correct) against the
  *bound* animation (alias, wrong) to see it.
- **D210** (sibling, found by the post-D209 sweep): `chrToPatrol` inits
  `act_patrol.lastvisible60` via `act_init.padding[0x13]` (union byte `0x4c`).
  `act_patrol` starts with a `patrol_path *path` — widens 4→8, so `padding[0x13]`
  no longer hits `lastvisible60` (now `0x50`); it lands in `waydata` and
  `lastvisible60` is left uninitialised → the patrol "haven't seen the player
  recently" timer reads garbage for the first ticks. Fix: named field under
  `#ifdef PORT`. **NB the alias need not land on a *pointer byte*** (as D209
  did) — here it lands on a plain float that just isn't the field intended.
  The tell is the same: a hardcoded index into `padding[]` right next to
  named writes of the *same* arm. The rest of the `padding[N]` sweep
  (`chrlvTickAnim`/`chrlvTickDead` → `act_anim`/`act_dead`, both pointer-free)
  is layout-stable.

**Lesson.** When a value reads as a clean constant (0, 1) rather than
garbage, a pointer byte is a prime suspect — the high bytes of a low-4GB
heap pointer are all zero, so a misaligned read looks like a legitimate
"feature off" value. Prefer the named field under `#ifdef PORT`; the alias
is only correct at 32-bit pointer width. Also: verify that every caller
reaches the site with the union arm you are naming actually active.
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
  D191: `bondviewSelectCuff` (`bondview2.c`) — the **same** `Switches`
  array, missed by D141. `offset = switchindex << 2` then
  `base = (ModelNode**)((u8*)switches + offset)`, deref'd `base[0..5]`. On PC
  the 8-byte slot stride makes `base[N]` a half-word-swapped pointer
  (`0x70267a90` → `0x70267a9000000000`) → crash in `modelGetNodeRwData` when
  the player fires with a fresh-save Bond model (Bunker ii first guard,
  Statue post-cutscene). Fix: `offset = switchindex * sizeof(ModelNode *)`
  under `#ifdef PORT`. When you fix one `arr[i<<2]` pointer-array site, grep
  the whole file — these travel in packs.

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
- **D219 (M-112; the finding itself resolved in M-115 — kept as a triage lesson) — "the shared texture decoder is proven correct" is
  a claim about *pixel format*, not *compression method*, and the two are
  independent knobs on the same runtime `texLoad`.** `src/game/image.c`'s
  non-zlib texture decompressor reads a per-image 4-bit `TEXCOMPMETHOD_*`
  from the bitstream (uncompressed / huffman / huffman-per-channel / RLE /
  lookup / huffman-lookup / RLE-lookup / huffman-blur / RLE-blur) independent
  of the 4-bit pixel-`format` (RGBA32/16, IA16, I8/4, CI8/4 …). Two textures
  sharing a pixel format can still take completely different code paths:
  `_HUFFMAN`/`_HUFFMANPERHCHANNEL`/`_RLE` go through `texChannelsToPixels`
  (per-channel planes in R,G,B,A order), while `_LOOKUP`/`_HUFFMANLOOKUP`/
  `_RLELOOKUP` go through `texBuildLookup`+`texInflateLookup*` (a packed
  color table, bits read directly in final pixel layout). "Every other
  RGBA16 texture in the game renders correctly, so the decoder can't be the
  bug" is **not** a valid inference unless you've confirmed the *comparison*
  texture used the *same* `TEXCOMPMETHOD_*` as the suspect one — they can
  silently diverge into different functions despite an identical
  `G_IM_FMT_RGBA,G_IM_SIZ_16b` DL declaration. Also: whether a texture's
  `G_SETTIMG` marker is baked directly into a compiled display list
  (`gsDPSetTextureImage(...IMAGESEG(id))` in `assets/*.c`, fixed up by
  `gimgFixupGlobalimagetable`'s DL-bswap pass) vs. synthesized at runtime
  from an `sImageTableEntry`'s `index` field (`texSelect`,
  `src/game/othermodemicrocode.c`) is a real, checkable difference in
  *resolution* path worth ruling in/out early — but both funnel into the
  same `texLoadFromDisplayList`→`texLoad` call, so it is a weaker lead than
  the compression-method one. See `docs/dev/findings.md` D219 for the full
  writeup and an env-gated (`GE_D219RAW`) diagnostic left in the tree to
  check this on the next session that can capture runtime output.
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
  **M-106 addendum:** independently re-derived (not just re-cited) — `CutsceneRecord`
  (propDef type 46) has zero pointer/union fields, so it cannot exhibit this class at
  all; confirms the M-31 stride audit rather than superseding it. D148/D160's residual
  cause is still unlocated (needs the live trace); see D173 for a related negative
  result on the puppet-position side.
- **D132/D126 corollary — a zeroed ROM-serialized pointer-width tail slot is not
  automatically a converter bug; check the N64 source literal first (M-106).**
  Before assuming an offline converter wrongly zeroed a widened pointer field
  (the D122/D126/D132 "dead-on-load" pattern), grep the asset `.c` source tables
  (`assets/obseg/**/*.c`) for that field's literal initialiser. `PadRecord.stan`
  (`src/bondtypes.h:1744-1751`) looked exactly like a D126/D132 miss — a ROM-
  serialized tail pointer, zeroed by `tools_pc/d88_emit.py`'s `emit_pad()` — but
  every `PadRecord` in every checked setup table (`UsetupdamZ.c` etc.) initialises
  `stan` to literal `0`; N64 never carried a live pointer there either, so the PC
  zeroing is byte-identical to ground truth, not a regression. The tell that saves
  the trip: if the *source*, not just the ROM binary, always writes `0`/`NULL` for
  a field, it was never "dead-on-load" in the D123-corollary sense (a field that
  reads a real small int/id before being overwritten) — it's simply always
  runtime-computed, on both platforms, and a NULL seed into whatever consumes it
  is the original game's normal case, not a port defect.
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
  extents, `#else` keeps the stream verbatim (D176(a), M-46). **Caveat
  (D176(a), fidelity pass):** a plain `w=1` ortho is only correct when every
  vertex in the primitive really does share one depth/scale. The RDP's own
  coefficient block for this idiom carries `S·w′/T·w′/w′` per vertex (a real
  perspective-correct texture unit divides it back out per pixel) whenever
  the upstream game math computed S/T from a *world-space* projection without
  dividing by `w` itself (GE's sky does — `unk20/24` are texel coords on the
  world cloud-plane intersection, not pre-divided). Re-emitting those under
  `w=1` makes fast3d's GPU pipeline interpolate texture coords *linearly in
  screen space* instead of perspective-correctly — invisible when the
  primitive's per-vertex `w` barely varies, but a primitive spanning
  near-camera to horizon-grazing geometry can have >30x `w` spread between
  its own vertices, and linear interpolation then smears the entire S/T
  delta evenly across every pixel instead of concentrating it near the
  far/horizon vertices — reads as "tiles too densely / scrolls too fast"
  everywhere instead of a gentle gradient. Fix: build a small custom
  projection matrix that gives each vertex its real `w` (any per-vertex
  camera-space `w` the game math already computed) while keeping the same
  screen-space `x,y` (`clip.xy = ndc.xy·w`, `clip.w = w` — the GPU's normal
  divide recovers the intended `ndc.xy` regardless of `w`, only the
  interpolation changes); `Vtx.ob[]` is `s16` so pre-scale by a per-primitive
  `wScale` and have the matrix multiply it back in. **Follow-on caveat
  (D227, M-95): compute that `wScale` once per shared-vertex group, not
  once per triangle.** If a decompiled routine fans one shared vertex pool
  out across several separate calls to a per-primitive perspective-
  correction helper (one call per triangle instead of one call for the
  whole fan) and each call derives its own scale factor, two triangles
  sharing an edge/vertex can each independently pick a different scale —
  and because the scale feeds into a low-precision (`s16`) vertex field,
  the same logical vertex quantizes to two slightly different values
  depending on which call computed it. The position math stays exact for
  any scale (it's just a rescale that cancels out algebraically), so this
  isn't a logic bug and won't show up as wrong geometry — it shows up as a
  visible crack/seam at the shared edge, easy to misread as "two separate
  things happening" rather than one quantization mismatch. Fix: hoist the
  scale computation to scan every vertex in the whole shared group once,
  before the per-triangle calls, and thread that one value through instead
  of letting each call recompute its own. **Follow-on caveat #2 (D227,
  M-98): fixing ONE per-primitive-derived quantity this way doesn't fix
  every one.** The same routine can derive more than one such "shared state
  from the whole vertex group" value — GE's sky code also floors S/T into a
  per-primitive range fold to keep it in `s16` (`floorf(minS/64)*64`,
  `sky.c`'s `skyPortRenderPoly`). Sharing `wScale` across the fan and
  leaving the fold per-call left the exact same seam-shaped bug alive one
  field over: nominally-identical shared vertices still bake different
  absolute values (here, off by an exact multiple of the fold period) into
  each triangle's own vertex buffer. When you find one "derive this from
  only my local call's inputs, should derive from the whole shared group"
  bug in a routine, grep the same function for every other value computed
  the same way (any per-call min/max/floor/round over a subset of a larger
  shared vertex/primitive pool) — don't assume fixing the first one you
  found closes the whole defect class.
- Z buffer cleared by pointing the color image at it + fill-rect → does
  nothing in fast3d; must emit `G_CLEAR_DEPTH_EXT` (D105).
- LOD / detail mip tiles: fast3d fabricates a crop when detail textures
  are off → force base tile (D107).
- **A CI-format tile drawn with the TLUT disabled (`G_TT_NONE`) is not a
  palette texture** — the N64 RDP feeds the raw TMEM texel straight into the
  color pipe, i.e. a CI8+`G_TT_NONE` tile behaves as I8. fast3d's
  `import_texture()` dispatched purely on `tile.fmt` and did a palette lookup
  against a stale `rdp.palette` → garbage (D161, GE Depot ceiling = blue
  speckle). Fix: when `rdp.palette_fmt == G_TT_NONE`, route CI4/CI8 → I4/I8.
- **CI texture cache must key on palette CONTENT, not just the TLUT source
  address (D217).** GE assembles weapon/character model DLs in scratch arena
  RAM and reissues `gDPLoadTLUT` from a *repeated* source address with different
  palette content between materials and frames. fast3d keyed CI textures on
  `{texel addr, palette_addrs[0/1], palette_index, size_bytes}` with no content
  check → a later material takes a stale cache HIT and samples the GL texture
  decoded for an earlier palette. Fix: FNV-1a the 512-byte `rdp.palette` in
  `gfx_dp_load_tlut` (rare vs draws) and fold that hash into the CI cache key.
- **GE model-DL TLUT payloads carry a 2-entry leading zero pad (D217, still
  open).** Every larger weapon/chr model TLUT decodes with `entry[0]==entry[1]
  ==0x0000` and real colour from index 2. `count=1` model TLUTs that feed
  palette index 0 (solid-fill weapon parts — the PP7 grip) are the canary: if
  the port's model-TLUT source pointer is even 4 bytes off, index 0 reads
  adjacent texel/blob bytes and the part renders a wrong flat colour (green on
  Facility/Depot, black-by-luck on Bunker1) while larger tiles look plausible
  because they never index the low entries. Fix is upstream (segment resolution
  or `tools_pc/d43_emit.py` blob placement), not in the fast3d decode.
  **M-88 correction: the "segment resolution / `d43_emit.py` blob placement"
  theory above is refuted.** Traced the full reference chain: model files
  store textures as a numeric `TextureID` (`ModelFileTextures`,
  `src/bondtypes.h`) into the **global** `Globalimagetable` (fixed once at
  runtime by `port/src/gimgfixup.c`, D68 — not per-model, not per-level, and
  not touched by any `tools_pc/d43_*.py` script); pixel+palette bytes are
  decoded fresh each level load by `texInflateZlib`/`NonZlib`
  (`src/game/image.c`) into a runtime pool, with the palette appended after
  the pixel data (`tex->unk0a` records the boundary) — there is no
  ROM-serialized or offline-converted "model-TLUT source pointer" to be off
  by 4 bytes. **General rule this adds to the catalogue: a "same asset,
  different bytes on different levels" symptom for a texture/model resource
  does NOT imply an offline-converter/sidecar bug** — GE's model and image
  pipelines resolve almost everything through small numeric IDs into
  shared, runtime-decoded pools; the offline `tools_pc/d43_*.py` scripts
  only ever touch model *geometry* (nodes/GDL structure/vertex data), never
  texture or palette content. Before chasing a converter offset for a
  texture-content bug, first prove the content is even sidecar-owned (grep
  the relevant `d43_*.py`/`d88_*.py`/`d69_*.py` for the field name — if it
  isn't there, the bug is runtime: `src/game/image.c`/`tex.c` (decode) or
  `port/fast3d/gfx_pc.cpp` (RDP emulation)). Current leading hypothesis for
  the real mechanism (unconfirmed, needs a fast3d-side probe out of this
  session's scope): `gfx_dp_load_tlut`'s palette-source addressing
  (`port/fast3d/gfx_pc.cpp:2202`) uses `pitch = rdp.texture_to_load.width +
  1` from the preceding `gDPSetTextureImage`, which `texWriteLoadToTmemAddr`
  (`src/game/tex.c:503,519`) always calls with a literal `width=1` — a
  software-RDP approximation that may not correctly reproduce hardware
  LOADTLUT byte-addressing for this width=1 special case. See
  `docs/dev/findings.md` §D217 "M-88" for the full trace + empirical
  `GE_TEXDUMP` repro (BUNKER1 `pal[0]=0001` vs FACILITY `pal[0]=1a00`,
  exact match to the originally reported bytes).
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
- fast3d CPU-side "trivial reject" (`v1->clip_rej & v2->clip_rej &
  v3->clip_rej` in `gfx_sp_tri1`) trusts per-vertex outcodes (`x<-w`,
  `x>w`, etc.) that are only valid half-space tests when `w>0`. A vertex
  behind the camera (`w<0`) flips the comparison sense, so its outcode can
  be wrong; if that spurious bit happens to match the other two (correct)
  vertices', the AND-reduction drops a triangle GL's own clipper would
  have rendered correctly. Bites large near-camera polygons (room
  walls/ceilings close to a doorway) far more than small prop models —
  reads as "background geometry flickers away, props keep drawing" (D233).
  The backface-cull code right below already special-cases this same
  w-sign hazard for its cross-product sign; the trivial-reject block
  didn't. Fix pattern: skip the AND-test (don't trivially reject) whenever
  any vertex has `w<0` — safe, since it only ever adds triangles, never
  drops more. General lesson: any CPU-side "skip this triangle" heuristic
  in fast3d needs the same w<0-behind-camera exemption the existing
  backface culling already has, or it'll silently eat near-camera geometry
  exactly where a port is most likely to get scrutinized (doorways,
  tight corridors).
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
  neighbor-difference at every candidate row pitch — a correctly-pitched real
  image has a sharp minimum at its true pitch (≈0.2–0.7 on a 0–15 nibble
  scale), a pitch/shear bug has the minimum at a *different* pitch, and genuine
  noise data is flat (~3.7) at every pitch. That three-way split settles
  decode-vs-pitch-vs-source-data in one pass with no rebuild.

- **D219 — a runtime bitstream decoder that stores a "wide" (>1 byte/pixel)
  reconstructed value via a native machine write needs an explicit
  byte-order fixup on PC; a compile-time C-array asset does not. And the
  fixup is per *importer family*, not per texture: scope it by pixel
  format, never by texnum range or compmethod (M-113/M-114/M-115).**
  `src/game/image.c`'s texture decompressor (`texReadUncompressed`,
  `texChannelsToPixels`, `texBuildLookup`, `texInflateLookup`,
  `texInflateLookupFromBuffer`) builds each wide pixel as a shifted-OR
  integer and does a native store — correct on N64 (native == big-endian),
  byte-reversed on x86-64. This is the **opposite** of the usual "BE ROM
  data read as LE" direction in this section: here the PORT *host* writes
  native-endian and a downstream *shared, otherwise-correct* consumer has
  opinions about byte order. The trap M-113→M-114 hit: the fast3d importer
  family is NOT uniform. `import_texture_rgba16`/`_ia16` (G_IM_SIZ_16b:
  RGBA16, RGB15, IA16) read the pool as **manually big-endian bytes**
  (`(addr[0]<<8)|addr[1]`) → the decoder must store `bswap16(value)`;
  `import_texture_rgba32` (G_IM_SIZ_32b: RGBA32, RGB24) does `PD_BE32()`
  on a **native** u32 load → it wants host-order words and a store-time
  swap *inverts* them (that was the muzzle-flash-blue/ammo-pink
  regression). So the correct scope is the decoded `TEXFORMAT_*` — which
  every call site already switches on — not compmethod (the fire frames
  span LOOKUP/RLE/HUFFMANBLUR/RLELOOKUP) and not a hardcoded texnum range.
  Static `assets/*.c` C-array textures don't hit this because
  `gfx_tex_normalize_source` (D71) already normalizes them upstream of
  `import_texture_*`; only assets that are BOTH genuinely multi-byte-per-
  pixel AND decompressed at runtime through `texLoad`'s bitstream path are
  exposed. Offline ROM census (M-115): 94 such images — the 16-bit family
  is exactly `IMAGE_FIRE_0..14` + texnum 1198–1201/2430/2510–2523, and
  every ammo/flare/crosshair/muzzle-flash wide-pixel image is RGBA32.
  **Only the fire frames were eyeball-verified (M-115); texnum
  1198–1201/2430/2510–2523 were corrected by the importer contract, not
  seen on screen.** If a sprite in any level ever renders with wrong or
  inverted colors, check first whether its texnum is in that list (and
  decode it with `GE_D219RAW=1` against `scratch/d219_census.txt`).
  General rule for this bug class: before "fixing" a shared decoder's
  byte order, enumerate **every consumer** of the buffer and check which
  endianness each one assumes — the fix belongs at the store site, keyed
  on the format each consumer family expects.
  Final state (M-115, resolved): `PORT_PIXEL16` = bswap16 under `#ifdef PORT`
  (16-bit family only), `PORT_PIXEL32` = identity, wrapped around every
  wide-pixel store in the five functions above; CI/I4/I8/IA4/IA8 (single
  byte, no byte order) untouched. The intermediate history — M-113's blanket
  swap regressing muzzle flash/ammo HUD (M-114), reverted to identity, then
  M-115's format-scoped re-application — is in `docs/dev/findings.md` D219.

- **The tile's declared format is not the source data's format when GE's
  custom ucode transforms TMEM at load time (D229).** `texSelect`
  (`src/game/othermodemicrocode.c`) loads CI8 water images with
  `gDPSetTextureImage(CI,16b)` + `gDPLoadBlock` (the 16b is only the
  4KB-per-block load convention: lrs counts 16-bit units, so 700 × 2 = 1400
  bytes of raw 8-bit indices), and GE's RSP ucode expands indices→RGBA16 in
  TMEM at load — after which `sub_GAME_7F09343C` (`src/game/unk_092E50.c`)
  re-declares the slot as RGBA/16b and samples blue. fast3d has no such
  transform: it trusted the tile declaration, read index byte-pairs as
  16-bit texels (g = 2·idx mod 32 dominates → green mottle), and the water's
  pre-existing `sinf()` PRIM_LOD_FRAC cross-fade read as "pulsating". Fix
  (`port/fast3d/gfx_pc.cpp`, M-116): record the `G_IM_FMT` of each
  SetTexImage into `LoadedTexture.src_fmt` at load time; in `import_texture`,
  an RGBA/16b tile whose slot's last load came from a CI source routes
  through the CI8 palette import — the port equivalent of expand-at-load.
  Fires only on a genuine format disagreement, so real RGBA16 textures are
  untouched. **General rule:** when a GE draw declares a tile format that
  looks wrong for the data it loaded, suspect the ucode's load-time transform
  (ginit.s is missing from this repo — the expansion lives there), not UV,
  shade, or the combiner.

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

## D6. Non-void function with no `return` — latent until `-O2`

The decomp has a handful of functions declared to return a value (often
`Gfx *`) whose body ends without a `return` — the IDO/N64 build happened to
leave the intended value in `$v0` (usually the result of a tail call), and
the caller's `x = f(...)` kept working by luck. GCC on the PC target does
too **at `-Og`**, but at **`-O2`** the return register is genuinely
undefined and the caller reads garbage. When the value is a display-list
cursor, the caller then keeps writing the DL from a stale offset and
overwrites whatever the function just emitted — the emitted primitive
silently disappears.

- Instance: **D187** — `set_rgba_redirect_generate_microcode()`
  (`src/game/gunfire.c`), the sole emitter of the HUD ammo-type icon.
  Invisible under `-Og`; vanished the day release switched to `-O2`
  (`34885535`). Fix: `return` the tail call under `#ifdef AVOID_UB`
  (`#else` keeps the N64 body verbatim).
- Instance: **D77** — `sub_GAME_7F0C0BF0()` (`src/game/mp_music.c`), a
  one-line wrapper around `get_mTrack2Vol()` with no `return`. Every
  in-level music trigger (`set_missionstate()`'s MISSION_STATE_1/4 cases —
  i.e. the only path that starts a level's background track; the front-end
  menu/intro music bypasses this and calls `musicTrack1Play()` directly,
  which is why menu music worked while level music didn't) feeds this
  garbage/zero return straight into `musicTrack1ApplySeqpVol()` /
  `musicTrack3ApplySeqpVol()`. The result isn't a missing on-screen
  primitive but a **silent audio channel that still runs**: the compact-seq
  player keeps processing note-on events (visible in a `GE_AUDIOTRACE`
  `[MUSICNOTE]` capture with normal-looking key/velocity data) but every
  note's synthesized volume is scaled by the zeroed track volume, so nothing
  reaches the mixed output. A per-note trace alone looks completely healthy
  — the tell is a *track/channel-level* gain feed with a missing `return`
  upstream. This class isn't limited to display-list cursors; audit any
  `-O2`-only "quietly broken but not crashing" symptom for the same shape.
  Fix: `return` under `#ifdef AVOID_UB` (`#else` keeps the N64 body).
- The one flagged in-source with an explicit comment is
  `grep -rn "missing a \"return\"" src/` (`gunfire.c`). Others (like D77's)
  exist without the banner — suspect this class whenever an `-O2` build
  silently drops or mutes a small on-screen/audio element that an `-Og`
  build shows/plays correctly. One-line `#ifdef AVOID_UB` fix each.
- Not caught headless by a symbol/link check or a default-config framediff
  (the missing primitive is often small). Needs an `-O2` build + an eyeball
  or a targeted crop diff.

## D7. `va_list` is an ARRAY type on x86-64 SysV — never take `&` of a by-value `va_list` param

On Windows x64 `va_list` is a scalar (`char *`), so passing a `va_list`
parameter by value and later taking its address (`&args`) to hand a helper a
"pointer to the list" happens to work. On the **x86-64 System V ABI**
(Linux/macOS) `va_list` is `__va_list_tag[1]` — an array — so a by-value
`va_list` parameter has already decayed to `__va_list_tag *`. `&args` is then
the address of the *local pointer slot*, not the argument list, and a helper
doing `va_arg(*args, …)` walks garbage → segfault. Latent at `-Og`, fatal at
`-O2`.

- Instance: **D188** — the IDO printf engine `_Printf`
  (`src/libultrare/libc/xprintf.c`) passed `&args` to `_Putfld`; crashed at
  boot on Linux `-O2` via `bossInitMainthreadData` → `sprintf`. Fix: under
  `#ifdef PORT`, thread a real `va_list *` from the top (`sprintf.c` passes
  `&args` of an actual `va_list` object; `_Printf` takes `va_list *` and
  passes it straight through). `#else` keeps the ROM-matching body.
- Rule: to share one arg-walk between functions, pass `va_list *` explicitly
  from the outermost `va_start` scope. Never `&` a `va_list` that arrived as
  a parameter. `va_copy` into a fresh local is the other portable option when
  the callee must not disturb the caller's position.
- Same "works on MinGW, UB on SysV, exposed at `-O2`" shape as D6 — suspect
  this class for any Linux-only crash whose backtrace runs through
  `xprintf`/`_Putfld`/`vsnprintf`-alikes.

## D8. Latent fixed-size stack-buffer over/under-runs — fatal only under a stack protector

The decomp has a class of local scratch buffers that game code writes a few
slots past (or before) the declared bounds — e.g. a traversal stack whose
bail check is looser than its array size, or `&buf[i*3]` with `i` starting at
-1. The N64 build and the MinGW/Windows build have **no stack protector**, so
the overrun lands in adjacent stack scratch and is (in practice) harmless —
the Windows build is stable over thousands of frames. **Ubuntu's gcc default
is `-fstack-protector-strong`**, which places a canary right after the buffer
and turns every such overrun into a fatal `*** stack smashing detected ***`
`__stack_chk_fail`.

- Instances (D189): `stan.c` `sub_GAME_7F0B1DDC` `StandTile *tileStack[39]`
  (bail is `if (cat >= 41)`, checked once per outer loop → writes `[40..]`);
  `bondview2.c` `bondviewCalcIntroSwirlCamera` `f32 pointbuf[10]` written
  `[-3 .. 11]`.
- Fix policy: **`-fno-stack-protector` globally** (`CMakeLists.txt`) so Linux
  matches the N64 and MinGW builds — the code is byte-matched to the ROM and
  cannot be "fixed" per site without diverging. Where a case is cleanly
  bounded and the buffer is pure local scratch (never escapes, no ABI role),
  widening it under `#ifdef PORT` is also fine (`tileStack[64]`).
- A hardened Linux build (re-enabling the protector) would need the whole
  class swept first. Grep heuristic for new instances: any Linux-only
  `__stack_chk_fail` / `__fortify_fail` in the backtrace, in a `sub_GAME_*` or
  other byte-matched function, during stage load or the first few frames.
- Distinct from D7 (`va_list` UB) and D6 (missing `return`) but the same
  "works on N64 + MinGW, fatal on hardened Linux" shape.

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
  `tools_pc/framediff.py <ppmdir>` (structural: 16×12 grid mean-color +
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
- **A verification tool that shares the code's indexing convention proves
  self-consistency, not correctness** (D206 — cost eight sessions). The port
  played every SFX one bank slot too high. Real cause: `struct
  ALInstrumentAlt_s` (`src/snd.h`) is a cast alias over the on-disk
  `ALInstrument`, and its `soundArray` member sits at struct offset **12** on
  N64 (three 4-byte words) but **16** on PC (8-byte pointer + 8-byte
  alignment). On N64 that offset-12 array deliberately aliases the on-disk
  `bendRange`/`soundCount` words so `soundArray[N]` == on-disk entry `[N-1]`
  (GE's `SFX_ID`s are 1-based by design); on PC the alias is broken and every
  ID resolved one slot high. It survived M-68/M-70's "exact match" of 200+
  requests because `exact_match.py` compared each runtime voice against a ROM
  decode *of the requested index* with a decoder that walked the bank
  0-based; `diff_bank.py` likewise walked N64 and PC layouts with identical
  indexing. Both faithfully reproduced the bug and reported a pass. Two rules:
  (1) when validating a **mapping** (index → asset, id → record, offset →
  field), the reference must be produced **independently of the code's own
  accessor** — an external rip, a published table, a hand-decoded sample — or
  the test only confirms the code agrees with itself; (2) when a count and an
  enum disagree by one (261 bank entries vs 262 `SFX_ID` members), suspect a
  **pointer-width struct-layout shift** in a ROM-serialized struct (the D3x
  class) before anything subtler — check every cast-alias struct's member
  offsets at 32- vs 64-bit.
- **Count a probe's events by its canonical one-per-call line, never by a bare
  field match** (D204, then D205/M-73 which re-made the identical mistake on the
  identical sound index). `GE_AUDIOTRACE` emits *two* lines per `sndPlaySfx`
  (a `dumppos=... bank=...` line and a `sndPlaySfx: t=... soundIndex=...` line),
  so `grep -c soundIndex=109` reports exactly double the truth. D204 already
  corrected M-63's "idx 109 fired 94 times in 60 s" on these grounds; M-73 then
  built a whole root cause ("guards stuck re-triggering, 256x/29.7 s") on the
  doubled count and pinned it on converted collision geometry, which M-74
  withdrew. Two general rules: (1) grep the timestamped call line
  (`sndPlaySfx: t=`), not the field; (2) **before calling a rate anomalous,
  compare it to the rate the data says is legal** — the "barrage" was ~6x
  *under* the AK47's own `SoundTriggerRate` ceiling, which one lookup in
  `assets/obseg/gun/gunWeaponStats.inc.c` would have shown. Corollary: a
  perfectly monotone index sweep in a trace looks like a broken `random() % n`
  but is often a deliberate round-robin counter in ground-truth code (e.g.
  `male_guard_yelp_counter`, `chraction.c:2454`) — check the selector before
  reporting it.
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
- **D247 — a visual "defect" report can be faithful, unmodified N64 behaviour;
  check the decomp's own constants before assuming a port bug.** User reported
  black bars top/bottom of the screen. The obvious port-side suspects (a
  present/letterbox path, a framebuffer-vs-window size mismatch) were all
  clean — no code anywhere sets `G_ASPECT_MODE_EXT` for GE, so fast3d never
  letterboxes. The real cause: `bondviewGetCurrentPlayerViewportHeight()`
  (`bondview2.c:7858`) returns `VIEWPORT_HEIGHT_DEFAULT` = 220 of 240 NTSC
  scanlines (`fr.h:34`, ~92% fill) whenever `PLAYER_OPTION_SCREEN` is
  `SCREEN_SIZE_FULLSCREEN` — the decomp's own default value — unmodified,
  no `#ifdef PORT` in sight. `VIEWPORT_HEIGHT_FULLSCREEN` (304, no inset) is
  a trap name: it's only reachable via the unrelated `cameraBufferToggle`
  branch, not normal solo play. This is GoldenEye's real TV-safe-area
  letterboxing, present on original hardware. Ruled out a stale EEPROM
  `Screen=Wide/Cinema` setting from earlier sessions as an alternative
  explanation by reproducing identically on a fresh EEPROM. **Lesson:**
  before chasing a `port/` fix for a rendering "bug," grep the reported
  geometry (viewport height/width, screen offset, etc.) back to its decomp
  origin — a visually surprising but *exactly reproduced* N64 quirk isn't a
  port defect, and the fix is to close the finding, not to write code.
  §F **D247**.
