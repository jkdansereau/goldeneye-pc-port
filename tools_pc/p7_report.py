#!/usr/bin/env python3
# P7 (2026-10-04): cross-platform golden table.
# For every level, compare tools_pc/golden/<level>/linux/ against
# tools_pc/golden/<level>/win/ with framediff.py in both modes:
#   --exact --tol 2          -> per-pixel share over tol (the rendering delta)
#   default (structural)     -> worst grid-cell dmean, nonclear delta, phash
# Prints one line per (level, frame) plus a per-level verdict.
#
# Path-agnostic: the repo root is the parent of this script's directory
# (tools_pc/), so it can be invoked from anywhere; no absolute path, no
# '~', no machine default.
import os
import subprocess
import json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FD = os.path.join(ROOT, "tools_pc", "framediff.py")

LEVELS = [("dam","33"),("facility","34"),("runway","35"),("surface1","36"),
          ("bunker1","09"),("silo","20"),("frigate","26"),("surface2","43"),
          ("bunker2","27"),("statue","22"),("archives","24"),("streets","29"),
          ("depot","30"),("train","25"),("jungle","37"),("control","23"),
          ("caverns","39"),("cradle","41"),("aztec","28"),("egypt","32"),
          ("cuba","54")]

def run(args):
    p = subprocess.run(args, capture_output=True, text=True)
    out = p.stdout + p.stderr
    i = out.find("{")
    if i < 0:
        return None, p.returncode, out
    try:
        d, _ = json.JSONDecoder().raw_decode(out[i:])
    except Exception:
        return None, p.returncode, out
    return d, p.returncode, out

rows = []
for name, num in LEVELS:
    lin = os.path.join(ROOT, "tools_pc", "golden", name, "linux")
    win = os.path.join(ROOT, "tools_pc", "golden", name, "win")
    ex, rcx, ox = run(["python3", FD, lin, "--golden", win,
                       "--exact", "--tol", "2", "--json"])
    st, rcg, og = run(["python3", FD, lin, "--golden", win, "--json"])
    if ex is None or st is None:
        rows.append((name,"ERR","framediff failed", rcx, rcg))
        continue
    per = {}
    for r in ex["results"]:
        per.setdefault(r["frame"], {})["exact"] = r
    for r in st["results"]:
        per.setdefault(r["frame"], {})["struct"] = r
    for f in sorted(per):
        e = per[f].get("exact", {}); s = per[f].get("struct", {})
        rows.append((name, f.replace("frame_",""),
                     "%.3f%%" % e.get("diff_pct", 0.0),
                     str(s.get("worst_cell","")), str(s.get("phash_hamming","")),
                     e.get("status",""), s.get("status","")))
    print("%-9s exact rc=%d struct rc=%d" % (name, rcx, rcg))

print()
print("level      frame   over%   worst_cell phash exact struct")
for r in rows:
    if len(r) == 7:
        print("%-10s %-6s %7s %10s %5s %6s %6s" % r[:7])
    else:
        print("%-10s %s" % (r[0], r[1]))
