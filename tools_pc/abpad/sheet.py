import sys
from pathlib import Path
from PIL import Image, ImageDraw

D = Path(sys.argv[1])          # run dir: 1964_<ms>.png + port_<ms>.png (capture.ps1)
t0, t1, step = float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
W, H = 320, 240
rows = []
t = t0
while t <= t1 + 1e-6:
    tag = f"{int(round(t * 1000)):05d}"
    row = Image.new("RGB", (W * 2 + 70, H), "black")
    for i, side in enumerate(("1964", "port")):
        f = D / f"{side}_{tag}.png"
        if f.exists():
            row.paste(Image.open(f).convert("RGB").resize((W, H)), (70 + i * W, 0))
    ImageDraw.Draw(row).text((5, H // 2), f"{t:.1f}s", fill="white")
    rows.append(row)
    t += step
sheet = Image.new("RGB", (W * 2 + 70, H * len(rows)), "black")
for i, r in enumerate(rows):
    sheet.paste(r, (0, i * H))
out = D / f"sheet2_{t0:04.1f}-{t1:04.1f}.png"
sheet.save(out)
print(out)
