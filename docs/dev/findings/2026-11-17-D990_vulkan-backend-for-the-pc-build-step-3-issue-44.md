# D990

## D990: Vulkan backend for the PC build, step 3 (issue #44) -- off-screen framebuffers: MSAA, resolve, copies, framebuffer textures, depth -- BUILT + RUN 2026-11-17 (Windows cross-build, RTX 5070: MSAA 4x and 1x drawn like GL, validation 0 errors; maintainer play-tested)

**Change (port render layer only, no game code), `gfx_vulkan.cpp`.**
- **Framebuffer images** (as deko3d): each off-screen framebuffer owns a single-sample RGBA8 colour image (sampled, copied and resolved into; its sampled view swizzles alpha to ONE, as GL's RGB attachment), a multisampled colour image when MSAA is on, and a depth image at the rendering's sample count when the framebuffer has depth. The sample count is clamped to `framebufferColorSampleCounts & framebufferDepthSampleCounts`. Images are rebuilt when size, MSAA level or depth change, and their destruction waits for the submission serial like any other image.
- **Layouts:** every image's layout is tracked (`layout_to`); a change ends the current rendering pass and records one conservative ALL_COMMANDS barrier (none between two read-only layouts). Before a draw, any framebuffer it samples is moved to SHADER_READ_ONLY; a framebuffer sampled while it is the current target is replaced by the dummy texture (GL feedback is undefined).
- **Orientation:** targets stay top-down (D989); a framebuffer sampled as a texture sets the uber-shader's `flip_v` for that unit (t' = 1 - t). Copies are blits with GL's bottom-up rectangles mirrored on both sides, so the relative orientation is GL's.
- **Copies and resolves:** `resolve_msaa_color_buffer` resolves directly (`vkCmdResolveImage`) when source and target are same-size off-screen framebuffers, else resolves the source and blits (into the window, or across sizes); `copy_framebuffer` blits the source's resolved image. Both run between rendering passes.
- `gfx_framebuffers_enabled` is back on (step 2 had it off). Park destroys every framebuffer's images (the first run's 10 leaked objects at exit).

**Verified.** Windows cross-build, `GE_RENDERER=vk -level_09` on an RTX 5070 with the MSYS2 validation layer (D988): MSAA 4x renders into the multisampled framebuffer and resolves into the window with smoothed edges, picture as GL's; MSAA 1x likewise; 0 validation errors, clean exit, 35 pipelines. The maintainer played the build. Linux and Switch build. Not exercised: GE never emits `G_COPYFB_EXT` or `G_SETTIMG_FB_EXT`, so `copy_framebuffer` and framebuffer textures (`select_texture_fb`) are compiled but have not run.

**Next (issue #44 step 4).** Post passes: FXAA, the GE+ sky pass (`gfx_skyplus_failed` is still set under Vulkan), the interpolation slots.

**Confidence.** High for the MSAA / resolve path on Windows / NVIDIA; medium for the copy and framebuffer-texture paths (unrun). Linux runtime, AMD / Intel and MoltenVK were not run.
