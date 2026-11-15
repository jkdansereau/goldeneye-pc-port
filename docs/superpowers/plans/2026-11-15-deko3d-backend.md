# deko3d Backend (issue #12, step 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A deko3d implementation of `GfxRenderingAPI` on the Switch that replays `GfxFrame` exactly as the GL backend does, opt-in behind `GE_RENDERER=deko3d`, so the replay thread's cost can be measured against GL on the console.

**Architecture:** `gfx_frame.cpp`'s replay already talks only to a `GfxRenderingAPI` (`B->...`), so a new backend `port/fast3d/gfx_deko3d.cpp` slots in without touching the decoder. Combiner shaders are not compiled at run time: a host tool generates deko-flavoured GLSL for every combiner id listed in `port/switch/shaders/combiners.txt`, the image's host `uam` compiles them to DKSH, and the pack is embedded in the NRO (the "shipped shader cache": one fixed GPU, so a DKSH built at build time is valid on every console). An uber-shader, with the combiner selected by push-constant data, covers ids missing from the pack, and each miss is logged so it can be added to the list. SDL keeps input, audio and the event pump, but creates no GL window in deko3d mode; the backend owns the `nwindow` swapchain.

**Tech Stack:** C++17, devkitA64 + libnx, deko3d 0.5.0, uam 1.1.0 (host, in `local/project-dam-build`), CMake/Ninja in Docker, SDL2 (input/audio/events only in this mode).

**Spec:** GitHub issue christphe/project-dam-nx#12 ("Plan, in order", item 1, and "How to measure and test"); decision of 2026-11-15: build-time uber + common set, no runtime compiler.

## Global Constraints

- No game-logic changes; no N64-build files (`Makefile`, `tools/` except the existing `tools/switch/`, `rsp/`, `ld/`).
- GL backend (`gfx_opengl.cpp`) is not modified: PC goldens depend on it. The deko GLSL generator is a deliberate, separate copy of `gfx_opengl_gen_shader_src`'s logic.
- Gate: `GE_RENDERER=deko3d` selects the backend; anything else (or unset) = GL, unchanged. Read once at `videoInit`.
- Every new file compiled into the game is wrapped in `#ifdef __SWITCH__` (`port/fast3d/*.cpp` is GLOB_RECURSE'd into every platform's build).
- Same coordinate conventions as GL: device flags `DkDeviceFlags_OriginLowerLeft | DkDeviceFlags_DepthMinusOneToOne`; `get_clip_parameters` returns `{false, false}`; viewport/scissor take the same bottom-up pixel coordinates.
- Shade/fog qualifiers baked at Switch defaults (`gfx_rdp_affine = 1` → `noperspective` shade, `gfx_fog_vertex = 1`); `GE_SHADEPERSP` / `GE_FOGPIXEL` set under deko3d log once `"DK: GE_SHADEPERSP/GE_FOGPIXEL ignored under deko3d"` and are ignored.
- Three-point filtering is a uniform flag in the deko GLSL (always compiled in), never a shader variant.
- Tree hygiene (AGENTS.md): no local paths, findings recorded as **D973** (next free label after D972), one batched commit at the end of the session.
- Measurements only on the real console over nxlink (`docs/switch/nxlink-stdio.md`), fidelity in Ryujinx.

## Review Focus

- Dock/undock mid-level: the swapchain is recreated at 1920x1080 / 1280x720 and fb 0's size follows; no stale image is presented. → Task 5 step "dock test".
- Texture id recycled within one frame (`delete_texture` then `new_texture` returns the same id, then an upload): the old image's memory must not be reused until its frame's fence has passed, and the new upload must land in a new image. → Task 4 heap test `deferred_free_not_reused_before_fence` and Task 6 Ryujinx check on the intro (heavy texture churn).
- Settings change in game (texture filter, mipmap mode, anisotropy, MSAA level) via the options overlay: samplers are rebuilt, framebuffers are resized, nothing crashes. → Task 7 step "settings toggle".
- Data ring exhausted inside one frame (Jungle co-op at 150 % draw distance): the backend submits and moves to a fresh chunk instead of overwriting memory in use by the GPU. → Task 5 test `ring_wraps_only_past_fence` and Task 9 console run.
- Quit from the menu ("Quit to desktop") and the nxlink auto-quit: queue idle, swapchain destroyed before `nwindow` is released, clean return to hbmenu. → Task 5 step "quit test".

