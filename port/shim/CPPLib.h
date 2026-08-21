/*
 * PC port shim for include/CPPLib.h (see docs/PCPortResearch.md §11 D9).
 *
 * CPPLib's emptiness test pastes its argument into a macro name:
 *
 *     #define IS_EMPTY(x)  _IS_EMPTY(x)
 *     #define _IS_EMPTY(x) IS_PROBE(CAT(_IS_EMPTY, _##x##_))
 *
 * The `_##x##_` paste hard-errors in GCC when x is not a single identifier —
 * e.g. a pp-number like `0.1` (PROPFILERECORD's SCALE argument: "pasting '_'
 * and '0.1' does not give a valid preprocessing token") or a multi-token
 * expression. IDO evidently tolerated the failed paste.
 *
 * After macro-argument expansion, the original test is exactly "is the token
 * sequence empty?": only `_IS_EMPTY__` (the empty case) is defined as PROBE,
 * every other pasted name is undefined and yields 0. This shim replaces
 * IS_EMPTY with a paste-free variant that computes the same result for zero-
 * and single-argument calls:
 *
 *     IS_EMPTY()            -> 1   (empty argument)
 *     IS_EMPTY(<anything>)  -> 0
 *
 * Multi-argument calls (only reachable through SWITCH(), which is not used on
 * the port — see D7) are undefined here, as they were only ever meaningful
 * under IDO. NOT/DEFINED/IS_BOOL keep their original paste form: every active
 * port call site passes them a single token (0/1, an identifier, or empty),
 * which pastes cleanly. If a new one does not, extend this shim the same way.
 *
 * All in-tree includes of CPPLib.h are angle-bracketed, so this file
 * (first on the include path) intercepts them. Idempotent under repeated
 * inclusion. Inert in the N64 build (no -DPORT): pure pass-through.
 */
#if defined(PORT)
#include "include/CPPLib.h"

#undef IS_EMPTY
#define IS_EMPTY(...) _PORT_IS_EMPTY_SEL(__VA_ARGS__, ~, _PORT_IS_EMPTY_N, _PORT_IS_EMPTY_0)()
/* Pick the 3rd parameter: with zero content args the sentinels shift it to
 * _PORT_IS_EMPTY_0; with one or more content args it is _PORT_IS_EMPTY_N. */
#define _PORT_IS_EMPTY_SEL(a, b, f, ...) f
#define _PORT_IS_EMPTY_0() 1
#define _PORT_IS_EMPTY_N(x, ...) 0

#else
#include "include/CPPLib.h"
#endif
