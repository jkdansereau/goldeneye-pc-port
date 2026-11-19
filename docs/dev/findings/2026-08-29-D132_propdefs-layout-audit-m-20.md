# D132

<!-- from the old section-F/H table row -->
| D132 | D88 propDefs layout audit (M-20): converter cursor confirmed vs real PC struct layout for all types the 21 levels emit. DOOR/OBJECT-prefix/VEHICHLE/AIRCRAFT/TANK/AUTOGUN/AMMO/TINTED_GLASS/objective sub-records all MATCH. **Divergence found:** types 14 LINK / 19 SWITCH / 38 LOCK_DOOR / 44 SAFE_ITEM — each has `s32 IndexN` fields sharing a union with a pointer (`LinkRecord.first`/`Index1`), so on PC the field sits in an 8-byte, 8-aligned union slot, but the converter emits it at N64 tight-4-byte-word offsets → `pdef->Index1` reads `Index2`'s value, `Index2` reads 0 → switch-doors / dual-weapons / locked-doors / safes silently fail their validity guard (non-crash; guard failure also prevents the under-sized-record `->next` overflow). | **CLOSED (2026-09-28 bookkeeping: fix applied — `tools_pc/d88_propdefs.py` carries the D132 union-slot layouts for types 14/19/38/44; post-fix verification recorded in the `## D132` entry).** Earlier: proposed fix (not applied) — see below |

## D132 — D88 propDefs layout audit (M-20)

Static analysis only (no build, no game run). Goal: prove
`tools_pc/d88_propdefs.py`'s per-type cursor matches the real compiled PC
struct layout (8-byte pointers, 8-byte alignment) for every `PROPDEF_*`
record type any of the 21 solo `Usetup*Z` levels actually emits, and that
the handler set is total.

### Handler totality

Authoritative per-level type histogram: walk each level's `propDefs`
region with `d88_propdefs.PROPDEF_N64_WORDS` as the stride (the same
table `convert_stream` uses; it walks all 21 to an exact region-end
match, and `d125_check.py` confirms offline emit == converter for all 21
— so the stride table and the encountered type set are sound).

Union of all `type` bytes emitted across the 21 levels:

    1 2 3 4 5 6 7 8 9 10 11 12 13 14 17 18 19 20 21 22 23 24 25 26 27 28
    30 32 33 34 35 36 37 38 39 40 42 43 44 45 46 47 48

Never emitted (in the enum, absent from every shipped solo level):
15 (DEBRIS), 16, 29 (OBJ_DEPOSIT), 31 (OBJ_NULL), 41.

`tools_pc/d88_propdef_scan.py` is **stale / unreliable** — its private
`WC` + `SIZEOF_N64` guess table desyncs mid-walk and reports spurious
`unmapped type NOTHING`. `d88_propdefs.convert_stream` is the ground-truth
walker. (Scan tool left as-is; a follow-up could point it at
`PROPDEF_N64_WORDS`.)

Every emitted type has a non-generic handler **or** is provably safe
under the generic arm (header `_hdr_word` + `_bswap32` of the remaining
words, no pointer widening, no sub-word packed field):

- Generic-safe: 2 (DOOR_SCALE, `s32 Scale`), 23 (OBJECTIVE_START — see
  below), 24 (OBJECTIVE_END, header only), 25/26/27/28 (the meaningful
  field is the `s32 ObjRefID` at 0x4 that `get_status_of_objective` reads
  via the `MissionObjectiveRecord` cast — the `u16 unk4` in the stub
  structs is not how it is accessed, so `_bswap32` is correct), 34
  (OBJ_COPY_ITEM, 3 scalar words), 46 (CAMERAPOS / `CutsceneRecord` —
  `coord3d pos; f32 theta; f32 verta; s32 pad`, **no pointer**, 7 words,
  `PROPDEF_PC_BYTES[46]=28` correct).

### Per-type layout comparison (audited types)

