# D981

## D981: Boris (title intro) had a triangle stretched from his head toward the origin with the GPU mesh path (D978) -- mesh uniforms reordered so a draw's pushes stay in the first 512 words -- FIXED + MAINTAINER-CHECKED 2026-11-16 (Ryujinx, title intro; Switch build)

**Symptom.** Title intro, Boris: a head triangle stretched toward the shoulder and the screen origin, only with `GE_MESHGPU` on (gone with `GE_MESHGPU=0`, still there with `GE_MESHGPU_FULL=0`, gone when meshes with more than one matrix stayed on the CPU).

**Ruled out by probes (removed).** Bake: every baked vertex's slot matrix gives the CPU's clip position (x, y, z, w). Draw: re-running the CPU vertex transform during the GPU walk matched the GPU slot matrices for every vertex over ~9600 intro frames (meshes up to 7 matrices). Every vertex's slot index, read back with the drawn program's stride, is in range. So the matrices were right on the CPU side and wrong only as read by the shader.

**Cause.** `DkMeshUniforms` had the 32 matrices first (2048 bytes) and the fog/LOD/env tail at byte 2048. Each draw pushed the tail at offset 2048, past the 512 words of Ryujinx's inline constant-buffer cache (the same limit that made one 2208-byte push crash). The write landed on the wrong place and the shader read a bad matrix 0, the slot of vertices loaded before the list's first `G_MTX`, which Boris's head has.

**Fix.** Tail first, matrices last (`gfx_dk_layout.h`, the `Mesh` block in `dkshgen.cpp`); `dk_mesh_draw` pushes the tail at 0 and then only the `nmtx` matrices at `offsetof(mtx)`, all inside the first 512 words for up to 25 matrices. Not checked on the console (the console path may never have shown it).

**Verification.** Maintainer, Ryujinx, title intro: Boris draws correctly with the GPU mesh path on.
