# seaview: the GE+ sky and sea, outside the game

Tune the sky and the sea on the PC, then check the look and the GPU time on the console, without
playing the game each time. Both viewers draw with the game's own shaders (`port/switch/shaders/`)
from one captured frame of Frigate. Background: [docs/dev/shaders-sea.md](../../docs/dev/shaders-sea.md),
[shaders-sky.md](../../docs/dev/shaders-sky.md), [shaders-general.md](../../docs/dev/shaders-general.md).

## Setup

Copy `.env.example` to `.env` at the repo root and fill it in (console IP, emulator, Docker image,
a Windows g++). `.env` is ignored by git.

## Commands

```sh
python tools_pc/seaview/seaview.py dump        # capture Frigate in the Switch emulator -> data/sea_dump.bin
python tools_pc/seaview/seaview.py pc          # build and open the PC viewer (--hp: ask for the discrete GPU)
python tools_pc/seaview/seaview.py nx-send --ini tools_pc/seaview/data/seaview.ini   # console viewer, with those settings
python tools_pc/seaview/seaview.py nx-build    # rebuild the console viewer (after a shader change: nx-send --build)
python tools_pc/seaview/seaview.py ini2args F  # the console arguments an .ini turns into
```

The capture holds GE's cloud texture, which comes from the ROM: it stays in `data/`, which git ignores,
and so does the console NRO that embeds it.

## PC viewer

- Menus **Sea tests** / **Sky tests**: variants of the near sea and of the sky (viewer only: the shaders
  compile them under `#ifdef VIEWER`; the game never defines it). **Load**: the sky and sea passes drawn
  1-64 times per frame, so differences show on a fast GPU (the title gives the time per pass).
- **Settings** window: every value the shaders take (sun, sky, clouds, dome curvature, eye height, water,
  waves, Fresnel, bake distances; `0` for the bake start = no bake), with colour pickers, a reset per value,
  and save/load to an `.ini`. `data/seaview.ini` is read at start.
- View: click in it for mouse look (Escape releases), WASD or ZQSD, Shift = fast, Space/E up, Ctrl down.
- The GPU time of the PC says nothing about the console: compare variants with it, measure on the console.

## Console viewer

`nx-send` sends `nx/seaview-nx.nro` through nxlink (hbmenu, netloader: Y) with the `.ini` turned into
arguments. It draws the same passes as the game's sky draw (reflections, far-sea bake, the sea at half
width, the final pass) at 1920x1080 and prints the GPU time on the nxlink output every second.

Pad: left stick move, right stick look, ZL/ZR down/up, B (held) fast (700 units/s, 6000 held), D-pad up/down sea variant,
D-pad left/right sky variant (live build only), L/R load x1-x64, Minus render scale 1, 1/2, 1/4 (the sky and sea drawn smaller and enlarged; `scale=N` argument), Y sway, X freeze time, A re-bake, + quit.

## How the editable values reach the shaders

`PRM(i, default)` in `skycommon.glsl` is the constant `default` in the game and `uPrv[i]` (block `Viewer`,
binding 1) in the viewers. A value found good in the viewer becomes the new constant in the shader.
The settings table lives in `pc/seaview.cpp` (`kPar`); `seaview.py` reads it to translate an `.ini`.
