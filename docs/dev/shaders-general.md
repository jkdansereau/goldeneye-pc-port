# GE+ shaders on the Switch backend: general lessons

Read this before touching `port/switch/shaders/*.glsl` or the sky pass in `port/fast3d/gfx_deko3d.cpp`.
The sea and the sky have their own notes: [shaders-sea.md](shaders-sea.md), [shaders-sky.md](shaders-sky.md).
Everything here was learned the hard way on issue #38 (Scenery (Enhanced) on water levels); each item cost at least one wasted build/run cycle.

## How the shaders are built and run

- Shaders are GLSL compiled **offline by uam** (`cmake/SwitchPort.cmake`) into a pack the backend loads. There is no runtime compiler.
- `skycommon.glsl` is not a shader: cmake pastes it at the `#pragma skycommon` line of `skyplus`, `water` (Dam's own water) and `skylut`. Every function in it is compiled three times, and a global it declares exists in all three. A change that makes sense for the sky pass can break `water.frag.glsl`, which has no view rays (`uC0..uC3` are zero there) and no sea panorama.
- The sky pass is a fullscreen triangle (`fullscreen.vert.glsl`). It runs several times per frame with different `uPass` values (`uC0_.w`); see the sea and sky notes.
- Uniforms: `block Sky`, 16 vec4 (256 bytes), pushed by `draw_fullscreen` at UBO binding 0 and, for the water draws, at binding 2. The members are listed at the top of `skycommon.glsl` and mirrored in `struct DkSkyUniforms`.
  Any new member needs both sides and the three shaders that declare the block.
- Fragment UBO bindings 0, 1, 2 are the ones known to work. Binding 1 is the uber shader's per-draw params slot: anything pushed there is overwritten by the next draw, which is fine for a pass that draws immediately after the push. **A new binding (3) was tried and never shown to work; do not add one without a minimal test first.**

## Traps specific to deko3d / Ryujinx / uam

1. **A shader that fails to compile does not stop the build.** The error is printed in the middle of a long cmake log and the old pack stays in the NRO. Never filter the build with `grep | tail` and call it done: look for `Shader failed to compile` and `error:`, and make sure the NRO is newer than the last shader edit. Twice a "measurement" was run against a stale pack.
2. **uam is stricter than Mesa/AMD.** An identifier that only the test viewer declares (`uPlay`) is an error in uam. `const` arrays and `vec2[8](...)` are fine. Keep temporary code to things the game's shaders also declare, or strip it before building for the console.
3. **Render targets are stored top row first.** A fullscreen pass writes at `vUV` (y up) and every later read of that target uses `vec2(x, 1.0 - y)`. This applies to any texture a pass renders into and another pass samples (`uHalfSea`, `uSeaPano`). The standalone viewer is GL (y up) and rewrites these reads when it loads the shader.
4. **Never use `gl_FragCoord`**; its orientation differs between targets and Ryujinx/hardware. Use `vUV`.
5. **`dFdy` is 0 in the sky pass under Ryujinx**, so derivatives of anything derived from the screen position must be computed analytically. `dFdx` works. The sea's `gDpdy` is analytic (see the sea note).
6. **NaN and inf become black on NVIDIA (Ryujinx host), not on AMD.** `clamp(NaN, 0, 1)` gives 0. Near the horizon the elevation tends to 0, distances and pixel footprints blow up, and a mip level computed from them is garbage. Clamp footprints and distances explicitly; test on both a real console and the Ryujinx (NVIDIA) build, not only the AMD viewer.
7. **fp16 is not supported by uam.** There is no cheap half-precision trick.
8. The shader never gets a depth/derivative guarantee outside the pass that sets it up: set `gDpdx/gDpdy/gDir`-style globals in the pass, and give them a safe default (zero) for the callers that never set them.

## Performance facts (Switch console, Frigate, 1080p docked)

- Full-width sea: 15.88 ms GPU. Sea drawn at **half width** into its own target: 11.38 ms. Quarter width: 11.22 ms. Below half, the sea's pixel count is no longer what costs.
- Anisotropic filtering on the wave texture: 8x cost about 7 ms; **2x is the setting** (and it is what gives the far sea its fine, horizontal, photo-like streaks).
- The budget is 60 fps (about 12 ms of GPU for this scene on the console).
- The AMD integrated GPU in the viewer is **not** a proxy for the Switch: baking the far sea did not change the viewer's frame time (4.46 vs 4.42 ms at 1080p) because there the sky was 1.2 ms of 4.5 and the rest was fixed cost. Only the console tells.

## How to work on these shaders (what went wrong and the rule that follows)

| What happened | Rule |
|---|---|
| Several changes per build; the cause of a regression was unknowable. | One change per run. Name the run (`bake1`, `taa4`...) and keep its screenshot. |
| A result in the AMD viewer was taken as proof; the game showed black or flat. | Validate in the game (Ryujinx, then console) before saying "works". The viewer is for iterating on a look. |
| Screenshots compared at different positions (the test teleport races with the user's pad). | Check the `TMPPOS` line of the log before comparing two captures. Tell the user not to touch the pad during a run. |
| Numbers chosen from one image. | Take a sweep (3-4 values side by side), look at all of them, then pick. Use the reference photo, not memory. |
| A metric (second temporal difference) called TAA "worse" while the eye said better. | A metric that disagrees with the user's eye is the wrong metric. Do not argue from it. |
| Complex fixes (TAA, tangent continuation, view-space grain) before the simple one (true plane, mips, aniso 2). | Try the plain physical answer first and look at it. See the sea note for the list of rejected approaches. |
| `\n` inside a C string written through a shell heredoc or a Python string became a real newline: two broken builds. | Edit source files with the Edit/Write tools; never push escape sequences through shell quoting. |
| Long chained commands (build + run + capture + analyse) were interrupted and left the state unknown. | Short steps. After an interruption, check what ran (processes, files) before redoing it. |
| A cache/bake keyed on a drifting value (cloud offsets) re-baked every 2 seconds and the picture popped; then keyed on too little (sun only) and a bake made at spawn height stayed wrong after the teleport: black sea. | For anything cached or baked: write down every input, test at level start, after a teleport and after a pause, and make the key cover exactly the inputs that matter. |

## Test tooling

- Ryujinx: build `build-switch-args` (reads `data/ge007.args`), launch, PrintWindow screenshot. The reference spot on Frigate is `GE_TELEPORT=595.3,521.9,690.4,296` with `-level_26 -hard0`.
- Console: `tools/switch/nxlink.py` to the console's netloader (hbmenu, Y). The console must have no game running.
- **`tools_pc/seaview/`** (README there): a PC viewer (Win32 + OpenGL) and a console viewer (deko3d, sent by nxlink) that draw the real shaders from one captured frame (`GE_SEADUMP=1`), with variant menus, every `PRM` value editable and saved to an `.ini`, render scale, GPU time. The console viewer compiles the `.ini`'s values in as constants, as the game's build does. **The viewer only shows what it shares with the game**: the bake logic in the game changed once without the viewer, and the viewer hid the game's bug. Keep them in step.
- Use **Ryubing** for screenshots of this sea, never the old Ryujinx r.49574a9 (it mis-renders the far sea).
