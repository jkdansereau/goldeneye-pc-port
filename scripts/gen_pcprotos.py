#!/usr/bin/env python3
"""One-shot D38 generator for port/include/pc_protos.h (see docs/internals.md §F/D38).

Pipeline (all inputs were captured from a PC build of the decomp):
  1. Compile the game TUs and collect `implicit function declaration` warnings
     -> build-pc/implicit_funcs.txt (one name per line).
  2. Scan the tree for each name's true definition signature
     -> build-pc/protomap.json (name -> [return type, params...]).
  3. Run this script to emit port/include/pc_protos.h.

NOTE: the committed pc_protos.h is the source of truth and was hand-adjusted
after generation (C-only guard; full prototypes for the 11 functions whose true
signatures use default-promoted parameter types, which C11 forbids matching
with an empty parameter list). Re-running this script overwrites those tweaks —
regenerate only if new implicit declarations appear, then re-apply the tweaks.
"""

import json, re

names = [l.strip() for l in open('build-pc/implicit_funcs.txt') if l.strip()]
pm = json.load(open('build-pc/protomap.json'))

# MSVCRT / not-in-tree: fixed declarations
specials = {
    'assert': 'void',
    'memcmp': 'int',
    'snprintf': 'int',
    'time': 'long long',
    'GetCurrentThreadStackLimits': 'int',
}

def clean(rtype):
    rtype = re.sub(r'\s+', ' ', rtype).strip()
    for kw in ('extern ', 'static ', 'inline '):
        if rtype.startswith(kw):
            rtype = rtype[len(kw):].strip()
    return rtype

lines = []
for n in sorted(names):
    if n in specials:
        rt = specials[n]
    elif n in pm:
        rt = clean(pm[n][0])
        assert rt, (n, pm[n])
    else:
        raise SystemExit('no type for %s' % n)
    lines.append('%s %s();' % (rt, n))

hdr = '''/*
 * pc_protos.h - PC-port-only prototypes for implicitly declared functions.
 *
 * D38 (docs/internals.md section F): the decomp has ~72 translation units
 * that call ~400 functions without any visible prototype (missing #include of
 * the declaring header). Under C11 an implicit declaration assumes `int f()`,
 * which on N64 (MIPS, 32-bit pointers) is harmless, but on x86-64 it silently
 * truncates every pointer (and 64-bit scalar) return value to 32 bits - e.g.
 * tokenFind() in set_mt_tex_alloc() returned a low-32-bit "pointer" that then
 * faulted in strtol().
 *
 * This header declares each of those functions with its TRUE return type and
 * an empty parameter list (no argument checking, no dependency on the
 * parameter types' headers). Empty-paren declarations are compatible with the
 * real prototypes elsewhere, so this is purely additive: it changes only the
 * width of the returned value, restoring N64-correct semantics on x86-64.
 *
 * Anchored at the end of port/shim/PR/gbi.h (PC-only shim), which every game
 * TU parses via <ultra64.h>. Inert in the N64 build (no -DPORT).
 */
#ifndef _PC_PROTOS_H_
#define _PC_PROTOS_H_

#if defined(PORT) && defined(__x86_64__)

#include <stddef.h>      /* size_t */
#include <PR/gbi.h>      /* Gfx, Mtx, Vtx, Light (shimmed on PC) */
#include "bondtypes.h"   /* coord3d, PropRecord, ObjectRecord, ModelFileHeader, bool, ITEM_IDS */
#include "bondconstants.h" /* MPSCENARIOS, OBJECTIVESTATUS, PROP, DIFFICULTY, TICKOP */
#include "game/file.h"   /* save_data */

#pragma push_macro("assert")
#undef assert
void assert();
#pragma pop_macro("assert")

''' + '\n'.join(lines) + '''

#endif /* PORT && __x86_64__ */
#endif /* _PC_PROTOS_H_ */
'''
open('port/include/pc_protos.h', 'w').write(hdr)
print("wrote port/include/pc_protos.h with %d declarations" % len(lines))
