# D977

## D977: GE+ sky under the horizon showed clouds or the previous frame (Cradle) -- the shader's early return was not honoured in Ryujinx; one final write now -- FIXED + MAINTAINER-VERIFIED 2026-11-16 (Ryujinx, Cradle, GL and deko3d; console unaffected)

**Symptom.** On Cradle, looking down from the cradle, the view under the horizon showed a layer of saturated blue clouds, streaked when the camera moved; with Sky = Original it is the flat sky colour (as on the N64: a maintainer's N64 capture shows a flat light blue under the cradle). Dam and Surface hide it behind their ground.

**Cause.** Not the level and not GE's sky: with the GE+ command dropped (GE's sky still skipped) nothing is drawn there and the screen keeps old frames. A log of the GL pass (`SKYPASS`) showed one pass per frame, the right viewport and corner rays (top y 0.96, bottom y -0.08). The shader returned early under the horizon (`if (d.y <= 0.0) { o = F; return; }`), yet the pixels there showed exactly the cloud code run on a downward ray: `fr = 1 - min(1, 2 dy / hz) * gain` goes above 1, the cloud shade `(1 - fr)` goes negative and removes the red and green of Cradle's yellow cloud tint, leaving blue. Test colours written before the return (magenta, solid red) never showed. So Ryujinx did not honour the early return in this fragment shader (GL and deko3d, which both run through its shader translation). Console check (2026-11-16, nxlink, `main` before this fix, deko3d, Cradle): no clouds under the horizon on the hardware, so the bug is Ryujinx's only; the fix changes nothing on the console.

**Fix (port layer only).** Both sky shaders (`gfx_opengl.cpp` and `port/switch/shaders/skyplus.frag.glsl`) keep a `below` flag and choose the colour at the single write at the end: the record's sky colour under the horizon, the GE+ sky above. A NaN ray counts as under the horizon.

**Verification.** Maintainer, Ryujinx, Cradle: flat sky colour under the horizon, GE+ sky above, with GL (`GE_RENDERER=gl`) and with deko3d. Switch NRO builds (Docker).

**Confidence.** High on the fix (direct A/B) and on the cause being Ryujinx-only (console run without the fix is clean); the exact translation fault is not read in Ryujinx's source.
