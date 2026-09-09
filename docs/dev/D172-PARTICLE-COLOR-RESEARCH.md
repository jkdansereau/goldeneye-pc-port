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

## 4. The 0xB9 root-cause candidate is WITHDRAWN (M-82, 2026-09-08)

The earlier audit claimed fast3d has "no case for opcode 0xB9" and that the D146
`default:` guard therefore aborts every particle sub-DL. **That is wrong.**

This build does **not** define `F3DEX_GBI_2` (verified: no `-DF3DEX_GBI_2` in
`CMakeLists.txt`, no `#define` in `include/PR/gbi.h` or anywhere in tree). In the
non-F3DEX2 branch of `gbi.h`, `G_SETOTHERMODE_L = G_IMMFIRST-6 = -71`, and
`(uint8_t)(-71) == 0xB9`. So `gfx_pc.cpp:2731` `case (uint8_t)G_SETOTHERMODE_L:`
**is** `case 0xB9:`.

Decode of the record word `w0 = 0xB900031D`, `w1 = <varies>`:
`gfx_sp_set_other_mode(C0(8,8)=3, C0(0,8)=29, w1)` → sets `other_mode_l` bits
3..31. `sft=3 == G_MDSFT_RENDERMODE`, `len=29` — this is exactly
`gsDPSetRenderMode(w1)`. `w1` values (`0x0C184B50`, `0x00504B50`, …) are
render-mode / blender words. fast3d applies it correctly.

So the particle sub-DL runs to completion on PC: `E7` RDPPIPESYNC (handled),
`B9` SetRenderMode (handled), `FC` SETCOMBINE (handled), `FD` SETTIMG (handled).
**No D146 abort. No stale-state fallthrough.** The wrong-colour bug is elsewhere.

## 5. Where the bug actually is — re-scoped (M-82)

The dominant combine (14/17 records, §2) is a **2-cycle** mode
(`cycle 1 RGB: (COMBINED - 0)·SHADE + COMB_A`). But the records set render mode
(`0xB9`) and **never set the cycle-type** field (`G_MDSFT_CYCLETYPE`, in
`other_mode_h`). On N64 the ambient `other_mode_h` at particle-draw time is
2-cycle (set by whatever DL ran before); fast3d's ambient `other_mode_h` at that
point is very likely **1-cycle**, so `use_2cyc` (`gfx_pc.cpp:1521`) is false and
fast3d renders only cycle-0 of the combine:
`(TEXEL0 - 0)·TEXEL1 + COMBINED_A`. With no second tile loaded, **TEXEL1 is
garbage** → arbitrary colour → the magenta/cyan symptom. This is the "secondary"
note from the old §4, now promoted to the primary hypothesis.

### Verification

1. **DONE (M-82).** `-level_09` (bunker1), `GE_INPUTSCRIPT` sustained-fire,
   ~3600 frames, `d172_probe.log`: **zero `D146: unknown GBI opcode` lines,
   zero ERROR/WARN of any kind.** The particle sub-DL does not abort on 0xB9.
   §4 root cause is dead. (Headless, so this does not by itself confirm the
   §5 2-cycle hypothesis — that still needs the probe below.)
2. Add an env-gated probe at `gfx_sp_tri*` when `rdp.combine_mode` matches a
   particle record's key: log `use_2cyc`, `other_mode_h & CYCLETYPE`, the CC
   key. Expect `use_2cyc == 0`.

## 6. Fix path (next session)

1. Run the §5 verification. If `use_2cyc == 0` on particle tris is confirmed:
2. **Port-only fix, `src/game/explosion.c` under `#ifdef PORT`** (same
   hardware-idiom exception class as the existing `G_TRI4` / dynamic-light
   `#ifdef PORT`s): emit an explicit `gsDPSetCycleType(G_CYC_2CYCLE)` (or the
   raw `G_SETOTHERMODE_H` immediate) into / ahead of the particle record replay
   so PC does not depend on ambient cycle-type. `#else` = N64 stream verbatim.
   Alternative (pure port): in `gfx_pc.cpp`, when a SETCOMBINE carries a
   2-cycle-only combine (cycle-1 slots non-trivial) force `use_2cyc` — riskier,
   broader blast radius.
3. If colour still wrong after 2-cycle is forced: check `texSelect` actually
   binds TEXEL1's tile for these records, and re-decode the combine (the §2
   decode was not re-verified this pass).

## Files

- `src/game/explosion.c` — record table (165), replay loop (894)
- `src/game/image_bank.c:322`, `src/game/image.c:475` — record offsets / load-time scan
- `port/fast3d/gfx_pc.cpp` — SP walker cases (~2600–2930), D146 default (2920),
  SETCOMBINE decode (2786), combine store (2270), cc generation (312+), 2CYCLE flag (1523)
- `rsp/graphics/gmain.s` — N64 ucode ground truth for 0xB9 (dispatch table at dmem 0xbc;
  ginit.s missing from repo)

Note (M-82): 0xB9 is `G_SETOTHERMODE_L` in this non-F3DEX2 build and is already
handled by fast3d — `gmain.s` / `ginit.s` are NOT needed to resolve D172.
