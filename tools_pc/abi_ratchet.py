#!/usr/bin/env python3
"""D441 P4: ABI warning ratchet for CI.

Re-runs every .c/.cpp entry of BUILD_DIR/compile_commands.json with
-c -> -fsyntax-only, -o <x> removed, -Werror dropped, and
-Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion -Wno-error
appended (same method as scratch/d441/collect.py), deduped the same way
on (file, line, col, flag, msg). Counts per (repo-relative file, flag)
and compares against the baseline JSON {"file": {"flag": count}}:

  python tools_pc/abi_ratchet.py build-pc           # fail if any count rises
  python tools_pc/abi_ratchet.py build-pc --update  # rewrite the baseline
  python tools_pc/abi_ratchet.py build-pc --baseline other.json

Exit codes: 0 = OK (every count flat or lower), 1 = ratchet violation
(a count rose, or a new file/flag appeared with count > 0), 2 = usage or
environment error (missing compile_commands.json / baseline). Stdlib only.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROOT_FW = ROOT.replace('\\', '/')
DEFAULT_BASELINE = os.path.join(HERE, 'abi_baseline.json')

FLAGS = ['-Wpointer-to-int-cast', '-Wint-to-pointer-cast', '-Wint-conversion']
FLAG_RE = re.compile(
    r'^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+): warning: (?P<msg>.*?) '
    r'\[(?P<flag>-W(?:pointer-to-int-cast|int-to-pointer-cast|int-conversion))\]\s*$')

if os.name == 'nt':
    ENV = dict(os.environ)
    ENV['PATH'] = r'C:\msys64\mingw64\bin;' + ENV.get('PATH', '')
    if not ENV.get('ComSpec'):
        ENV['ComSpec'] = r'C:\Windows\system32\cmd.exe'
    if not ENV.get('PATHEXT'):
        ENV['PATHEXT'] = '.COM;.EXE;.BAT;.CMD'
    if not ENV.get('TMP'):
        ENV['TMP'] = ENV.get('TEMP', os.environ.get('SystemRoot', r'C:\Windows')) + r'\Temp'
    ENV['TEMP'] = ENV['TMP']
else:
    ENV = None  # inherit


def transform(entry):
    """-o <x> removed, -c -> -fsyntax-only, -Werror dropped, ratchet flags appended."""
    toks = shlex.split(entry['command'], posix=False)
    new, skip = [], False
    for t in toks:
        if skip:
            skip = False
            continue
        if t == '-o':
            skip = True
            continue
        if t == '-c':
            new.append('-fsyntax-only')
            continue
        if t == '-Werror' or t.startswith('-Werror='):
            continue
        new.append(t)
    new += FLAGS + ['-Wno-error']
    return new


def norm_path(p, tu_file, cwd):
    """Repo-relative forward-slash path (same rules as collect.py)."""
    p = p.replace('\\', '/')
    if p.startswith(ROOT_FW + '/'):
        return p[len(ROOT_FW) + 1:]
    if os.path.isabs(p):
        return p
    bases = []
    for b in (os.path.dirname(tu_file), cwd):
        if b not in bases:
            bases.append(b)
    fallback = None
    for b in bases:
        cand = os.path.normpath(os.path.join(b, p)).replace('\\', '/')
        if not cand.startswith(ROOT_FW + '/'):
            continue
        rel = cand[len(ROOT_FW) + 1:]
        if fallback is None:
            fallback = rel
        if os.path.exists(os.path.join(ROOT, *rel.split('/'))):
            return rel
    return fallback if fallback is not None else p


def run_tu(entry):
    """Returns (tu_file, rows, error); rows are (file, line, col, flag, msg)."""
    tu = entry['file']
    cwd = entry.get('directory', ROOT)
    cmd = transform(entry)
    env = ENV
    if env is not None and os.path.isabs(cmd[0]):
        # Put the compiler's own bin dir first so cc1 loads its matching DLLs.
        # On GitHub-hosted runners setup-msys2 installs under D:\a\_temp while
        # the image also ships an unrelated C:\msys64.
        env = dict(env)
        env['PATH'] = os.path.dirname(cmd[0]) + ';' + env['PATH']
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True,
                           text=True, env=env, timeout=600)
    except Exception as ex:  # noqa: BLE001 - report and continue
        return tu, [], repr(ex)
    rows = []
    for line in (p.stderr or '').splitlines():
        m = FLAG_RE.match(line)
        if m:
            rows.append((norm_path(m.group('path'), tu, cwd),
                         int(m.group('line')), int(m.group('col')),
                         m.group('flag'), m.group('msg')))
    err = 'rc=%d' % p.returncode if p.returncode != 0 else None
    return tu, rows, err


def collect(build_dir):
    cc_json = os.path.join(build_dir, 'compile_commands.json')
    if not os.path.isfile(cc_json):
        print('ERROR: %s not found — run ./build-pc.sh ntsc-final first '
              '(CMake exports compile_commands.json at configure time).'
              % cc_json, file=sys.stderr)
        sys.exit(2)
    db = json.load(open(cc_json, encoding='utf-8'))
    entries = [e for e in db if e['file'].endswith(('.c', '.cpp'))]
    print('compile_commands entries: %d total, %d .c/.cpp selected'
          % (len(db), len(entries)))

    t0 = time.time()
    results = []
    with ThreadPoolExecutor(max_workers=4) as ex:
        for r in ex.map(run_tu, entries):
            results.append(r)
    dt = time.time() - t0

    seen = set()
    errors = []
    for tu, rows, err in results:
        if err:
            errors.append((tu, err))
        seen.update(rows)

    counts = {}
    for (f, _ln, _col, flag, _msg) in seen:
        counts.setdefault(f, {})
        counts[f][flag] = counts[f].get(flag, 0) + 1

    print('elapsed: %.1fs' % dt)
    if errors:
        print('TU ERRORS (%d):' % len(errors), file=sys.stderr)
        for tu, err in errors:
            try:
                rel = os.path.relpath(tu, ROOT)
            except ValueError:
                rel = tu
            print('  %s  %s' % (rel, err), file=sys.stderr)
        sys.exit(2)
    return counts


def main():
    ap = argparse.ArgumentParser(description='ABI warning ratchet (D441)')
    ap.add_argument('build_dir', help='directory containing compile_commands.json')
    ap.add_argument('--baseline', default=DEFAULT_BASELINE,
                    help='baseline JSON path (default: %s)' % DEFAULT_BASELINE)
    ap.add_argument('--update', action='store_true',
                    help='rewrite the baseline from the current tree and exit 0')
    args = ap.parse_args()

    counts = collect(args.build_dir)
    total_now = sum(c for fl in counts.values() for c in fl.values())

    if args.update:
        with open(args.baseline, 'w', encoding='utf-8', newline='\n') as fh:
            json.dump(counts, fh, indent=2, sort_keys=True)
            fh.write('\n')
        print('baseline updated: %s (%d files, %d warnings)'
              % (args.baseline, len(counts), total_now))
        return

    if not os.path.isfile(args.baseline):
        print('ERROR: baseline %s not found — run with --update first.'
              % args.baseline, file=sys.stderr)
        sys.exit(2)
    base = json.load(open(args.baseline, encoding='utf-8'))
    total_base = sum(c for fl in base.values() for c in fl.values())

    violations = []
    lowered = 0
    for f in sorted(set(counts) | set(base)):
        for flag in sorted(set(counts.get(f, {})) | set(base.get(f, {}))):
            old = base.get(f, {}).get(flag, 0)
            new = counts.get(f, {}).get(flag, 0)
            if new > old:
                violations.append((f, flag, old, new))
            elif new < old:
                lowered += 1

    if violations:
        print('ABI RATCHET VIOLATION: %d file/flag count(s) rose' % len(violations))
        for f, flag, old, new in violations:
            print('  %s  %s: %d -> %d' % (f, flag, old, new))
        sys.exit(1)

    if lowered:
        print('abi ratchet OK: %d warnings now vs %d baseline '
              '(%d file/flag count(s) lower; run --update to ratchet down)'
              % (total_now, total_base, lowered))
    else:
        print('abi ratchet OK: %d warnings now vs %d baseline'
              % (total_now, total_base))


if __name__ == '__main__':
    main()
