# D43 helper: decode a GE model file from ROM (N64 layout) and dump its
# node tree + GDL command streams. Usage: python d43_decode.py <name-substr>
import csv, struct, sys, zlib

ROM = r"data/ge007.ntsc-final.z64"
rom = open(ROM, "rb").read()

rows = list(csv.reader(open(r"scripts/filelist.u.csv")))
entries = []
for r in rows:
    if len(r) < 3 or not r[2]:
        continue
    try:
        addr, size = int(r[0]), int(r[1])
    except ValueError:
        continue
    entries.append((addr, size, r[2]))

flt = sys.argv[1] if len(sys.argv) > 1 else ""
want = None
for addr, size, name in sorted(entries):
    base = name.rsplit("/", 1)[-1]
    if base.endswith(".bin"):
        base = base[:-4]
    if flt and flt.lower() in base.lower():
        want = (addr, size, base)
        break
if not want:
    sys.exit("no file matching %r" % flt)

addr, size, base = want
data = rom[addr:addr + size]
out = zlib.decompress(data[2:], -15)
print("%s: rom=%d decomp=%d (0x%X)" % (base, size, len(out), len(out)))

# N64 header-less layout: switches[NS] then textures[NT] then RootNode.
# We don't know NS/NT from the file alone; find them via the exe record is
# not available here, so heuristic: scan for a plausible node tree start.
# Instead: dump raw words and let the eye decode. Print first 64 bytes.
print("head:", out[:64].hex())

def be16(b, o): return struct.unpack_from(">H", b, o)[0]
def be32(b, o): return struct.unpack_from(">I", b, o)[0]

# Try to find GDL streams: sequences of 8-byte words whose w0>>24 is a known cmd.
CMDS = {0xc0:"NOOP",0x05:"VTX",0x0f:"MTX",0x13:"DL",0x15:"MOVEWORD",0x19:"POPMTX",
        0x02:"MATRIXLOAD",0x07:"CLIPRATIO",0x0b:"SPSETOTHERMODE?",0x0d:"ENVMODE?",
        0x23:"RDPPPIPESYNC",0x25:"RDPSETSCISSOR?",0x26:"RDPSETOPTIONS?",0x27:"RDPLOADTLUT?",
        0x28:"RDPSETCONVERT?",0x29:"RDPSETSCENECULL?",0x2a:"RDPFILLRECT",0x2b:"RDPSETFOGCOLOR?",
        0x2c:"RDPSETPRIMCOLOR",0x2d:"RDPSETPROJECT?",0x2e:"RDPSETBADEVENTS?",0x2f:"RDPLOADUTILE?",
        0x31:"RDPSETENVCOLOR",0x32:"RDPSETCOMBINE",0xba:"RDPSETOTHERMODE_H",0xbb:"RDPSETOTHERMODE_L",
        0xbf:"RDPSETTEXIMAGE",0xb1:"RDPSETTILESIZE?",0xbbb:None}
def cmdname(c):
    return CMDS.get(c, "0x%02x" % c)

# scan for the longest run of plausible Gfx words
best = (0, 0, 0)
i = 0
while i + 8 <= len(out):
    w0 = be32(out, i)
    c = w0 >> 24
    if c in CMDS:
        j = i
        while j + 8 <= len(out) and (be32(out, j) >> 24) in CMDS:
            j += 8
        L = j - i
        if L > best[0]:
            best = (L, i, c)
    i += 4

L, off, c0 = best
print("\nlongest GDL-like run: %d bytes at offset 0x%X (starts cmd %s)" % (L, off, cmdname(c0)))
# dump first 24 words of that run
for k in range(min(24, L // 8)):
    w0 = be32(out, off + 8*k)
    w1 = be32(out, off + 8*k + 4)
    print("  [%2d] %s  w0=%08X w1=%08X" % (k, cmdname(w0 >> 24), w0, w1))
