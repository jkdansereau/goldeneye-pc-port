# Social preview card

`social-preview.png` is the image GitHub shows when the repo is linked on
Slack, Discord, Reddit, X, etc. (Repo **Settings → General → Social preview**.)
It is generated from `social-preview.html` — the two live side by side so the
card stays editable.

- **Size:** 1280×640 (GitHub's recommended ratio).
- **Fonts:** Tinos (a Times metric-compatible serif) for the title, Arimo
  (Arial metric-compatible) for the body — both from Google Fonts.
- **Everything else is pure CSS** — no raster assets, no external images.

## Design brief

Recreate GoldenEye 007's in-game **mission-briefing / objectives screen** and
repurpose it as a project status card:

- a mottled grey-green **concrete/stone** surround (three layered SVG
  `feTurbulence` fractal-noise fills — broad staining, mid mottle, fine tooth —
  plus a soft edge vignette);
- a cream **parchment card** with a faint wavy watermark and a drop shadow;
- the game's right-edge **filing tabs** (`NEXT` / `PREVIOUS`) and the small red
  **crosshair/reticle**;
- a faint circular **emblem** watermark, upper right;
- a translucent diagonal **`CLASSIFIED`** stamp, kept low-opacity so the status
  text stays readable;
- serif title, `007` and `PC PORT` in the GoldenEye menu **red** (`#a01818`);
- body laid out as the briefing's `Mission:` / `REPORT:` / `Status:` /
  `Statistics:` fields.

Keep the copy short. Update the stat lines from the README's "By the numbers"
table when the numbers move; update `Status:` when the phase changes.

## Regenerate the PNG

Any Chromium (Chrome, Edge, Brave, `chromium`) can render it headless:

```sh
cd .github
chrome --headless --disable-gpu --hide-scrollbars \
       --force-device-scale-factor=1 --virtual-time-budget=6000 \
       --window-size=1280,640 \
       --screenshot="$(pwd)/social-preview.png" \
       "$(pwd)/social-preview.html"
```

`--virtual-time-budget=6000` gives the web fonts time to load.
`--force-device-scale-factor=1` keeps the output exactly 1280×640.

If your browser blocks web-font loads over `file://`, serve the folder first:

```sh
cd .github && python -m http.server 8000 &
chrome --headless --disable-gpu --hide-scrollbars \
       --force-device-scale-factor=1 --virtual-time-budget=6000 \
       --window-size=1280,640 \
       --screenshot="$(pwd)/social-preview.png" \
       "http://127.0.0.1:8000/social-preview.html"
```

Then re-upload `social-preview.png` under Settings → General → Social preview
(GitHub does not pick it up from the repo automatically).
