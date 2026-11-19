# D991

## D991: Vulkan backend for the PC build, step 4 (issue #44) -- post passes: FXAA, GE+ sky, D578 present slots -- BUILT + RUN 2026-11-18 (Windows cross-build, RTX 5070: Surface with the GE+ sky, forced 240 Hz interpolation and FXAA (GL and Vulkan) drawn upright, validation 0 errors)

**Change (port render layer only, no game code).**
- **Pack** (`cmake/VulkanShaders.cmake`): the three named entries of the deko3d pack (`DK_PACK_FULLSCREEN` / `FXAA` / `SKYPLUS`) from the same GLSL (`port/switch/shaders/`). glslang wants `gl_VertexIndex` where uam takes `gl_VertexID`: the script compiles a copy of `fullscreen.vert.glsl` with that one substitution; the Switch file is untouched.
- **Fullscreen pipelines** (`gfx_vulkan.cpp`, `get_fs_pipeline`): the pack's fullscreen triangle with no vertex input, blending off, every channel written, keyed on fragment module x target formats x samples; same layout and dynamic state as the game's pipelines. `draw_fullscreen` binds the sampled image (binding 0) and a ring-allocated uniform block (binding 4), sets GL's viewport / scissor, and marks GL's dynamic state dirty for the next draw.
- **FXAA** (`gfx_vulkan_fxaa_pass`): the source framebuffer's single-sample colour (alpha-ONE view, clamp-to-edge as GL leaves it) drawn into the window at its own size.
- **GE+ sky** (`gfx_vulkan_skyplus_stage` / `_pass`): the 64-slot staging ring and `DkSkyUniforms` packing of deko3d; the intensity texture is RGBA8 with a CPU mip chain, re-uploaded when `texGen` changes; drawn over the pass's viewport with depth off. `gfx_skyplus_failed` is no longer forced to 1 (it is set only if the pack lacks the shaders).
- **D578 present slots** (`gfx_vulkan_interp_store` / `_show` / `_sync`): per-slot window-format images sized on first store (D583), filled by `vkCmdCopyImage` from the acquired swapchain image and copied back into it at show; `sync` is `vkQueueWaitIdle` + retire. Needs a transfer-source swapchain (`s_sc_readable`); without it the slots are skipped.

- **FXAA on PC (maintainer request):** `Video.FXAA` was read on every platform but acted on only on the Switch (project-dam, #2). The `__SWITCH__` guards are gone: the three call sites and the window-sized resolve target in `gfx_pc.cpp`, `gfx_fxaa_on` / `GFX_FXAA`, the `gfx_opengl_fxaa_pass` definition, the nullptr entry of the pass table (GL crashed through it, Rip=0), and `video.c` (init and the live `VCFG_FXAA` update). The ini default stays 0 off the Switch (Switch preset: 1). There is no PC menu entry for it yet.

**Verified.** Windows cross-build, `GE_RENDERER=vk` on an RTX 5070 with the MSYS2 validation layer (D988), Surface (`-level_36`) with `Visuals = 2`: the GE+ sky (clouds, sun glow) is drawn behind the level, GL's capture of the same level is the reference for the geometry; with `GE_INTERP_FORCE=1` (the panel reports 59 Hz) "frame interpolation on: 240 fps presents" and a correct image; FXAA (`Video.FXAA = 1`) with MSAA 8 (resolved first) and MSAA 1, on GL and on Vulkan: upright, anti-aliased, same picture on both. 0 validation errors in every run; three more Vulkan MSAA 1 + FXAA runs clean (one run earlier in the session hung before the GL table fix and did not recur). Windows, Linux and Switch build.

**Not exercised.** The interpolation's softened (alpha < 1) passes, which this run's decisions never reached ("D583 decisions" all zero); present slots under a resize; the sky inside an off-screen framebuffer.

**Next (issue #44 step 5).** The mesh path (`GE_MESHGPU`: `mesh_create` / `mesh_draw` / `mesh_supported`, uniform block 6, the clip-control extension is then required).

**Confidence.** High for the sky and the slots on Windows / NVIDIA; medium for FXAA (visual check on GL and Vulkan, no pixel diff; the Switch path was only compiled). Linux runtime, AMD / Intel and MoltenVK were not run.
