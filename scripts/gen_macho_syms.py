#!/usr/bin/env python3
"""Convert ELF/PE GAS absolute symbols to Mach-O spelling."""

import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT OUTPUT", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    lines = []

    for line in source.read_text(encoding="utf-8").splitlines():
        if re.match(r"^\s*\.section\s+\.data\s*$", line):
            continue

        match = re.match(r"^(\s*)\.global\s+([A-Za-z_]\w*)\s*$", line)
        if match:
            lines.append(f"{match.group(1)}.globl _{match.group(2)}")
            continue

        match = re.match(
            r"^(\s*)\.set\s+([A-Za-z_]\w*)(\s*,.*)$", line
        )
        if match:
            lines.append(
                f"{match.group(1)}.set _{match.group(2)}{match.group(3)}"
            )
            continue

        lines.append(line)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
