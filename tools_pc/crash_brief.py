#!/usr/bin/env python3
"""crash_brief.py — crash log -> pre-filled investigation brief (speed-ups
plan Step 9 / R5).

Symbolicates every faulting PC in a ge007.crash.log with addr2line, then
greps docs/porting-notes.md and docs/dev/findings-index.csv (Step 4/R8) for
sections that already cover that function/file, and checks a small known
parked-crash signature list (Cuba credits, D188 SysV va_list, D189
stack-protector) so agents stop re-investigating crashes that are already
understood-but-not-worth-fixing. Prints a dev-process.md-shaped brief ready
to hand to a subagent, filling in what's derivable and leaving the rest as
placeholders.

Usage:
  crash_brief.py [--crash-log PATH] [--exe PATH] [--platform win|linux]
                 [--level NAME] [--json]

Defaults: --crash-log ge007.crash.log (repo root), --exe autodetected from
--platform (build-pc/ge007.x86_64.exe or build-linux/ge007.x86_64).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

_ADDR2LINE = None


def find_addr2line():
    """shutil.which() alone is not enough here: this repo's PATH convention
    for the win toolchain is the MSYS2 POSIX form (/c/msys64/mingw64/bin),
    which a native Windows Python (`python`, as opposed to an MSYS-flavored
    `python3`) cannot resolve -- os.path.isfile("/c/...") is not a valid
    Windows path, so the PATH search silently finds nothing and every
    address comes back unresolved. Fall back to the documented install
    location before giving up."""
    global _ADDR2LINE
    if _ADDR2LINE:
        return _ADDR2LINE
    p = shutil.which("addr2line")
    if not p:
        for c in (r"C:\msys64\mingw64\bin\addr2line.exe",
                  r"C:\msys64\usr\bin\addr2line.exe",
                  "/usr/bin/addr2line"):
            if os.path.exists(c):
                p = c
                break
    _ADDR2LINE = p or "addr2line"
    return _ADDR2LINE

# --------------------------------------------------------------------------
# Known parked-crash signatures. Each entry: (regex over the raw crash-log
# text or a resolved symbol, label, one-line note). Matching one of these
# means "this is understood, don't re-derive it — see the label".
KNOWN_SIGNATURES = [
    (re.compile(r"credits", re.I), "D129/D76",
     "Cuba end-credits crash -- parked, not a fresh bug."),
    (re.compile(r"_Printf|_Putfld|va_list", re.I), "D188 (porting-notes D7)",
     "va_list-by-value ABI misuse on x86-64 SysV -- thread a va_list* instead."),
    (re.compile(r"tileStack|pointbuf|stack.protector|stack.smash", re.I),
     "D189 (porting-notes D8)",
     "latent fixed-size stack-buffer over/under-run, fatal only under "
     "-fstack-protector-strong (Linux/Ubuntu default)."),
]

# Known parked crashes by resolved symbol -- a regex over the crash-log text
# alone misses this class: the tell (a local var like tileStack[39]) lives
# inside the function body, not its symbol name, so the KNOWN_SIGNATURES
# text-regex never fires even though the crash *is* the named finding.
FUNC_LABELS = {
    "sub_GAME_7F0B1DDC": ("D189 (porting-notes D8)",
        "stan.c tileStack[39] overrun -- the exact D189 repro function."),
    "add_ptr_to_objective": ("D190",
        "objective.c:64 propDef-stride family, Linux -level_45 SIGSEGV at load."),
}

BACKTRACE_LINE = re.compile(r"^#\d+:\s*(0x[0-9a-fA-F]+)")
PC_LINE = re.compile(r"^PC:\s*(0x[0-9a-fA-F]+)")


def find_addresses(text, max_frames=8):
    """Pull the crash PC + up to max_frames backtrace addresses, in order,
    deduplicated. Deliberately generic across the Windows (EBP-chain) and
    Linux (backtrace_symbols) crash.c formats -- both print '#NN: 0xADDR...'
    backtrace lines and a leading 'PC: 0xADDR' line (see port/src/crash.c)."""
    addrs = []
    for line in text.splitlines():
        m = PC_LINE.match(line) or BACKTRACE_LINE.match(line)
        if m and m.group(1) not in addrs:
            addrs.append(m.group(1))
        if len(addrs) >= max_frames:
            break
    return addrs


def symbolicate(exe, addr):
    """addr2line -f -C -> (function, file:line), or (None, None) if it
    can't resolve (stripped exe, address outside any module, ...)."""
    try:
        out = subprocess.run([find_addr2line(), "-e", exe, "-f", "-C", addr],
                              capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None, None
    lines = out.stdout.strip().splitlines()
    if len(lines) < 2:
        return None, None
    func, loc = lines[0], lines[1]
    if func in ("??", "") or loc.startswith("??"):
        return (func if func not in ("??", "") else None,
                None if loc.startswith("??") else loc)
    return func, loc


def load_porting_notes_sections():
    """[(header_text, anchor, body_text), ...] from the '## ' sections."""
    path = os.path.join(ROOT, "docs", "porting-notes.md")
    if not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8").read()
    parts = re.split(r"^(## .+)$", text, flags=re.M)
    sections = []
    for i in range(1, len(parts), 2):
        header = parts[i][3:].strip()
        body = parts[i + 1] if i + 1 < len(parts) else ""
        sections.append((header, body))
    return sections


def load_findings_index():
    path = os.path.join(ROOT, "docs", "dev", "findings-index.csv")
    rows = []
    if not os.path.exists(path):
        return rows
    import csv
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(l for l in f if not l.startswith("#")):
            rows.append(row)
    return rows


def keyword_hits(func, fileloc, sections, findings, cap=6):
    """Grep porting-notes sections + findings-index one-liners for the
    resolved function name / file basename.

    Word-boundary match, not substring: a short file basename like "stan"
    (stan.c) is a substring of "distance"/"instant"/"understand" and a naive
    `needle in haystack` test matched nearly every finding in the log --
    caught live testing this script. Also drops needles under 5 chars (too
    common to be distinctive) and caps the result so a hit list is a lead,
    not the whole findings log pasted into the brief."""
    needles = []
    if func:
        # split CamelCase / snake_case into tokens; keep the full name too
        needles += re.split(r"[_A-Z]", func)
        needles.append(func)
    if fileloc:
        # rsplit, not split: a Windows debug path is "C:/.../stan.c:2163" --
        # a plain split(":")[0] grabs the drive letter "C", not the path
        # (caught live-testing this script).
        base = os.path.basename(fileloc.rsplit(":", 1)[0])
        needles.append(os.path.splitext(base)[0])
    needles = sorted({n.lower() for n in needles if len(n) >= 5})
    patterns = [re.compile(r"\b%s\b" % re.escape(n)) for n in needles]
    if not patterns:
        return [], []

    porting_hits, finding_hits = [], []
    for header, body in sections:
        blob = (header + " " + body).lower()
        if any(p.search(blob) for p in patterns):
            porting_hits.append(header)
    for row in findings:
        blob = (row.get("one_liner", "") + " " + row.get("label", "")).lower()
        if any(p.search(blob) for p in patterns):
            finding_hits.append("%s (%s)" % (row["label"], row["one_liner"]))
    return porting_hits[:cap], finding_hits[:cap]


def check_known_signatures(crash_text, frames):
    hits = []
    for f in frames:
        func = f.get("func")
        if func in FUNC_LABELS:
            hits.append(FUNC_LABELS[func])
    haystack = crash_text + " " + " ".join(f.get("func") or "" for f in frames)
    for pattern, label, note in KNOWN_SIGNATURES:
        if pattern.search(haystack):
            hits.append((label, note))
    # dedupe, keep order
    seen = set()
    out = []
    for h in hits:
        if h not in seen:
            seen.add(h)
            out.append(h)
    return out


def render_brief(args, frames, known_hits, porting_hits, finding_hits):
    top = frames[0] if frames else {}
    task_sym = top.get("func") or "(unresolved)"
    task_loc = top.get("loc") or top.get("addr", "?")

    lines = []
    lines.append("TASK: crash in %s (%s)%s."
                  % (task_sym, task_loc,
                     "  level=%s" % args.level if args.level else ""))
    read_first = ["docs/porting-notes.md"]
    if porting_hits:
        read_first.append("§" + ", §".join(dict.fromkeys(porting_hits)))
    if finding_hits:
        read_first.append("docs/dev/findings.md: " +
                           "; ".join(dict.fromkeys(finding_hits)))
    lines.append("READ FIRST: " + "; ".join(read_first) + ".")
    lines.append("FILES YOU MAY TOUCH: <fill in -- likely %s; disjoint from "
                 "any other in-flight work>."
                 % (os.path.basename(task_loc.rsplit(":", 1)[0])
                    if task_loc != "?" else "<unknown -- addr2line couldn't "
                    "resolve the crash PC; is the exe built with -g?>"))
    if known_hits:
        lines.append("KNOWN-GOOD / RULED OUT: matches a PARKED signature -- "
                      + "; ".join("%s: %s" % (l, n) for l, n in known_hits)
                      + "  -- confirm it's the same instance before spending "
                        "a budget re-deriving a known crash.")
    else:
        lines.append("KNOWN-GOOD / RULED OUT: <no known-signature match -- "
                      "novel crash, no shortcut>.")
    lines.append("PRE-FLIGHT ATTACHED: %s (%d frame%s symbolicated)."
                  % (args.crash_log, len(frames), "" if len(frames) == 1 else "s"))
    for i, f in enumerate(frames):
        lines.append("  #%02d %s  %s  %s" % (i, f["addr"], f.get("func") or "??",
                                              f.get("loc") or "??"))
    lines.append("BUDGET: <fill in -- tight for a cheap-class bug, more room "
                 "for a structural one>. On expiry: revert probes, write up "
                 "with confidence.")
    lines.append("CONSTRAINTS: no game-logic changes; ABI/layout/format only; "
                 "#ifdef PORT; documented in the finding log.")
    verify = ("tools_pc/verify.sh %s" % args.level) if args.level else \
             "tools_pc/verify.sh <level> (fill in which level reproduces this)"
    lines.append("VERIFY: %s." % verify)
    lines.append("REPORT: (a) root cause + file:line evidence  (b) fix diff, "
                 "or why not  (c) probes left in tree  (d) confidence.\n"
                 "        Append any generalisable quirk to "
                 "docs/porting-notes.md.")
    return "\n".join(lines)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--crash-log", default=os.path.join(ROOT, "ge007.crash.log"))
    ap.add_argument("--exe")
    ap.add_argument("--platform", choices=["win", "linux"])
    ap.add_argument("--level", help="level name/number this crash reproduces on "
                                     "(fills VERIFY:)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv[1:])

    # Content pulled in from findings.md/porting-notes.md (arrows, em-dashes,
    # etc.) can contain non-cp1252 characters; native Windows Python's stdout
    # defaults to the console codepage, not UTF-8, and raises on them.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    if not os.path.exists(args.crash_log):
        print("error: no crash log at %s" % args.crash_log, file=sys.stderr)
        return 2

    platform = args.platform or ("linux" if sys.platform.startswith("linux") else "win")
    exe = args.exe or (os.path.join(ROOT, "build-linux", "ge007.x86_64")
                        if platform == "linux"
                        else os.path.join(ROOT, "build-pc", "ge007.x86_64.exe"))
    if not os.path.exists(exe):
        print("error: exe not found at %s (pass --exe)" % exe, file=sys.stderr)
        return 2

    crash_text = open(args.crash_log, encoding="utf-8", errors="replace").read()
    addrs = find_addresses(crash_text)
    if not addrs:
        print("error: no PC/backtrace addresses found in %s" % args.crash_log,
              file=sys.stderr)
        return 2

    frames = []
    for addr in addrs:
        func, loc = symbolicate(exe, addr)
        frames.append({"addr": addr, "func": func, "loc": loc})

    sections = load_porting_notes_sections()
    findings = load_findings_index()
    porting_hits, finding_hits = [], []
    for f in frames:
        p, fi = keyword_hits(f.get("func"), f.get("loc"), sections, findings)
        porting_hits += p
        finding_hits += fi
    porting_hits = list(dict.fromkeys(porting_hits))[:6]
    finding_hits = list(dict.fromkeys(finding_hits))[:6]
    known_hits = check_known_signatures(crash_text, frames)

    brief = render_brief(args, frames, known_hits, porting_hits, finding_hits)
    print(brief)

    if args.json:
        print(json.dumps({
            "crash_log": args.crash_log, "exe": exe, "platform": platform,
            "frames": frames,
            "known_signature_matches": [{"label": l, "note": n} for l, n in known_hits],
            "porting_notes_hits": list(dict.fromkeys(porting_hits)),
            "findings_hits": list(dict.fromkeys(finding_hits)),
        }, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