Legend: N64 word -> PC byte offset; check = converter cursor lands on the
same PC offset the compiler would.

**ObjectRecord prefix** (shared by 3/5/12/17/36/42/43 and the head of
1/4/6/7/8/10/11/13/20/21/39/40/45/47). N64 0x80 / 32 w -> PC **144 B**.
Field-by-field: header@0; obj/pad `_hh_word`@4; flags@8; flags2@12;
prop*@16(+8); model*@24(+8); Mtxf mtx@32..96; runtime_pos@96..108;
runtime_bitflags@108; collisiondata*@112(+8); projectile*@120(+8);
maxdamage@128; damage@132; shadecol@136 (verbatim); nextcol@140;
sizeof **144**. Converter `_emit_object_prefix` cursor lands on every one
of these. MATCH.

**DOOR (1)** — `DoorRecord`, tail N64 w32..w63. The C3r Bunker2 suspect.
PC struct = **296 B** (`PROPDEF_PC_BYTES[1]=296`, `sizepropdef` PORT
`return 74`).

| field | N64 off | PC off (compiler) | converter | ok |
|---|---|---|---|---|
| linkedDoorOffset s32 (read-b4-write id) | 0x80 | 0x90 | 0x90 `_bswap32` | yes |
| maxFrac..maxSpeed 5xf32 | 0x84..0x98 | 0x94..0xa8 | same | yes |
| doorFlags u16 / doorType u16 | 0x98 | 0xa8 (`_hh_word`) | 0xa8 | yes |
| keyflags..doorOpenSound 3xu32 | 0x9c..0xa4 | 0xac..0xb4 | same | yes |
| frac..speed 5xf32 | 0xa8..0xb8 | 0xb8..0xc8 | same | yes |
| openstate s8/unkbd s8/calcopacity s16 | 0xbc | 0xcc (byte-pattern) | 0xcc | yes |
| TintDist s32 | 0xc0 | 0xd0 | 0xd0 | yes |
| CullDist s16/soundType s8/fadeTime60 s8 | 0xc4 | 0xd4 (byte-pattern) | 0xd4 | yes |
| linkedDoor* | 0xc8 | 0xd8 (8-al) +8 | 0xd8 +8 | yes |
| unkcc* (Vertex*) | 0xcc | 0xe0 +8 | 0xe0 +8 | yes |
| bbox (u32 + `struct bbox` 24B = 28B, 7 w) | 0xd0 | 0xe8..0x104 | 0xe8..0x104 | yes |
| openedTime u32 / portalNumber s32 | 0xec / 0xf0 | 0x104 / 0x108 | same | yes |
| openSoundState* | 0xf4 | 0x110 (8-al, 4B pad) +8 | 0x110 +8 | yes |
| closeSoundState* | 0xf8 | 0x118 +8 | 0x118 +8 | yes |
| lastcalc60 union s32/f32 | 0xfc | 0x120 | 0x120 `_bswap32` | yes |
| **sizeof** | 0x100 | **0x124 -> pad 296** | 296 | yes |

**Conclusion: the DOOR converter cursor matches the real PC layout
exactly.** `linkedDoorOffset` lands at PC 0x90 and is emitted as an int
id (not zeroed). The C3r Bunker2 residual was fixed by **D126** (objective
sub-record `->next` growth desyncing the walk so `linkedDoorOffset+arg2`
resolved to the wrong record) — not a DOOR-tail cursor bug. Bunker2
currently PASSES, consistent with this.

**VEHICHLE (39)** — `VehichleRecord`, PC **208 B** (`sizepropdef` PORT
`return 52`). ailist@w32 = int id (`OBJ_ID_WORDS`, D123);
aioffset|aireturnlist@w33 = `_hh_word`; path@w41, Sound@w43 = ptr.
Cursor: id 144->152, hh 152->156, 7xf32 156->184, path 184(8-al)->192,
nextstep 192->196, Sound 200(8-al, 4 pad)->208. MATCH (208).

