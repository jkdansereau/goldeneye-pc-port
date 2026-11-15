#!/usr/bin/env python3
"""Merge combiner ids from game logs into port/switch/shaders/combiners.txt (D973).

Usage: python -I tools_pc/cc_census_merge.py <combiners.txt> <log> [<log> ...]

Reads the `CCID <id0 hex16> <id1 hex8>` lines a run prints with GE_CCLOG=1
(port/fast3d/gfx_frame.cpp) and the `DKSH miss <id0> <id1>` lines the deko3d
backend prints for a combiner missing from the shipped shader pack
(port/fast3d/gfx_deko3d.cpp). The list keeps one `0x<16 hex> 0x<8 hex>` pair per
line, sorted and unique; `#` comment lines at the top of the file are kept.
Prints how many pairs were added.
"""
import re
import sys

PAT = re.compile(r"(?:CCID|DKSH miss)\s+(?:0x)?([0-9a-fA-F]{1,16})\s+(?:0x)?([0-9a-fA-F]{1,8})\b")
LINE = re.compile(r"^\s*0x([0-9a-fA-F]{1,16})\s+0x([0-9a-fA-F]{1,8})\s*(?:#.*)?$")


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = argv[1]
    header = []
    pairs = set()
    try:
        with open(path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.rstrip("\n")
                m = LINE.match(line)
                if m:
                    pairs.add((int(m.group(1), 16), int(m.group(2), 16)))
                elif line.strip().startswith("#") or not line.strip():
                    if not pairs:
                        header.append(line)
                else:
                    print("warning: %s: ignoring line %r" % (path, line), file=sys.stderr)
    except FileNotFoundError:
        header = ["# deko3d shader pack: combiner ids (D973). One pair per line: 0x<id0> 0x<id1>.",
                  "# Merged from GE_CCLOG=1 / DKSH miss log lines by tools_pc/cc_census_merge.py."]
    before = len(pairs)
    for log in argv[2:]:
        with open(log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                for m in PAT.finditer(line):
                    pairs.add((int(m.group(1), 16), int(m.group(2), 16)))
    while header and not header[-1].strip():
        header.pop()
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for h in header:
            f.write(h + "\n")
        for id0, id1 in sorted(pairs):
            f.write("0x%016x 0x%08x\n" % (id0, id1))
    print("%s: %d pairs (+%d)" % (path, len(pairs), len(pairs) - before))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