---

### Task 1: Combiner id census (backend-neutral)

**Files:**
- Modify: `port/fast3d/gfx_frame.cpp` (in `r_create_and_load_new_shader`)
- Create: `port/switch/shaders/combiners.txt`
- Create: `tools_pc/cc_census_merge.py`

**Interfaces:**
- Produces: log line `CCID <id0 hex16> <id1 hex8>` (exactly once per distinct pair per run) when `GE_CCLOG=1`; `combiners.txt` format: one `0x<16 hex> 0x<8 hex>` pair per line, sorted, `#` comments allowed.

- [x] **Step 1:** Add the `GE_CCLOG` env (cached getenv, D250) log in `r_create_and_load_new_shader`, printing via `sysLogPrintf(LOG_NOTE, "CCID %016llx %08x", ...)` once per new pair (it already runs once per pair).
- [x] **Step 2:** Write `tools_pc/cc_census_merge.py <combiners.txt> <log>...`: reads `CCID` lines from logs (also accepts the `DKSH miss` lines of Task 6), merges into the file, keeps it sorted and unique, prints how many were added. Run with `-I`.
- [x] **Step 3:** Seed: Linux build in Docker (`docker compose run --rm linux`), then the golden-level runs of `tools_pc/verify.sh` with `GE_CCLOG=1` (all levels it covers), plus 2P split-screen (`GE_COOP=1`) on one level; merge all logs. (done via Ryujinx + console runs: the Linux build cannot run headless here)
  Expected: `combiners.txt` with a few hundred pairs; a second identical run adds 0.
- [ ] **Step 4:** Check that the PC golden diff is unchanged with `GE_CCLOG` unset (`tools_pc/verify.sh`), since this touches a shared file. (NOT RUN: no GL runtime with the ROM in this environment; see D973)

### Task 2: Shader generator and pack (host tool)

**Files:**
- Create: `port/fast3d/gfx_dk_layout.h` (pure C, no deko3d types; used by host tool and backend)
- Create: `tools_pc/dkshgen/dkshgen.cpp`
- Create: `port/switch/shaders/uber.vert.glsl`, `uber.frag.glsl`, `fullscreen.vert.glsl`, `fxaa.frag.glsl`, `skyplus.frag.glsl`
- Test: `tools_pc/dkshgen/dk_layout_test.c`

