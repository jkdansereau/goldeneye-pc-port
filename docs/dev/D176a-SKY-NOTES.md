# D176(a) Sky — consolidated research notes (read-only)

Companion to `GRAPHICS-BACKLOG.md` D176(a) and findings.md M-37/M-47.
Purpose: make the next implementation session mechanical. No code changed by
this doc.

## State of play

- **Root cause (M-37):** `skyRenderTri`/`skyRenderFull` emit geometry only as
  `G_RDPHALF_*` immediates; `gfx_pc.cpp:2901-2903` deliberately no-ops them
  (same in the PD port) → solid black sky on every cloud-sky level. Clean
  headless repro: `-level_22` (Statue), frame ~360.
- **M-47 draft (Path B)** — `scratchpad/sky.patch`: `#ifdef PORT` hooks in
  both functions re-emit the sky as ordinary screen-space GBI via
  `skyPortRenderPoly` (ortho pixel projection + `gSPVertex`/triangles).
  Geometry reaches GL and textures (top-half luma 0.1 → ~174 on `-level_22`).
  Two open defects (below). Draft, not shippable.
- **Path A** (decode RDPHALF in fast3d) is the backlog's original suggestion;
  the wire format is now fully decoded (next section), so it is a viable
  fallback if Path B's units can't be pinned.

## RDPHALF wire format (decoded from `src/game/sky.c` + `include/PR/gbi.h`)

- Opcodes (`gbi.h:102-164,216-223`): `G_RDPHALF_1 = 0xe1`,
  `G_RDPHALF_2 = 0xf1` (final word of a stream), `G_RDPHALF_CONT`
  (= `G_IMMFIRST-13`). Tri ops: `G_TRI_FILL 0xc8`, `G_TRI_FILL_ZBUFF 0xc9`,
  `G_TRI_SHADE_TXTR 0xce`, `G_TRI_SHADE_TXTR_ZBUFF 0xcf`.
- **All payload values are S15.1 fixed point** — converter is
  `sub_GAME_7F094298` (`sky.c:218`): `clamp(x, ±32767.9) × 65536`, u32.
- **Packing:** each `(G_RDPHALF_1, G_RDPHALF_CONT)` pair carries four
  halfwords = **two S15.1 values**: `valueA = (w1.hi16, w2.lo16)`,
  `valueB = (w1.lo16, w2.hi16)`. Multi-value groups are streamed in two
  passes — all high halves first, then all low halves (see the explicit
  `(x & 0xffff0000) | (y & 0xffff0000) >> 16` packing at `sky.c:1854-1870`).
- **`skyRenderTri` header** (`sky.c:1725-1728`): first word =
  `(op << 24) | (winding/sign flag bit23) | lo16(first value)`; the stream
  then carries screen-space positions (`unk28×0.25` = pixel X, `unk2c` = Y×4
  family) plus per-vertex attributes. The RSP ucode reassembles and DMAs a
  real edge-walked RDP tri command (per the M-46 comment in sky.patch).
- **Vertex attribute vectors are 8 components** (`sky.c:1759-1776`):
  `(r, g, b, a, S·w′, T·w′, 32767·w′, z)` with perspective weight
  `w′ = unk34 × min(unk0c)/2` — i.e. color + perspective-corrected S/T +
  depth. `SkyRelated38` fields: `unk28 = screenX×4`, `unk2c = screenY×4`,
  `unk30 = z (0..0x7fff)`, `unk34 = 1/w`, `unk20/unk24 = S/T`.
- GE emits RDPHALF **nowhere except sky.c** — the command surface is closed
  and small.

## Defect 1 — texcoord shear (clouds smeared into streaks)

`skyPortRenderPoly` uses `tc = unk20 × 32.0f` — a magic number. GBI vertex
`tc` is **S8.8 texel** space; the real RSP path applies the sky image's tile
shifts to the streamed S/T. Fix spec:

1. Read the base-tile descriptor of the cloud image (`skywaterimages[0]`,
   GIMG offset `0xFB4` per M-36) from the extracted assets/sidecar — get W, H
   and shifts/shiftt.
2. Cross-check against how `gfx_pc.cpp` addresses texels for a known-good
   textured DL (the working reference).
3. Replace the magic: if streamed S/T are image fractions f ∈ [0,1), then
   `tc = f × (W << 8)` / `f × (H << 8)`. Verify on `-level_22`.

## Defect 2 — vertical coverage (band fills only top ~25%)

Leading hypothesis, from reading the N64 path: **the quad is not a plain
corner quad.** The N64 stream's four vertices are *perspective-interpolated*
points computed in the big interpolation block (`sky.c:1797-1813`, the
`sp210/sp290/sp2b0/sp230` vectors), not the raw TL/TR/BL/BR corners that
Path B's hook passes through. If those interpolated y-values differ from the
corner ys (they will, wherever w varies across the quad), the re-emitted
quad lands short.

Next steps (one focused session):
1. Per-call probe on `skyPortRenderPoly`: log nverts + each vertex's
   (x, y) + the tri op-header word; A/B against N64 reference footage of
   `-level_22`/`-level_36`.
2. If confirmed, either (a) replicate the interpolation block under PORT and
   emit the 4 computed vertices, or (b) drop to Path A with the decoder spec
   above (the ucode's own reassembly is then irrelevant — we decode the same
   values directly).

## Recommendation

Finish **Path B** first — geometry already works; both defects are unit/
vertex-set questions answerable in one render-iterate session. Keep Path A
as fallback; it is now fully specified by this doc if B stalls.