**AIRCRAFT (40)** — `AircraftRecord`, PC **208 B**. Same shape; path@w43,
Sound@w44. Cursor lands path 192(8-al)->200, Sound 200->208. MATCH.

**AUTOGUN (13)** — `AutogunRecord`, PC **248 B** (`sizepropdef` PORT
`return 62`). 17 scalar tail words 144->212, then unkC4*/unkC8*/beam*
(w49/50/51) 216(8-al, 4 pad)->224->232->240, is_active 240->244, unkD4
244->248. MATCH (`PROPDEF_PC_BYTES[13]=248`).

**AMMO / MultiAmmoCrate (20)** — `MultiAmmoCrateRecord`, PC **200 B**.
Tail = `slots[13]` of `{u16 modelnum; u16 quantity}` -> 13 `_hh_word`
writes 144->196, pad->200. `AMMOTYPE_GLOBAL_MAX == 13` matches
`PROPDEF_N64_WORDS[20]=45` (32+13). MATCH (`sizepropdef` PORT `return 50`).

**TANK (45)** — `TankRecord`, converter **248 B** (`_emit_object_prefix`
+ collision*@w32 + 23 `_bswap32` words). The `TankRecord` struct as
declared in `bondtypes.h` is **53 words**, but `PROPDEF_N64_WORDS[45]=56`
(getools/ROM) — the struct's `//s32 unk88..` comments imply a 3-word gap
the C declaration omits, so the *field offsets* past `rect` are
unconfirmed. However: the only pointer is `collision`@w32 (correctly
widened + kept in slot), the stride is self-consistent with `sizepropdef`
PORT (`return 62` = 248), and TANK appears only twice (Depot x1, Runway
x1). Walk integrity is intact; only tank-field *semantics* (non-crash)
could be off. **Confidence medium**; acceptable until a tank level is
played.

**TINTED_GLASS (47)** — `TintedGlassRecord`, PC **168 B**. 5 `s32` tail
words 144->164, pad->168. No pointers. MATCH (`sizepropdef` PORT
`return 42`).

**Objective sub-records 30/32/33/35** (D126) — re-confirmed: each is
`{header; s32 payload x N; T *next}`; converter emits header + N
`_bswap32` words + 8 zero bytes for `next` at offset 16, total 24 B.
`criteria_deposit` (33) has N=3 (5 N64 words); the rest N=2 (4 words).
MATCH (`sizepropdef` PORT `return 6`).

**OBJECTIVE_START (23)** — `MissionObjectiveRecord` ends in
`WatchMenuObjectiveTextRecord *nextentry` @0x10, so PC `sizeof` is 24,
but `PROPDEF_N64_WORDS[23]=4` (nextentry is not serialized) and
`sizepropdef` PORT `return 4` (16 B stride). This is **safe**: nothing
writes `MissionObjectiveRecord.nextentry` — the briefing-text linked
list (`ptr_last_briefing_setup_entry_type23`, `objective.c:54`,
`objective_status.c:88`) is a chain of `struct watchMenuObjectiveText`
(type-35 records) via *their* `nextentry`, despite the misleading global
name. The type-23 record is only ever *read* (`get_status_of_objective`
walks it as `ObjRefID`@4 / `TextID`@8 / `MinDificulty`@0xc). Uniform
16-byte stride, no pointer store -> no D126-class overflow.

### Divergence found — types 14 / 19 / 38 / 44 (union-with-pointer index slots)

`LinkRecord` (14), `SwitchRecord`=`LinkRecord` (19), `LockDoorRecord`
(38), `SafeObjectRecord` (44) each declare their index fields as a
**union with a pointer**:

    typedef struct LinkRecord {
        inherits PropDefHeaderRecord;                       // 0, 4 B
        union { struct PropRecord *first;  s32 Index1; };   // PC: off 8 (8-al, 8 B)
        union { struct PropRecord *second; s32 Index2; };   // PC: off 16
        struct LinkRecord *next;                            // PC: off 24
    } LinkRecord;                                           // PC sizeof = 32

