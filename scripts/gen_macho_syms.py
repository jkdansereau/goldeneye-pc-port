#!/usr/bin/env python3
"""Convert ELF/PE GAS absolute symbols to Mach-O spelling.

Also accepts C-preprocessed input (`cc -E` on a .S, the dram_syms path on
Apple): gcc's `# N "file"` line markers pass through as harmless `#`
comments, and C-style integer suffixes in `.set` expressions are stripped
(`0x70000000UL` -> `0x70000000`) so the Mach-O assembler never has to parse
them."""

import pathlib
import re
import sys


def main() -> int:
    argv = sys.argv[1:]
    base = 0
    if argv and argv[0] == "--base":
        if len(argv) < 2:
            print(f"usage: {sys.argv[0]} [--base N] INPUT OUTPUT", file=sys.stderr)
            return 2
        base = int(argv[1], 0)
        argv = argv[2:]
    if len(argv) != 2:
        print(f"usage: {sys.argv[0]} [--base N] INPUT OUTPUT", file=sys.stderr)
        return 2

    source = pathlib.Path(argv[0])
    output = pathlib.Path(argv[1])
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
            expr = re.sub(
                r"\b(0[xX][0-9a-fA-F]+|\d+)(?:[uU][lL]{1,2}|[uUlL])\b", r"\1", match.group(3)
            )
            if base:
                # Add the window base to the expression's first numeric
                # literal (romassets lines are a single literal; dram lines
                # never take --base — see the docstring).
                expr = re.sub(
                    r"(0[xX][0-9a-fA-F]+|\d+)",
                    lambda m, _b=base: f"0x{(int(m.group(1), 0) + _b):X}",
                    expr,
                    count=1,
                )
            lines.append(
                f"{match.group(1)}.set _{match.group(2)}{expr}"
            )
            continue

        lines.append(line)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
