# Golden baseline frames

`frame_NNNNNN.png` — reference `GE_PCDUMP` captures for `framediff.py`
(`-level_09` BUNKER1 intro pan). Compared structurally, not exactly — the port
is not frame-deterministic (see `docs/internals.md` "D117").

## D168 re-orientation (2026-09-01)

The `GE_PCDUMP` PPM writer (`port/fast3d/gfx_opengl.cpp
gfx_opengl_dump_bound_fbo`) used to emit `glReadPixels` rows unreversed, so
every capture — and therefore these goldens — was **vertically flipped**. The
writer now emits rows top-to-bottom. The PNGs in this directory were flipped
vertically in place to match; content is otherwise unchanged. Regenerate them
properly from a fresh run when convenient:

```sh
GE_PCDUMP="200-440:120" ./build-pc/ge007.x86_64.exe -level_09
python tools_pc/framediff.py ppm --update
```