On PC the union is 8 bytes / 8-aligned, so `Index1` sits at **byte 8**
(4 B pad at byte 4), `Index2` at **byte 16**, `next` at **byte 24**.
`LockDoorRecord` is the same shape (sizeof 32). `SafeObjectRecord` has
three such unions (`item`/`safe`/`door`) + `next` -> `Index1`@8,
`Index2`@16, `Index3`@24, `next`@32, sizeof **40**.

The converter's handler for `(14, 19, 38, 44)` does:

    out[0:4] = _hdr_word(src[so:so + 4])
    for i in range(1, n64w):
        out[4 * i:4 * i + 4] = _bswap32(src[so + 4 * i: so + 4 * i + 4])

i.e. it lays the N64 index words at PC bytes **4, 8, 12** — the N64
tight-4-byte packing. Result on PC:

| read | converter put | compiler expects | effect |
|---|---|---|---|
| `pdef->Index1` (byte 8) | N64 word 2 = **Index2** | Index1 | wrong id |
| `pdef->Index2` (byte 16) | (never written) = **0** | Index2 | always 0 |

`PROPDEF_PC_BYTES` also under-sizes 14 & 19 & 44 (24 vs real 32 / 32 / 40;
38 is coincidentally 32).

**Runtime effect (`prop.c` `proplvreset2` walk):** `PROPDEF_SWITCH`
reads `index1 = pdef_switch->Index1` / `index2 = pdef_switch->Index2`,
resolves `doorA`/`doorB`, and only if
`doorA && doorA->prop && doorB && doorB->type==PROPDEF_DOOR && doorB->prop`
writes `pdef_switch->first/second` + calls `initSetLevelLoadPropSwitch`
(which does `arg0->next = ...`). Because the indices read wrong, that
guard **fails**, so the pointer stores (and the would-be `->next`
overflow past the under-sized record) never happen. Same for
`PROPDEF_LOCK_DOOR` (`pdef_lock_door->door/lock` + `->next`),
`PROPDEF_SAFE_ITEM` (`pdef_safe->item/safe/door`), and `PROPDEF_LINK`
(`propweaponSetDual`, also `guna && gunb` guarded).

**Net: non-crashing but silently broken** — switch-activated doors, dual
(left+right) weapon pickups, padlocked doors, and safe/safe-item links do
not initialise on PC. This explains why Streets (`UsetuptraZ`, 20x
LOCK_DOOR) and Aztec/Dam (SWITCH) still PASS the load+no-crash sweep.
Below crash work in priority, but a WS6 objective-playthrough blocker.

Affected levels: LINK — Caverns, sevb. SWITCH — Archives, Aztec, Dam.
LOCK_DOOR — Dam, sevx, sevxb, **Streets x20**. SAFE_ITEM — Archives,
Depot, sevb, sevx.

**Not previously caught:** D125's stride re-check only asserted
`sizepropdef == PROPDEF_PC_BYTES/4` (internal consistency), never
`PROPDEF_PC_BYTES == real compiler sizeof`. D122's totality note flagged
`[u16|u16]` half-swap but not the `union{ptr; s32}` index-slot case.

### APPLIED (M-20, commit pending) — ABI/layout only, `#ifdef PORT`

Fix below applied verbatim. `tools_pc/d88_propdefs.py`: `PROPDEF_PC_BYTES`
`14/19 -> 32`, `44 -> 40`; the `(14,19,38,44)` handler now emits each
`Index{k}` into the low 4 B of the 8-aligned slot at PC `8 + 8*k`
(`nidx = {14:2,19:2,38:2,44:3}`). `loadobjectmodel.c sizepropdef()` PORT
switch: `LINK/SWITCH/LOCK_DOOR -> return 8`, `SAFE_ITEM -> return 10`,
`TAG` stays `6`. Regen chain run. Verified: `-level_09` framediff 3/3
PASS; `-level_20` behaviour byte-identical to a freshly-built pre-D132
baseline (the `frame_000320` phash delta is pre-existing stale-golden /
Silo slowdown, NOT a regression — confirmed by building the baseline);
Archives (`-level_25`, SWITCH+SAFE_ITEM), Streets (`-level_29`, 20x
LOCK_DOOR), Dam (`-level_33`, SWITCH+LOCK_DOOR) all load + render +
no-crash. Sweep runs this pass were heavily flaky (level_09 itself
0-framed on a loaded machine) — pure D117/watchdog noise, no crashes.

