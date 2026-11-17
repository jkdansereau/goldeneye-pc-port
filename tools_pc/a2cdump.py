#!/usr/bin/env python3
"""Convert GE_A2CDUMP texture dumps to PNG and report their alpha spread (D984).

A run with GE_A2CDUMP=1 writes every texture the alpha-to-coverage test turns
down (a blended surface left on the N64 blend) to $S/a2cdump/ as a 32-bit TGA:
<raw address>_<reason>_f<fmt>-s<siz>_<w>x<h>.tga (reason: fmt = texture format
can hold a smooth alpha, fade = a prim / env alpha below 255).

For each one this writes a PNG next to it (or into --out) and prints how many
texels sit at alpha 0, at 255, and in between. A texture with few in-between
texels is a cut-out stored in a format that could carry a ramp (foliage that
should get A2C); a large in-between share is a real ramp (smoke, glow).

The dumps come from the game ROM: keep them out of the repository.

--sheet <file.png> also writes every texture into one grid image, over a
checkerboard so the alpha shows, in the order of the printed table (# column).

Usage: python tools_pc/a2cdump.py <a2cdump dir> [--out <dir>] [--mid-max 5]
                                   [--sheet <file.png>] [--only ramp|cut-out]
"""
import argparse
import os
import struct
import sys
import zlib

FMT_NAMES = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
SIZ_NAMES = {0: "4b", 1: "8b", 2: "16b", 3: "32b"}


def read_tga(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 18:
        raise ValueError("short file")
    idlen, cmaptype, imgtype = data[0], data[1], data[2]
    w, h, bpp, desc = struct.unpack_from("<HHBB", data, 12)
    if cmaptype != 0 or imgtype != 2 or bpp != 32:
        raise ValueError("not an uncompressed 32-bit TGA")
    off = 18 + idlen
    px = data[off:off + w * h * 4]
    if len(px) < w * h * 4:
        raise ValueError("truncated pixels")
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    if not desc & 0x20:  # bottom-left origin
        rows.reverse()
    rgba = bytearray()
    for row in rows:
        for x in range(0, len(row), 4):
            b, g, r, a = row[x:x + 4]
            rgba += bytes((r, g, b, a))
    return w, h, bytes(rgba)


def write_png(path, w, h, rgba):
    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def contact_sheet(path, items, cell=128, cols=8):
    """items: (w, h, rgba) tuples; nearest-scaled into cell x cell tiles over a checkerboard."""
    pad = 4
    step = cell + pad
    rows = (len(items) + cols - 1) // cols
    W, H = cols * step + pad, rows * step + pad
    img = bytearray(bytes((0x30, 0x30, 0x30, 0xFF)) * (W * H))
    for n, (w, h, rgba) in enumerate(items):
        scale = max(1, min(cell // max(w, 1), cell // max(h, 1)))
        if w * scale > cell or h * scale > cell:
            scale = 1
        sw, sh = min(w * scale, cell), min(h * scale, cell)
        ox = pad + (n % cols) * step
        oy = pad + (n // cols) * step
        for y in range(sh):
            sy = min(y * h // sh, h - 1)
            for x in range(sw):
                sx = min(x * w // sw, w - 1)
                r, g, b, a = rgba[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
                bg = 200 if ((x // 8) + (y // 8)) & 1 else 120
                o = ((oy + y) * W + ox + x) * 4
                img[o] = (r * a + bg * (255 - a)) // 255
                img[o + 1] = (g * a + bg * (255 - a)) // 255
                img[o + 2] = (b * a + bg * (255 - a)) // 255
    write_png(path, W, H, bytes(img))


def describe(name):
    """'0010aac0_fmt_f4-s1_64x32' -> 'fmt I8 64x32' (best effort)."""
    parts = name.split("_")
    if len(parts) < 4:
        return name
    reason, fs, size = parts[1], parts[2], parts[3]
    try:
        fmt, siz = fs[1:].split("-s")
        fs = FMT_NAMES.get(int(fmt), fmt) + SIZ_NAMES.get(int(siz), siz)
    except ValueError:
        pass
    return f"{reason:4} {fs:7} {size}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", help="the a2cdump directory copied from $S (SD card or Ryujinx sdcard)")
    ap.add_argument("--out", help="where to write the PNGs (default: next to the TGAs)")
    ap.add_argument("--mid-max", type=float, default=5.0,
                    help="max %% of in-between alpha texels to call a texture a cut-out (default 5)")
    ap.add_argument("--sheet", help="also write all textures into this one PNG grid")
    ap.add_argument("--only", choices=("ramp", "cut-out"), help="list (and sheet) only this verdict")
    args = ap.parse_args()

    if not os.path.isdir(args.dir):
        sys.exit(f"not a directory: {args.dir}")
    out = args.out or args.dir
    os.makedirs(out, exist_ok=True)

    names = sorted(n for n in os.listdir(args.dir) if n.lower().endswith(".tga"))
    if not names:
        sys.exit(f"no .tga in {args.dir}")
    sheet = []
    print(f"{'#':>3} {'texture':40} {'kind':22} {'a=0':>6} {'a=255':>6} {'mid':>6} {'levels':>6}  verdict")
    for n in names:
        try:
            w, h, rgba = read_tga(os.path.join(args.dir, n))
        except (OSError, ValueError) as e:
            print(f"{'':3} {n:40} skipped: {e}")
            continue
        base = os.path.splitext(n)[0]
        alphas = rgba[3::4]
        total = len(alphas) or 1
        zero = alphas.count(0)
        full = alphas.count(255)
        mid = total - zero - full
        levels = len(set(alphas))
        mid_pct = 100.0 * mid / total
        verdict = "cut-out" if mid_pct <= args.mid_max else "ramp"
        if args.only and verdict != args.only:
            continue
        write_png(os.path.join(out, base + ".png"), w, h, rgba)
        sheet.append((w, h, rgba))
        print(f"{len(sheet) - 1:3} {base:40} {describe(base):22} {100.0 * zero / total:5.1f}% {100.0 * full / total:5.1f}% "
              f"{mid_pct:5.1f}% {levels:6}  {verdict}")
    print(f"\nPNGs in {out}")
    if args.sheet and sheet:
        contact_sheet(args.sheet, sheet)
        print(f"contact sheet: {args.sheet}")


if __name__ == "__main__":
    main()
