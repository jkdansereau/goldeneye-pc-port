#!/usr/bin/env python3
"""Report the context cost of the docs that every agent session loads.

Usage:
    python tools_pc/docs_budget.py            # table of per-file budget rows + tier-1 total
    python tools_pc/docs_budget.py --json     # same data as JSON
    python tools_pc/docs_budget.py --toc FILE # markdown TOC (## / ### headings) for FILE

For each tracked doc it prints path, bytes, lines, approx_tokens (bytes // 4),
the per-file budget in approx tokens, and OK/OVER. Missing files are reported
as "missing" and are not an error (docs/HANDOFF.md is optional). It then sums
the "tier 1" set (CLAUDE.md, AGENTS.md, docs/HANDOFF.md, docs/ROADMAP.md)
against TIER1_BUDGET. Exit code is 1 if any file or the tier-1 total is OVER,
else 0.
"""

import argparse
import json
import os
import re
import sys

# Approximate-token budgets for each tracked doc.
BUDGETS = {
    "CLAUDE.md": 4000,
    "AGENTS.md": 6000,
    "docs/HANDOFF.md": 3000,
    "docs/ROADMAP.md": 12000,
    "docs/porting-notes.md": 60000,
    "docs/dev/findings.md": 650000,
    "docs/dev/findings-index.csv": 40000,
}

# Files whose approx tokens are summed for the tier-1 total.
TIER1_FILES = ["CLAUDE.md", "AGENTS.md", "docs/HANDOFF.md", "docs/ROADMAP.md"]
TIER1_BUDGET = 25000


def repo_root():
    """Repo root = parent dir of the script's directory."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_bytes(path):
    """Return (bytes, lines) for a file read as UTF-8 with errors='replace',
    or None if the file is missing."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    return len(text.encode("utf-8")), len(text.splitlines())


def collect_rows():
    """Build the list of row dicts for every tracked doc."""
    root = repo_root()
    rows = []
    for rel in BUDGETS:
        full = os.path.join(root, *rel.split("/"))
        data = read_bytes(full)
        if data is None:
            rows.append({
                "path": rel,
                "bytes": None,
                "lines": None,
                "approx_tokens": None,
                "budget_tokens": BUDGETS[rel],
                "status": "missing",
            })
        else:
            nbytes, nlines = data
            tokens = nbytes // 4
            budget = BUDGETS[rel]
            rows.append({
                "path": rel,
                "bytes": nbytes,
                "lines": nlines,
                "approx_tokens": tokens,
                "budget_tokens": budget,
                "status": "OK" if tokens <= budget else "OVER",
            })
    return rows


def tier1_total(rows):
    total = 0
    for row in rows:
        if row["path"] in TIER1_FILES and row["approx_tokens"] is not None:
            total += row["approx_tokens"]
    return total


def print_table(rows, t1):
    header = f"{'path':<28} {'bytes':>10} {'lines':>8} {'tokens':>9} {'budget':>9}  status"
    print(header)
    for row in rows:
        if row["status"] == "missing":
            print(f"{row['path']:<28} missing")
        else:
            print(
                f"{row['path']:<28} {row['bytes']:>10} {row['lines']:>8} "
                f"{row['approx_tokens']:>9} {row['budget_tokens']:>9}  {row['status']}"
            )
    t1_status = "OK" if t1 <= TIER1_BUDGET else "OVER"
    print(f"{'TIER1 TOTAL':<28} {'':>10} {'':>8} {t1:>9} {TIER1_BUDGET:>9}  {t1_status}")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Report the context cost of agent-session docs and fail when over budget."
    )
    parser.add_argument("--toc", metavar="FILE", help="print a markdown TOC for FILE")
    parser.add_argument("--json", action="store_true", help="print JSON instead of the table")
    args = parser.parse_args(argv)

    if args.toc:
        return print_toc(args.toc)

    rows = collect_rows()
    t1 = tier1_total(rows)

    if args.json:
        payload = {
            "rows": rows,
            "tier1": {"total_tokens": t1, "budget_tokens": TIER1_BUDGET,
                      "status": "OK" if t1 <= TIER1_BUDGET else "OVER"},
        }
        print(json.dumps(payload, indent=2))
    else:
        print_table(rows, t1)

    over = any(r["status"] == "OVER" for r in rows) or t1 > TIER1_BUDGET
    return 1 if over else 0


def github_anchor(heading):
    """GitHub-style anchor: lowercase, strip non-alnum/space/hyphen, spaces -> hyphens."""
    text = heading.lower()
    text = re.sub(r"[^a-z0-9 \-]", "", text)
    return text.replace(" ", "-")


def print_toc(path):
    """Print a markdown TOC of '## ' / '### ' headings, skipping fenced code blocks."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        return 1

    in_fence = False
    for line in lines:
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if line.startswith("### "):
            heading = line[4:].strip()
            print(f"  - [{heading}](#{github_anchor(heading)})")
        elif line.startswith("## "):
            heading = line[3:].strip()
            print(f"- [{heading}](#{github_anchor(heading)})")
    return 0


if __name__ == "__main__":
    # Headings contain non-ASCII (arrows, em dashes); a cp1252 console can't print them.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