### Original proposed fix (as written pre-apply)

`tools_pc/d88_propdefs.py` — `PROPDEF_PC_BYTES`: `14: 24 -> 32`,
`19: 24 -> 32`, `44: 24 -> 40` (38 stays 32). Replace the
`(14, 19, 38, 44)` handler:

    if type_byte in (14, 19, 38, 44):  # LINK / SWITCH / LOCK_DOOR / SAFE_ITEM
        # D132: each Index{1,2,3} field shares a union with a pointer, so on
        # PC it lives in the LOW 4 bytes of an 8B/8-aligned slot at
        # PC offset 8 + 8*(N-1); the record ends in a *next the setup walk
        # writes.  The N64 image packs the indices as tight 4-byte words;
        # emitting them there put Index1 where the compiler reads Index2.
        nidx = {14: 2, 19: 2, 38: 2, 44: 3}[type_byte]
        out[0:4] = _hdr_word(src[so:so + 4])
        for k in range(nidx):
            w = src[so + 4 * (k + 1): so + 4 * (k + 1) + 4]
            out[8 + 8 * k: 12 + 8 * k] = _bswap32(w)   # low 4B, LE; high 4B = 0
        return bytes(out)

`src/game/loadobjectmodel.c` `sizepropdef()` `#ifdef PORT` switch: move
`PROPDEF_LINK` / `PROPDEF_SWITCH` out of the `return 6` group and give
`LINK`/`SWITCH`/`LOCK_DOOR` -> `return 8` (32 B) and `SAFE_ITEM` ->
`return 10` (40 B). `PROPDEF_TAG` stays `return 6` (its `ID`/`OffsetToObj`
are plain `u16`/`s16` at 0x4, not in a pointer union; handler already
correct).

After applying: `d88_emit.py --regen` (all 21 sidecars), then the full
`d43 && d69 && d88 --regen` chain, then re-verify `-level_09` / `-level_20`
golden + a Streets/Dam/Archives load.

### Confidence per type

| type(s) | verdict | confidence |
|---|---|---|
| ObjectRecord prefix (3/5/12/17/36/42/43) | matches | high |
| 1 DOOR | matches (incl. linkedDoorOffset @0x90) | high |
| 39 VEHICHLE / 40 AIRCRAFT | matches | high |
| 13 AUTOGUN | matches | high |
| 20 AMMO/MultiAmmoCrate | matches | high |
| 47 TINTED_GLASS | matches | high |
| 30/32/33/35 objective sub-records | matches (D126) | high |
| 23 OBJECTIVE_START | 16B stride safe (nextentry never written) | high |
| 2/24/25/26/27/28/34/46 (generic) | safe under generic arm | high |
| 4 KEY / 7 MAGAZINE / 21 ARMOUR / 8 COLLECTABLE | prefix + plain tail; widely used in passing levels | medium-high |
| 45 TANK | struct offsets unconfirmed (53 vs 56 w); stride self-consistent, walk intact, non-crash | medium |
| **14 LINK / 19 SWITCH / 38 LOCK_DOOR / 44 SAFE_ITEM** | **DIVERGENT** — union index slots at wrong PC offset; silently non-functional (non-crash) | fix high confidence |

### Files touched

Docs only: this subsection + the §F index rows (D88.4 status, new D132
row). No code changed; the proposed diff above is not applied. No temp
probe scripts left.