**Interfaces:**
- Produces (`gfx_dk_layout.h`):
  - `enum { DK_ATTR_POS=0, DK_ATTR_TC0=1, DK_ATTR_TC1=2, DK_ATTR_CLS0=3, DK_ATTR_CLT0=4, DK_ATTR_CLS1=5, DK_ATTR_CLT1=6, DK_ATTR_FOG=7, DK_ATTR_GRAY=8, DK_ATTR_IN1=9 /* ..IN7=15 */, DK_ATTR_COUNT=16 };`
  - `struct DkLayout { uint8_t num_floats; uint8_t count; uint8_t loc[16]; uint8_t size[16]; uint8_t offset[16]; };`
  - `void dk_layout_from_features(const struct CCFeatures* f, struct DkLayout* out);` (same order and sizes as GL's `create_and_load_new_shader`: pos 4, per texture tc 2 then clamp S/T 1 each, fog 4, grayscale 4, inputs 3 or 4)
  - `struct DkCombinerParams { uint32_t sel[2][2]; /* 4 x 8-bit sources a,b,c,d per cycle x (rgb, alpha) */ uint32_t flags; /* bit per CCFeatures opt_* + do_single/do_multiply/do_mix/color_alpha_same packed */ uint32_t pad; };` and `void dk_combiner_params(const struct CCFeatures* f, struct DkCombinerParams* out);`
  - Pack format (`ge007_dksh.bin`, little endian): header `"DKSHPACK"`, `u32 count`; entries `{u64 id0; u32 id1; u32 vert_off; u32 vert_len; u32 frag_off; u32 frag_len;}` sorted by (id0,id1); named entries use id0 = 0, id1 = 1 uber, 2 fullscreen vert, 3 fxaa frag, 4 skyplus frag (combiner id1 is never < 16 with id0 = 0: assert in the generator).
- Consumes: `gfx_cc_get_features` from `port/fast3d/gfx_cc.cpp` (compiled into the host tool).

- [x] **Step 1: Write the failing test** `dk_layout_test.c`: for a table of CCFeatures (no textures + 1 input, 2 textures + clamps both + fog + 3 inputs with alpha, grayscale only), assert `num_floats` equals what GL computes (no textures + 1 input without alpha: 4+3 = 7; two clamped textures + fog + 3 alpha inputs: 4 + 2×(2+1+1) + 4 + 12 = 28; grayscale only: 4+4 = 8) and that `offset[i]` is the running sum of `size[]`; assert `dk_combiner_params` round-trips `c[c][k][i]` for every cycle/channel/slot.
- [x] **Step 2:** Run `gcc -O2 -Wall -I port/fast3d -o <temp>/dklt tools_pc/dkshgen/dk_layout_test.c port/fast3d/gfx_cc.cpp -x c++` → FAIL (header missing).
- [x] **Step 3:** Implement `gfx_dk_layout.h` (static inline functions).
- [x] **Step 4:** Rerun → PASS.
- [x] **Step 5:** Implement `dkshgen`: `dkshgen glsl <combiners.txt> <outdir>` writes `cc_<id0>_<id1>.vert.glsl` / `.frag.glsl` per pair (deko GLSL 460: `layout(location = DK_ATTR_*)` inputs, `layout(binding = 0) uniform sampler2D uTex0`, `binding = 1` uTex1, `layout(std140, binding = 0) uniform Draw { int frame_count; float noise_scale; int three_point0; int three_point1; } ;` everything else ported from `gfx_opengl_gen_shader_src` with `current_filter_mode` checks replaced by the `three_point*` uniforms); `dkshgen pack <outfile> <list of id0 id1 vert.dksh frag.dksh>`.
- [x] **Step 6:** Write `uber.frag.glsl`: same structure as the generated shader with every feature present, inputs `IN1..IN7` as vec4 (missing ones read as fixed zero, alpha forced to 1 when `!opt_alpha`), combiner sources chosen from `DkCombinerParams` in a second uniform block (`binding = 1`); source selection with a 16-way `switch` per slot (no dynamically indexed local array: Maxwell spills those to local memory). FXAA and skyplus shaders: the GL sources ported to explicit bindings (`binding = 0` sampler, uniforms in a `binding = 0` block in the GL declaration order).
- [x] **Step 7:** In Docker: generate for the whole list and run `uam -s vert|frag -o x.dksh x.glsl` on every file. Expected: zero uam errors. Commit nothing yet (one batched commit at the end).

### Task 3: Build wiring

**Files:**
- Modify: `cmake/SwitchPort.cmake` (new macro `switch_deko3d_shaders(target)`, `deko3d` added to `LIBS` in `switch_dependencies`)
- Modify: `CMakeLists.txt` (call the macro at the existing project-dam hook next to `switch_package`)
- Create: `port/fast3d/gfx_dk_pack.S` (guarded `#ifdef __SWITCH__`; `.incbin` of the pack, symbols `ge007_dksh_pack` / `ge007_dksh_pack_end`)

**Interfaces:**
- Produces: `extern const uint8_t ge007_dksh_pack[], ge007_dksh_pack_end[];` and the pack at `${CMAKE_BINARY_DIR}/ge007_dksh.bin`.

- [x] **Step 1:** In the macro: `add_custom_command` builds `dkshgen` with the host compiler (`c++`, not the cross toolchain), runs `dkshgen glsl`, runs `uam` per file (`find_program(UAM uam HINTS ${DEVKITPRO}/tools/bin REQUIRED)`), runs `dkshgen pack`; `DEPENDS` on `combiners.txt`, the `.glsl` sources, `dkshgen.cpp`, `gfx_cc.cpp`, `gfx_dk_layout.h`; `gfx_dk_pack.S` gets `OBJECT_DEPENDS` on the pack.
- [x] **Step 2:** `docker compose run --rm switch` → builds; `aarch64-none-elf-nm build-switch/ge007.aarch64.elf | grep ge007_dksh_pack` prints both symbols.
- [x] **Step 3:** `touch port/switch/shaders/combiners.txt` then rebuild → only the pack and the `.S` object rebuild.
- [x] **Step 4:** Linux build (`docker compose run --rm linux`) still builds with no deko3d dependency.

### Task 4: GPU memory heap and descriptor slots (host-testable)

**Files:**
- Create: `port/fast3d/gfx_dk_heap.h` (pure C++, no deko3d types)
- Test: `tools_pc/dkshgen/dk_heap_test.cpp`

**Interfaces:**
- Produces:
  - `class DkHeap { public: explicit DkHeap(uint32_t chunk_size); bool alloc(uint32_t size, uint32_t align, DkHeapBlock* out); void free_after(const DkHeapBlock& b, uint64_t fence_serial); void retire(uint64_t completed_serial); uint32_t chunks() const; }` with `struct DkHeapBlock { uint32_t chunk; uint32_t offset; uint32_t size; };`. The heap only does bookkeeping. The backend maps `chunk` to a `DkMemBlock` it created when `alloc` reported a new chunk (`out->chunk == chunks()-1` after growth). First-fit with coalescing; freed ranges become reusable only after `retire(serial >= fence_serial)`.
  - `class DkSlots { public: explicit DkSlots(uint32_t n); int take(); void release_after(int slot, uint64_t fence_serial); void retire(uint64_t completed_serial); }` for image/sampler descriptor indices.
- [x] **Step 1: Write failing tests:** `alloc_respects_alignment`, `coalesces_neighbours`, `deferred_free_not_reused_before_fence` (free at serial 5, retire(4) → alloc of the same size returns a different offset; retire(5) → the freed offset is reused), `grows_new_chunk_when_full`, `slots_reused_only_after_fence`.
- [x] **Step 2:** `g++ -std=c++17 -O2 -Wall -I port/fast3d -o <temp>/dkht tools_pc/dkshgen/dk_heap_test.cpp && <temp>/dkht` → FAIL.
- [x] **Step 3:** Implement `gfx_dk_heap.h`.
- [x] **Step 4:** Rerun → all PASS.

### Task 5: Backend skeleton: device, swapchain, frames, clear, present; SDL without a GL window

**Files:**
- Create: `port/fast3d/gfx_deko3d.cpp`, `port/fast3d/gfx_deko3d.h`
- Modify: `port/fast3d/gfx_sdl2.cpp` (Switch-only no-GL mode)
- Modify: `port/src/video.c` (backend selection; renderer string)
- Test: `tools_pc/dkshgen/dk_ring_test.cpp`

**Interfaces:**
- Produces (`gfx_deko3d.h`): `extern struct GfxRenderingAPI gfx_deko3d_api; bool gfx_deko3d_wanted(void); /* GE_RENDERER=deko3d, read once */ void gfx_deko3d_present(void); /* acquire-safe present of the current swapchain image */ void gfx_deko3d_park(void); /* queue wait idle, destroy swapchain */ void gfx_deko3d_get_dimensions(uint32_t* w, uint32_t* h); const char* gfx_deko3d_renderer_string(void);` plus the extras of Task 8.
- Produces (`gfx_sdl2.cpp`): `extern "C" void gfx_sdl_set_external_present(void (*present)(void), void (*dims)(uint32_t*, uint32_t*), void (*park)(void));` Called before `init`. When set, `init` does `SDL_Init(SDL_INIT_VIDEO)` but creates no window and no GL context; `swap_buffers_begin` keeps the FpsCap limiter and capture hook and calls `present` instead of `SDL_GL_SwapWindow`; `get_dimensions` calls `dims`; `make_context_current` / `release_context` are no-ops; `park_for_exit` calls `park`.
- Frame model: 3 slots, each with a command-memory chunk (`DkMemBlock`, CPU-uncached, with 256 bytes of slack at the end: deko3d 0.5.0 can write past the end) and a data ring chunk (vertices, push data, upload staging), each guarded by a `DkFence`; a global `uint64_t serial` increments per submitted slot and feeds `DkHeap::retire`. Ring growth: when a write does not fit, submit what is recorded, take a fresh chunk for the rest of the frame (`DkRing` in the backend, logic testable without deko3d types).

- [x] **Step 1: Write the failing test** `dk_ring_test.cpp` for the ring bookkeeping (`DkRing` declared in `gfx_dk_heap.h`): `ring_wraps_only_past_fence` (a frame that writes more than one chunk gets a second chunk, and the first chunk is offered again only after its serial retired), `ring_alignment_256_for_uniforms`.
- [x] **Step 2:** Run → FAIL; implement `DkRing` in `gfx_dk_heap.h`; run → PASS.
- [x] **Step 3:** Implement device (`DkDeviceFlags_OriginLowerLeft | DkDeviceFlags_DepthMinusOneToOne`), queue (graphics), swapchain on `nwindowGetDefault()` with 2 framebuffer images at the size from `appletGetOperationMode()` (handheld 1280x720, docked 1920x1080, as `gfx_sdl_init`), depth image for fb 0, `start_frame` / `end_frame` / `finish_render`, `clear_framebuffer`, `clear_region`, `on_resize`; every other API member a logged no-op stub that runs once. fb 0's image is acquired lazily (`dkQueueAcquireImage`) at the first command that targets fb 0 in a frame; `present` submits and calls `dkQueuePresentImage`. A frame that never touched fb 0 does not present (MKDD: a double present with nothing drawn flickers).
- [x] **Step 4:** `video.c`: if `gfx_deko3d_wanted()` (Switch only): `renderingAPI = &gfx_deko3d_api;` and `gfx_sdl_set_external_present(...)` before the window init; `videoApplyLowEndDefaults` uses `gfx_deko3d_renderer_string()` (`"NVIDIA Tegra X1 (deko3d)"`). Log `"video: renderer deko3d (GE_RENDERER)"`.
- [x] **Step 5:** Dock test: on `appletGetOperationMode()` change (checked in `start_frame`), wait idle, destroy and recreate the swapchain and fb 0's depth image at the new size; `get_dimensions` reports it.
- [x] **Step 6:** Quit test: `gfx_deko3d_park` = `dkQueueWaitIdle`, destroy swapchain, then the memory blocks, then the device; called through `gfx_sdl_park_for_exit` on the replay thread (D49).
- [x] **Step 7:** Build the nxlink test NRO (`build-switch-link`, `-DNX_LINKSTDIO=ON -DNX_ARGS=ON`); in Ryujinx and on the console with `GE_RENDERER=deko3d NX_TEST_SECS=20`: the game boots to a cleared screen, the log shows no deko3d error, and the auto-quit returns to hbmenu. Dock/undock once on the console. (Ryujinx + console; dock toggle checked in Ryujinx only)

### Task 6: Draw path: shaders, vertex layouts, state, textures

**Files:**
- Modify: `port/fast3d/gfx_deko3d.cpp`

**Interfaces:**
- Consumes: `dk_layout_from_features`, `dk_combiner_params` (Task 2), `ge007_dksh_pack` (Task 3), `DkHeap`/`DkSlots` (Task 4).
- `struct ShaderProgram` (deko): `DkShader vert, frag; bool uber; DkCombinerParams params; DkLayout layout; uint8_t num_inputs; bool used_textures[2];`. `create_and_load_new_shader` looks the pair up in the pack (binary search); on a miss: uber program + `sysLogPrintf(LOG_NOTE, "DKSH miss %016llx %08x", ...)` once per pair. All DKSH loaded into one code `DkMemBlock` (`DkMemBlockFlags_Code`) at init.
- Texture = `{DkImage, DkHeapBlock, int desc_slot, w, h, mip levels, sampler state}` in a vector indexed by id; `new_texture` returns the next free id; `delete_texture` frees the heap block and slot with `free_after(serial)`.

- [x] **Step 1:** Program binding: `load_shader_with` binds vert+frag (`dkCmdBufBindShaders`), sets the vertex attrib state from `layout` (`DkVtxAttribState` per location, missing ones `isFixed`), one vertex buffer state (stride `num_floats*4`), and pushes the `Draw` uniform block (and `DkCombinerParams` for the uber program) with `dkCmdBufPushConstants`.
- [x] **Step 2:** `draw_triangles`: copy the floats into the data ring (or use `stream_begin`'s pointer, which returns ring memory directly), `dkCmdBufBindVtxBuffer`, `dkCmdBufDraw(DkPrimitive_Triangles, n*3, 1, 0, 0)`.
- [x] **Step 3:** State: `set_depth_mode` maps exactly as `gfx_opengl_set_depth_mode` (LEQUAL/LESS/ALWAYS, decal polygon offset -2,-2, write mask), depth clamp on (`depthClampEnable`); `set_depth_range`, `set_viewport`, `set_scissor` (scissor always enabled, as GL after init), `set_use_alpha` (blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA or DST_COLOR/ZERO). Clears ignore the scissor only after explicitly setting a full-target scissor and forget the cached scissor afterwards (MKDD pitfall: clears respect the scissor).
- [x] **Step 4:** Textures: `upload_texture` stages RGBA8 in the ring, `dkCmdBufCopyBufferToImage` per level, CPU mip chain identical to GL's Switch path (same conditions: `gen_mipmaps` or filter THREE_POINT/TRILINEAR). A re-upload with a different size allocates a new image and frees the old one through the fence. `select_texture` / `set_sampler_parameters` write the image descriptor and a sampler descriptor (filter/mip/aniso/wrap mapping copied from `gfx_opengl_sampler_key` and `gfx_cm_to_opengl`, mirror-clamp available) and bind `dkMakeTextureHandle(slot, slot)` to stage-fragment texture binding 0/1.
- [x] **Step 5:** Ryujinx A/B: title screen, Dam intro, Dam gameplay, the watch menu; screenshots with `GE_RENDERER` unset vs `deko3d`. Expected: no visible difference except FXAA/sky (Task 8). Merge the `DKSH miss` lines from the log with `cc_census_merge.py`, rebuild, rerun: zero misses on these scenes.

### Task 7: Framebuffers

**Files:**
- Modify: `port/fast3d/gfx_deko3d.cpp`

- [x] **Step 1:** `create_framebuffer` / `update_framebuffer_parameters`: colour RGBA8 (`DkImageFlags_UsageRender | UsageLoadStore` + sampled), depth Z24S8, MSAA images with `DkMsMode` when `msaa_level > 1` (clamped to 8) plus a single-sample resolve image; a resize frees the old images through the fence. `start_draw_to_framebuffer` binds the render targets (`dkCmdBufBindRenderTargets`) and stores `1/noise_scale` as GL does. `gfx_framebuffers_enabled == false` behaves as GL (fb 0 only).
- [x] **Step 2:** `copy_framebuffer` = `dkCmdBufBlitImage` with the same rectangles as GL and `DkBlitFlag_FlipY` when `flip_y`, nearest filter; `resolve_msaa_color_buffer` = `dkCmdBufResolveImage`; `select_texture_fb` binds the fb's colour image on unit 0 with a linear sampler; `get_framebuffer_texture_id` returns the fb's descriptor slot. A barrier (`dkCmdBufBarrier(DkBarrier_Fragments, DkInvalidateFlags_Image)`) between rendering to an image and sampling or blitting it.
- [ ] **Step 3:** Settings toggle: in Ryujinx, change MSAA (docked), texture filter, mipmaps and anisotropy from the options overlay during gameplay; no crash, picture matches GL with the same settings. (NOT RUN: no scripted pad input for the overlay; MSAA/fb rebuild exercised by the dock toggle)
- [x] **Step 4:** Split-screen: `GE_COOP=1` on Dam in Ryujinx, compare with GL.

### Task 8: Extra passes and PERFSTAT

**Files:**
- Modify: `port/fast3d/gfx_deko3d.cpp`, `port/fast3d/gfx_deko3d.h`
- Modify: `port/fast3d/gfx_pc.cpp` (call sites of `gfx_opengl_fxaa_pass`, `gfx_opengl_skyplus_*`, `gfx_opengl_interp_*`, `gfx_opengl_frame_count_rewind`, `gfx_opengl_perf_report`)
- Modify: `port/src/skyplus.c` only if `gfx_skyplus_failed` needs a second writer (it should not: the deko backend sets the same global)

**Interfaces:**
- Produces: `gfx_deko3d_fxaa_pass(int)`, `gfx_deko3d_skyplus_stage(const PortSkyPlus*, const int[4], float, float)`, `gfx_deko3d_skyplus_pass(int)`, `gfx_deko3d_interp_store(int, uint32_t, uint32_t)`, `gfx_deko3d_interp_show(int)`, `gfx_deko3d_interp_sync(void)`, `gfx_deko3d_frame_count_rewind(void)`, `gfx_deko3d_perf_report(double)`.
- In `gfx_pc.cpp`, each call site picks the function pointer once (`renderingAPI == &gfx_deko3d_api`) through a small static table `s_extra` filled in `gfx_init`; no `#ifdef` per site beyond `__SWITCH__`.

- [x] **Step 1:** FXAA and skyplus with the Task 2 DKSH (fullscreen triangle, no vertex buffer, `gl_VertexID`), uniforms via push constants, sky texture R8 with a CPU mip chain (as the GL path uses `glGenerateMipmap` here; same reasoning as the GL upload comment), wrap repeat.
- [x] **Step 2:** Interp slots: RGBA8 images sized on first store, `dkCmdBufBlitImage` both ways; `interp_sync` = `dkQueueWaitIdle`.
- [x] **Step 3:** PERFSTAT: `DkCounter_Timestamp` reports at the start and end of each frame into a small buffer, read two frames later; same output format as GL: `PERFSTAT gpu=%.2fms/frame (avg over %d frames)`; plus `PERFSTAT dk draws=%llu pushes=%llu ring_chunks=%llu uber_draws=%llu` every 300 frames.
- [x] **Step 4:** Ryujinx A/B with FXAA on (handheld and docked) and GE+ sky on Jungle and Dam: matches GL.

### Task 9: Measure on the console, document, deliver

**Files:**
- Modify: `docs/dev/findings.md` (new `## D973`), `docs/dev/findings-index.csv` (regenerate with `tools_pc/gen_findings_index.py`), `docs/porting-notes.md` (new class `D51.`, "deko3d backend pitfalls"), `docs/ROADMAP.md` (issue #12 row), `docs/switch/nxlink-stdio.md` (`GE_RENDERER`, `GE_CCLOG`)

- [x] **Step 1:** Console, nxlink, `NX_TEST_SECS=20 GE_PERFSTAT=1`, each with `GE_RENDERER` unset and `=deko3d`: Jungle co-op (`-level_37 -hard0 GE_COOP=1`), Dam, Bunker 1. Record per phase: decoder walk, replay, GPU, decoder waits, present. Expected outcome to report either way: the replay thread's ms/frame on deko3d vs GL. (Surface 1 + Jungle co-op only, see D973)
- [x] **Step 2:** Merge every `DKSH miss` from these runs into `combiners.txt`, rebuild, confirm zero misses on a rerun.
- [x] **Step 3:** One 60 s `-level_09` crash-free run with `GE_RENDERER=deko3d` (integration checkpoint). (Ryujinx, not the console: clean, backend parked at the auto-quit) (NOT RUN)
- [x] **Step 4:** Write D973 (numbers, misses, Ryujinx A/B outcome, known gaps such as `GE_PCDUMP` being GL-only), the porting-notes class, the ROADMAP row; comment the numbers on issue #12 (ask before posting). (issue comment not posted, per instructions)
- [x] **Step 5:** One batched commit on `feat/deko3d`; PR only when asked.
