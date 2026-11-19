# D995

## D995: Vulkan backend for the PC build, step 5 (issue #44) -- the mesh path (GE_MESHGPU) -- BUILT + RUN 2026-11-18 (Windows cross-build, RTX 5070: ~100k mesh draws per run on two levels, validation 0 errors)

**Change (port render layer only).** `vk_mesh_create` / `_delete` / `_draw` / `_supported` (`gfx_vulkan.cpp`), as `gfx_deko3d.cpp`:
- **Buffers:** one host-visible vertex buffer per mesh (`VkMesh`), freed after the serial that last used it (`s_dead_meshes`). Host memory for now: a device-local copy is a step 6 measurement (the ring and image heap are the same split).
- **Draw:** the program's `mesh_vert` (the pack's GPU-transform twin of the combiner) in a pipeline of its own (`PipeKey.mesh`); the `DkMeshUniforms` block (fog, LOD and env masks, up to 32 matrices) in a fresh ring allocation at binding 6 (vertex stage) pushed with the other descriptors; vertices bound at the run's byte offset. `zcut_pass` (D987) reuses the mesh pipeline. Culling is dynamic state set around the draw (back / front, front face CW when `invert_y`, GL's rule as in deko3d), restored to none afterwards.
- **Support:** `vk_mesh_supported` is false without `VK_EXT_depth_clip_control` (the twin writes GL's clip z), for the uber-shader fallback and for a pack entry without a twin; the cache then keeps the classic path. On PC the mesh cache is opt-in (`GE_MESHCACHE=1`); the Switch has it on.
- The exit log reports the mesh draw count.

**Verified.** Windows cross-build, `GE_RENDERER=vk GE_MESHCACHE=1` with the MSYS2 validation layer (D988): level 09 114473 mesh draws, Surface (`-level_36`) 90926, 0 validation errors, clean exit, pictures plausible (mesh cache hit rate 66-89 %). **Front-face rule:** GL's rule (`invert_y ? CW : CCW`) is used. Derived from the Vulkan area formula with the negative-height viewport (a counter-clockwise triangle in NDC, y up, has positive window area, so it is front-facing under COUNTER_CLOCKWISE) and from Fast3D's CPU test (`cross < 0` is front). A temporary flip probe (since removed) on level 09 showed the viewmodel gun dull and without specular highlights when inverted, as back faces look. No pixel comparison with the classic path was possible (camera and cutscene timing differ between runs, no readback before step 6); on level 36 neither the flipped rule nor no culling brought back the near terrain the classic capture shows, which GL does not show either at that moment, so that difference is timing.

**Not done / owed.** A pixel comparison against the classic path (the readback below exists now, but camera timing differs between runs); mesh-path timings against GL (step 6, with a device-local vertex copy); `GE_MESHGPU_FULL`; AMD / Intel, Linux.

**Next (issue #44 step 6).** On-disk pipeline cache and measurements against GL.

**Confidence.** High on NVIDIA / Windows for the draw and the culling rule; the performance side is unmeasured.
