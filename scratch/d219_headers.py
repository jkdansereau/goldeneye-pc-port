#!/usr/bin/env python3
"""D219 offline probe: parse the non-zlib/zlib texture bitstream headers
straight out of the ROM images segment, replicating texLoad/texInflateNonZlib
header reads (src/game/image.c) so we can compare TEXCOMPMETHOD/TEXFORMAT for
IMAGE_FIRE_N vs muzzle-flash/ammo/flare/impact siblings WITHOUT running the game.

ROM: data/ge007.ntsc-final.z64
Images segment cart start 0x108F7DF0 (port/src/romassets_u.s) -> file offset 0x8F7DF0.
Offsets are cumulative sizes from assets/images.def (image_entries_load, no align).
"""
import re, sys

SEG = 0x8F7DF0
ROM = open('data/ge007.ntsc-final.z64','rb').read()

# ---- enum name -> index from image_externs.h ----
enummap = {}
_in_enum = False
_v = 0
for line in open('assets/image_externs.h', encoding='utf-8', errors='replace'):
    if 'typedef enum IMAGEIDS' in line:
        _in_enum = True; continue
    if _in_enum and line.strip().startswith('}'):
        break
    m = re.match(r'\s*([A-Za-z_][A-Za-z0-9_]*)\s*,?\s*(//.*)?$', line)
    if m and not line.strip().startswith('//'):
        enummap[m.group(1)] = _v; _v += 1

# ---- parse images.def: field1 is a bare index or an enum name minus IMAGE_ ----
entries = []
for line in open('assets/images.def', encoding='utf-8', errors='replace'):
    m = re.match(r'\s*IMAGE\(\s*([A-Za-z0-9_]+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,', line)
    if not m: continue
    f1, sz = m.group(1), int(m.group(2), 0)
    if f1.isdigit():
        idx = int(f1); name = f"_image{idx}_ID"
    else:
        key = 'IMAGE_' + f1 if not f1.startswith('IMAGE_') else f1
        idx = enummap.get(key)
        name = key
        if idx is None:
            # named in images.def but left _imageN_ID in the decomp enum --
            # trust position
            idx = len(entries)
            print(f"# note: {key} not in enum, using positional index {idx}", file=sys.stderr)
    assert idx == len(entries), f"images.def out of order at {name} ({idx} vs {len(entries)})"
    entries.append((name, sz))
print(f"# {len(entries)} image entries", file=sys.stderr)

# cumulative offsets (image_entries_load)
offs = []
o = 0
for name, sz in entries:
    offs.append(o)
    o += sz
total = o
print(f"# images segment total size per g_Textures: {total:#x} (segment room to 0xC00000-0x8F7DF0 = {0xC00000-SEG:#x})", file=sys.stderr)

class Bits:
    def __init__(self, buf, pos):
        self.buf = buf; self.pos = pos; self.bit = 0
    def read(self, n):
        v = 0
        for _ in range(n):
            byte = self.buf[self.pos]
            v = (v << 1) | ((byte >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit = 0; self.pos += 1
        return v

NAMES = {n:i for i,(n,_) in enumerate(entries)}

def dump(i, name):
    off = offs[i]
    base = SEG + off
    b0 = ROM[base]
    u, z, lod = (b0>>7)&1, (b0>>6)&1, b0 & 0x3f
    out = [f"texnum={i:4d} {name:28s} off={off:#08x} byte0={b0:#04x} u={u} z={z} lod={lod}"]
    if z:
        # zlib path: format(8), numcolours(8)+1, palette... then per-image w/h
        b = Bits(ROM, base+1)
        fmt = b.read(8); nc = b.read(8)+1
        pal = [b.read(16) for _ in range(nc)]
        out.append(f"  ZLIB format={fmt} numcolours={nc} pal0={pal[0]:#05x}")
        w = b.read(8); h = b.read(8)
        out.append(f"  image0 {w}x{h} (zlib payload follows -- indices only)")
    else:
        b = Bits(ROM, base+1)
        fmt = b.read(4); w = b.read(8); h = b.read(8); cm = b.read(4)
        out.append(f"  NONZLIB format={fmt} width={w} height={h} compmethod={cm}")
    return ' '.join(out)

FMT = {0:'RGBA32',1:'RGBA16',2:'RGB24',3:'RGB15',4:'IA16',5:'IA8',6:'IA4',7:'I8',8:'I4',9:'RGBA16_CI8',10:'RGBA16_CI4',11:'IA16_CI8',12:'IA16_CI4'}
CM  = {0:'UNCOMPRESSED0',1:'UNCOMPRESSED1',2:'HUFFMAN',3:'HUFFMANPERHCHANNEL',4:'RLE',5:'LOOKUP',6:'HUFFMANLOOKUP',7:'RLELOOKUP',8:'HUFFMANBLUR',9:'RLEBLUR'}

def dump2(i, name):
    off = offs[i]
    base = SEG + off
    b0 = ROM[base]
    z = (b0>>6)&1; lod = b0 & 0x3f
    line = f"texnum={i:4d} {name:28s} off={off:#08x} byte0={b0:#04x} z={z} lod={lod}"
    if z:
        b = Bits(ROM, base+1)
        fmt = b.read(8); nc = b.read(8)+1
        pal = [b.read(16) for _ in range(nc)]
        w = b.read(8); h = b.read(8)
        line += f"  ZLIB fmt={FMT.get(fmt,fmt)}({fmt}) ncols={nc} pal0={pal[0]:#05x} img0={w}x{h}"
    else:
        b = Bits(ROM, base+1)
        fmt = b.read(4); w = b.read(8); h = b.read(8); cm = b.read(4)
        line += f"  NONZLIB fmt={FMT.get(fmt,fmt)}({fmt}) {w}x{h} comp={CM.get(cm,cm)}({cm})"
    return line

targets = []
# fire + smoke particle range
for i in range(2084, 2116):
    targets.append(i)
# flares
for n in ['IMAGE_FLAREORANGELINE','IMAGE_FLAREBLUEROUND','IMAGE_FLAREBLUELINE','IMAGE_FLAREWHITEROUND']:
    targets.append(NAMES[n])
# ammo HUD icons
for n in ['IMAGE_ROCKETAMMO','IMAGE_PLAINMINEAMMO','IMAGE_GRENADEAMMO','IMAGE_MAGAMMO','IMAGE_GLAMMO','IMAGE_KNIFEAMMO','IMAGE_SHOTAMMO']:
    targets.append(NAMES[n])
# impact sprites
for n in ['IMAGE_IMPACT1','IMAGE_IMPACT2','IMAGE_IMPACT3','IMAGE_IMPACT4','IMAGE_IMPACTMULTI','IMAGE_IMPACTREDBRICK2','IMAGE_IMPACTBRICK2','IMAGE_IMPACTBRICK3']:
    targets.append(NAMES[n])
# smoke sprites
for n in ['IMAGE_SMOKE1','IMAGE_SMOKE2','IMAGE_SMOKEBALLS1']:
    targets.append(NAMES[n])

seen = set()
for i in targets:
    if i in seen or i >= len(entries): continue
    seen.add(i)
    print(dump2(i, entries[i][0]))
