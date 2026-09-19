#!/usr/bin/env python3
"""Remove ELF-style weak aliases from a C source copied for Mach-O."""

import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT OUTPUT", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    text = source.read_text(encoding="utf-8")
    text = re.sub(r"^[ \t]*#pragma[ \t]+weak[^\n]*\n", "", text,
                  flags=re.MULTILINE)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
