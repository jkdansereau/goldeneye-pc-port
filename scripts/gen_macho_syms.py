#!/usr/bin/env python3
"""Convert ELF/PE GAS absolute symbols to Mach-O spelling.

Darwin needs `.data` (not `.section .data`), `_`-prefixed symbol names, and
`.globl` (not `.global`). The cross-platform source files in port/src/ are left
untouched; the Mach-O spelling is generated into the build tree.

--base is the Apple Silicon extension: PORT_ADDR_BASE, the host address the
whole N64 address space is shifted to (see port/include/port_addr.h). It is
added to every absolute value, so `cfb_16` becomes PORT_ADDR_BASE + 0x70000000.
With --base 0 (Intel) the output is byte-identical to the original transform.
"""

import argparse
import pathlib
import re
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0,
                    help="host address of the N64 address space "
                         "(PORT_ADDR_BASE; default 0)")
    args = ap.parse_args()

    source = pathlib.Path(args.input)
    output = pathlib.Path(args.output)
    lines = []

    for line in source.read_text(encoding="utf-8").splitlines():
        if re.match(r"^\s*\.section\s+\.data\s*$", line):
            continue

        match = re.match(r"^(\s*)\.global\s+([A-Za-z_]\w*)\s*$", line)
        if match:
            lines.append(f"{match.group(1)}.globl _{match.group(2)}")
            continue

        match = re.match(
            r"^(\s*)\.set\s+([A-Za-z_]\w*)(\s*,\s*)(0[xX][0-9A-Fa-f]+|\d+)(.*)$",
            line,
        )
        if match:
            value = int(match.group(4), 0) + args.base
            lines.append(
                f"{match.group(1)}.set _{match.group(2)}{match.group(3)}"
                f"0x{value:X}{match.group(5)}"
            )
            continue

        lines.append(line)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
