# D996

## D996: Vulkan backend for the PC build, steps 6 and 8 (issue #44) -- on-disk pipeline cache, CPU timings against GL, the Renderer menu row -- BUILT + RUN 2026-11-19 (Windows cross-build, RTX 5070)

**Pipeline cache.** The `VkPipelineCache` is read from `$S/ge007.vkcache` at start-up and written back when the backend parks (temporary file, then rename, so a crash never leaves half a cache). The driver validates the blob itself (vendor, device, cache UUID), so a file from another GPU or driver is ignored. `GE_NOSHADERWARM=1` skips it, as it skips the GL shader list. The park log now reports the pipelines built, the time spent in `vkCreateGraphicsPipelines` and the longest one.

**Selection (step 8).** `Video.Renderer` (0 OpenGL, 1 Vulkan, default 1 when the backend is built in; the maintainer chose Vulkan as the PC default) is a Display-page row ("Renderer", restart needed). `GE_RENDERER=vk` / `gl` overrides the setting, and `verify.sh` sets `GE_RENDERER=gl` (the goldens are GL captures). An ini written by an earlier build already holds `Video.Renderer = 0`, which stays. No usable Vulkan 1.3 driver -> OpenGL with a log line. The Switch keeps deko3d and has no row.

**Verified.** Cold cache (file removed): 0 KiB read, 35 pipelines in 25 ms, 673 KiB written; next run: 673 KiB read, 44 pipelines in 15 ms (the NVIDIA driver also keeps its own cache, so the gain here is small; it is larger on drivers without one). `Video.Renderer = 1` with no environment variable starts Vulkan; with `GE_RENDERER=gl` it starts GL. **CPU timings** (`GE_PERFSTAT=1`, `GE_MESHCACHE=1`, first ~3000 frames of the level, not a gameplay measurement): the display-list replay costs 0.44 ms/frame on GL and 0.15 ms/frame on Vulkan on level 09, 0.36 against 0.15 on level 36. GPU time and frame rate were not measured.

**Not done / owed.** GPU-side timings and a play-through comparison; a device-local copy of the mesh vertex buffers (they stay host-visible); the pixel comparison against GL (no readback); Linux run; AMD / Intel; MoltenVK (step 7, later).

**Confidence.** High that the cache and the selection work on NVIDIA / Windows; the performance numbers are CPU-only and early-level.

**Readback (follow-up to D996).** `GE_PCDUMP` and the screenshot key work under Vulkan: `videoPreSwapCapture` asks `gfx_vulkan_dump_window(path)`, and the present that follows copies the swapchain image into a host buffer (`vkCmdCopyImageToBuffer`, in the same submission, before the PRESENT transition), waits for the queue and writes the P6 PPM, top row first, RGB (BGRA swapchains swapped). Verified on Surface, frame 600, 2560x1440: the Vulkan and GL dumps are the same size, upright, the same colours and picture (a small camera difference from frame timing: mean difference 10.7 of 255, which is that shift, not a rendering difference). `verify.sh` still pins GL (the goldens are GL captures).
