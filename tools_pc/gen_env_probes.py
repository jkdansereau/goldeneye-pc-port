#!/usr/bin/env python3
"""Drift check for docs/dev/GE-ENV-PROBES.md (speed-ups plan Step 4 / R9).

That doc is a hand-curated table of every `GE_*` env probe -- var, file:line,
what it does, live/dead. It was last snapshotted by a manual grep and drifts
as probes are added/removed. This script does NOT regenerate the prose (the
curation is the value); it re-greps the live `getenv("GE_...")` sites and
reports:
  * NEW  -- a var with live sites that the doc never names
  * GONE -- a var the doc names that has no live site any more
  * a fresh var -> file:line site map (paste into the doc's File:line cells)

Usage:
    python tools_pc/gen_env_probes.py            # print the drift report
    python tools_pc/gen_env_probes.py --check     # exit 1 if NEW or GONE
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "dev" / "GE-ENV-PROBES.md"
SCAN_DIRS = ["src", "port"]
GETENV_RE = re.compile(r'getenv\(\s*"(GE_[A-Z0-9_]+)"\s*\)')


def live_sites() -> dict[str, list[str]]:
    sites: dict[str, list[str]] = {}
    for d in SCAN_DIRS:
        for path in sorted((ROOT / d).rglob("*")):
            if path.suffix not in {".c", ".cpp", ".h", ".def", ".inc"}:
                continue
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                continue
            for n, line in enumerate(lines, 1):
                for m in GETENV_RE.finditer(line):
                    rel = path.relative_to(ROOT).as_posix()
                    sites.setdefault(m.group(1), []).append(f"{rel}:{n}")
    return sites


def doc_vars() -> tuple[set[str], set[str]]:
    """Return (all vars named in the doc, vars whose row is an intentional tombstone)."""
    if not DOC.exists():
        return set(), set()
    named, tombstone = set(), set()
    for line in DOC.read_text(encoding="utf-8").splitlines():
        vs = re.findall(r"GE_[A-Z0-9_]+", line)
        named.update(vs)
        if "tombstone" in line.lower():
            tombstone.update(vs)
    return named, tombstone


def main() -> int:
    check = "--check" in sys.argv[1:]
    sites = live_sites()
    documented, tombstoned = doc_vars()
    live = set(sites)

    new = sorted(live - documented)
    gone = sorted(v for v in documented - live if v not in ({"GE_DETERM"} | tombstoned))

    out = []
    out.append(f"# GE_* probe drift vs {DOC.relative_to(ROOT).as_posix()}")
    out.append(f"# {len(live)} vars with live sites, {len(documented)} named in the doc\n")
    if new:
        out.append("## NEW -- live sites, not in the doc (add a row):")
        for v in new:
            out.append(f"  {v}")
            for s in sites[v]:
                out.append(f"      {s}")
    if gone:
        out.append("\n## GONE -- named in the doc, no live site (mark removed):")
        out += [f"  {v}" for v in gone]
    if not new and not gone:
        out.append("doc is in sync with live getenv sites (site line numbers still drift -- see map below)")

    out.append("\n## full live site map:")
    for v in sorted(sites):
        out.append(f"  {v}: " + ", ".join(sites[v]))

    print("\n".join(out))
    if check and (new or gone):
        sys.stderr.write(f"\nGE-ENV-PROBES.md drift: {len(new)} new, {len(gone)} gone\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
