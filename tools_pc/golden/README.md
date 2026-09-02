# Golden baseline frames

`frame_NNNNNN.png` — reference `GE_PCDUMP` captures for `framediff.py`
(`-level_09` BUNKER1 intro pan). Compared structurally, not exactly — the port
is not frame-deterministic (see `docs/internals.md` "D117").

Regenerate after a deliberate visual change:

```sh
GE_PCDUMP="200-440:120" ./build-pc/ge007.x86_64.exe -level_09
python tools_pc/framediff.py ppm --update    # then rm any stray *.png it copied
```

## History

- **D168 (2026-09-01):** the `GE_PCDUMP` PPM writer emitted `glReadPixels` rows
  unreversed, so every capture — and this baseline set — was vertically
  flipped. Writer fixed (`port/fast3d/gfx_opengl.cpp`); this set regenerated
  from a fresh, correctly-oriented `-level_09` run.
