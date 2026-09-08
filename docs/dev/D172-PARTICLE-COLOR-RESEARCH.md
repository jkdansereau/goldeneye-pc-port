# D172 — particle/flares wrong color (magenta/cyan blood): static research + root-cause candidate

Symptom (user-reported, GRAPHICS-BACKLOG line 31): bullet impacts / flares / blood spatter
render magenta/cyan instead of dark red. Static audit done; one strong root-cause candidate
with a cheap in-game verification. **No code changed.**

## 1. How particles get their color state (mechanism)

Particle triangles (`explosion.c`, `gSPVertex` + `G_TRI_FILL`/`G_TRI_SHADE_TXTR`) carry
**no per-primitive material command**. Their CC/tile state comes from replaying ROM-resident
state records via `gSPDisplayList(gdl++, g_ExplosionDisplayLists[var_s2])`
(`explosion.c:894`, 15-record loop, one record + its particles per iteration).

- Records: `globalDL_0x078 … globalDL_0x9a8`, a 17-entry table at image-bank offset
  0x28 (`g_pc_gimg_off_globalDL_*`), bank base cart 0x1029D160 (see `image_bank.c:322`).
- Each record is also scanned at load by `texLoadFromDisplayList` (`src/game/image.c:475`)
  for its `G_SETTIMG` + ABCD marker word — that part works (textures upload fine).

## 2. Record layout (decoded from ROM)

```
+0x00  E7 C3 FF FE        G_RDPPIPESYNC
+0x08  B9 00 03 1D        ???  <-- UNHANDLED by fast3d (see §4)
+0x10  <varies>           second word of the ??? command
+0x18  FC ...             G_SETCOMBINE w0   (rgb c0 | alpha c0 <<16)
+0x20  FF ...             G_SETCOMBINE w1   (rgb c1 | alpha c1 <<16)
+0x28  FD ...             G_SETTIMG w0      (timg pointer)
+0x30  .. 50 50 50 50     G_SETTIMG w1      (ABCD marker)
```

Dominant particle combine mode (14 of 17 records), decoded with the standard libultra
bit layout:

- cycle 0 RGB: `TEXEL0, 0, TEXEL1, COMB_A`
- cycle 0 ALP: `TEXEL0A, COMB_A, SHADE, COMB_A`
- cycle 1 RGB: `COMBINED, 0, SHADE, COMB_A`   ← **2-cycle** modulate
- cycle 1 ALP: same as cycle 0

## 3. fast3d audit (what is right)

- `G_SETCOMBINE` bit extraction (`gfx_pc.cpp:2786`) matches the standard libultra layout
  exactly (verified field-by-field against the ROM words). Not the bug.
- Store/decode of `rdp.combine_mode` (`gfx_dp_set_combine_mode` vs `gfx_generate_cc`
  key shifts) are consistent. Not the bug.
- The generated GLSL combiner implements the RDP formula with the usual special cases;
  `SHADER_COMBINED` correctly resolves to the previous cycle's running value.

## 4. Root-cause candidate: the 0xB9 word aborts the sub-DL (D146 path)

fast3d's SP walker has **no case for opcode 0xB9** (full case list audited: MTX/MOVEMEM/
VTX/DL/COL/syncs/all DP immediates/EXT commands — nothing in the 0x80–0xBF group). Its
`default:` (`gfx_pc.cpp:2920`, the D146 guard) **ends the whole (sub-)display list** on an
unknown opcode.

Consequence for every particle record replay:

```
RDPPIPESYNC  -> ok (no-op)
0xB9         -> D146 default -> RETURN (sub-DL dead)
G_SETCOMBINE -> never applied
G_SETTIMG    -> never applied
<particles>  -> drawn with STALE combine mode + stale tile/timg state
```

Stale CC/tile state = arbitrary colors = the magenta/cyan symptom. On N64, gmain.s
obviously handles 0xB9 (the game works there), so this is a fast3d gap, not a ROM quirk.

Secondary (only matters if 0xB9 turns out to be a no-op): particle 2-cycle rendering
requires `G_CYC_2CYCLE` in other_mode_h (`gfx_pc.cpp:1523`); the records never set
other-modes, so particles inherit ambient state — same on N64, but worth confirming if
the primary fix doesn't resolve it.

## 5. Verification (cheap, in-game)

The D146 guard logs `D146: unknown GBI opcode 0x%02x ...` rate-limited to **20** messages.
Run any level, shoot things / trigger flares, watch console output at startup for
`unknown GBI opcode 0xb9`. If present → root cause confirmed. (The budget fills in the
first frame or two of particles, so check early output.)

## 6. Fix path (next session)

1. **Decode 0xB9's semantics.** `ginit.s` (dispatch-table init) is *not in this repo*
   (`gmain.s:10` includes it; file absent). Options: (a) locate ginit.s in the original
   source / another checkout; (b) trace gmain.s's 0x80–0xBF group handler by hand;
   (c) infer from data — w0 is constant `B900031D` across all records, w1 varies
   (`0C184B50`, `00504B50`, …); if it's an other-modes/cycle-type setter, w1 bits should
   match G_MDSFT_* fields.
2. Implement the handler in fast3d (port-only). Re-run: blood/flares should go dark red;
   D146 spam for 0xb9 stops.
3. If colors still wrong after that: log `use_2cyc` + combine key on particle triangles
   (env-gated) and check ambient G_CYC_2CYCLE (§4 secondary).

## Files

- `src/game/explosion.c` — record table (165), replay loop (894)
- `src/game/image_bank.c:322`, `src/game/image.c:475` — record offsets / load-time scan
- `port/fast3d/gfx_pc.cpp` — SP walker cases (~2600–2930), D146 default (2920),
  SETCOMBINE decode (2786), combine store (2270), cc generation (312+), 2CYCLE flag (1523)
- `rsp/graphics/gmain.s` — N64 ucode ground truth for 0xB9 (dispatch table at dmem 0xbc;
  ginit.s missing from repo)
