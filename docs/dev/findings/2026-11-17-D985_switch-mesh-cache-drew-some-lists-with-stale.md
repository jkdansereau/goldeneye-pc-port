# D985

## D985: Switch mesh cache drew some lists with stale textures, palettes, vertices and fog (Dam: dark triangles, yellow discs while firing, fog-blue or black ground at the wall) -- FIXED + MAINTAINER-CHECKED 2026-11-17 (Ryujinx, Dam at the wall below the hangars, KF7)

**Symptom.** Dam, from the alarm area onwards and on the dam: a dark triangle, then yellow discs on the ground, while firing the KF7; a dark ground patch at the wall below the hangars. Never with `GE_MESHCACHE=0`; the discs and the triangle only with the GPU replay (`GE_MESHGPU=0` hid them).

**Method.** A temporary replay-vs-classic check (`GE_MESHVERIFY`, removed): every cache hit was replayed, then walked classically from the same RSP/RDP state and the same texture binding, comparing the emitted floats, the GPU state at each emission and the state left behind; `=2` did the same for GPU mesh draws (state per run). About 1.7 million CPU replays: no geometry or colour difference. Texture keys differing only by the CI palette hash were duplicates (`G_LOADTLUT` of 3 colours: the other bank entries come from the caller, same picture).

**Causes, all in `port/fast3d/gfx_meshcache.inc`.**
1. Inherited binding. A list whose first `G_TRI4` comes before its own texture load draws it with the caller's binding (weapon models, after a `G_TEXTURE` on the current tile, which does not ask for a re-import). The capture skipped the `in_bind` record once any `G_TEXTURE`/tile command had been seen, so the replay drew those triangles with the capture-time texture; a later untextured triangle also cleared the record. Now any triangle using a unit the list has not imported records it, plus the entry tile index (`MCW_FTILE`) when a `G_TEXTURE` came first.
2. Exit palette. `mc_apply_exit` restored all 256 palette entries of the capture; only the entries the list's `G_LOADTLUT`s write (`pal_mask`) are restored now, the hash recomputed as `gfx_dp_load_tlut` does.
3. Vertices after a GPU replay. The GPU replay skips `G_VTX` (the vertices are in the mesh) and left `rsp.loaded_vertices` as the previous list had them (the check reported a differing vertex buffer after every GPU replay). The capture marks the loads still live at the end of the list; the GPU replay runs those.
4. Near-plane meshes on the GPU. `GE_MESHGPU_FULL` (on since #33) let the GPU clip meshes that cross the camera plane, which interpolates shade and fog from the corner behind the eye (fog 255): the Dam ground at the wall went black (affine shade) or fog-blue (perspective shade, D986). It is off by default now: those meshes take the CPU near clip (D543/D553), as before #33.

**Ruled out.** The CPU replay's geometry, colours and combiner state; bullet impacts (the discs disappear when firing stops).

**Also affects.** Every level on the Switch; 1 and 2 were in 0.2.0 (step A, #11), 3 and 4 came with the GPU replay (#33, not released).
