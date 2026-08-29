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

## B. 16-byte PC `Gfx` / `Vtx` vs 8-byte N64

Any buffer reservation, `memcpy` size, slot stride, or pool budget
expressed in N64 `Gfx`/`Vtx` units is **half-size** on PC.

- Instances: D50.6 (texCopyGdls copied only w0 of each 16-byte slot),
  D58 (DL reserve 0x100→0x200), D85 (`bgWidenRoomGdl` 8→16 + bswap),
  D95 (2× master-DL buffer + raised mempool ceiling).

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
