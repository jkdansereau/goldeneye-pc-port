# D986

## D986: Switch ground shading showed a dark patch that follows the view -- deko3d pack made perspective-correct -- FIXED + MAINTAINER-CHECKED 2026-11-17 (Ryujinx, Dam at the wall below the hangars)

**Symptom.** Dam, ground at the wall below the hangars: a dark patch whose position follows the opposite of the view direction, with the mesh cache off as well (darker with it, D985). ares does not show it.

**Cause.** The deko3d shader pack is built with `noperspective` shade and per-vertex fog (D540, the RDP's screen-affine shade). On the big Dam ground triangles the screen-affine interpolation of their unevenly lit corners puts a dark band where the view is. With the GL renderer and `GE_SHADEPERSP=1` (perspective-correct shade, cache off) the patch is gone.

**Fix.** `tools_pc/dkshgen/dkshgen.cpp` (combiner pack) and `port/switch/shaders/uber.{vert,frag}.glsl` (uber-shader) interpolate the shade, the combiner inputs and the per-vertex fog with perspective; `gfx_rdp_affine = 0` on deko3d. Always, not under Video.Visuals: the N64 does not show the patch. The PC GL renderer is unchanged (PC goldens depend on it).

**Also affects.** Every level's shading on the Switch: gradients on large triangles near the camera now follow the perspective. D548's clamp of the combiner inputs stays.
