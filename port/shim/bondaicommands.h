/*
 * PC port shim for bondaicommands.h (see docs/PCPortResearch.md §11 D7).
 *
 * The real header is src/bondaicommands.h. It was written for the N64/IDO
 * preprocessor, which is more lenient than GCC in two ways that break the
 * build (only chraidata.c exercises these paths; it is the sole file that
 * uses CALL()/SWITCH()):
 *
 *  1. isSubroutine() calls DEFINED(SETUPSUBROUTINES(ID)). CPPLib's DEFINED
 *     does `CAT(_DEFINED, _##x##_)`; with x = SETUPSUBROUTINES(ID) (a
 *     multi-token macro call) GCC hard-errors on the `##` paste
 *     ("pasting ')' and '_' does not give a valid preprocessing token").
 *     IDO evidently expanded/dropped the paste so the CPPLib special case
 *     `_DEFINED_SETUPSUBROUTINES(ID)` was reached. The intended semantics:
 *     emit `| SETUPSUBROUTINES(ID)` iff the setup defined SETUPSUBROUTINES.
 *     No file in this tree defines it, so the OR-clause is always absent.
 *
 *  2. SWITCH() declares 49 fixed parameters (VAR + 16x3 case slots) and
 *     detects present cases via IS_EMPTY(CASE_CONTENTx). Game code calls it
 *     with 14-26 args, relying on IDO treating missing args as empty. GCC
 *     enforces the exact count. Worse, its content arguments (e.g.
 *     `PlayAnimation(...) BREAK`) are single arguments at parse time that
 *     expand to top-level comma lists; any re-parse of the forwarded stream
 *     splits them across the fixed CASE/VAL/CONTENT slots and mangles the
 *     output. A GCC-clean reimplementation would have to partition that
 *     stream, which the C preprocessor cannot do (content arity is not
 *     detectable). See §11 D7 for the full analysis.
 *
 *  3. The generated CALL() (aicommands2.h) concatenates SetReturnAiList()
 *     and SetChrAiList(), both of which end in their own trailing comma,
 *     and then appends its own separator comma. IDO accepted the resulting
 *     `... , ,` inside the array initializer; GCC hard-errors ("expected
 *     expression before ',' token"). The artifact byte is never executed:
 *     AI_SetChrAiList(CHR_SELF) switches to the called list at offset 0 and
 *     AI_Return resumes the return list at offset 0 (chrai.c), so anything
 *     after the SetChrAiList record in CALL is dead. Re-emitted without the
 *     artifact (§11 D11).
 *
 * Resolution: the three active SWITCH() call sites in chraidata.c are
 * `#ifdef PORT`-gated to the hand-written equivalents already present in the
 * file (each verified byte-identical to the IDO expansion of the SWITCH
 * call, including the IFNewRandomGreaterThan == SetNewRandom()+
 * IFRandomGreaterThan identity). The original 49-parameter SWITCH is left
 * defined but unused on the port; this shim replaces it with a marker that
 * fails loudly if new game code ever uses SWITCH() on the port.
 *
 * This shim includes the real header, then redefines isSubroutine and CALL
 * to be GCC-clean while preserving the original expansion exactly.
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#if defined(PORT)
#include "src/bondaicommands.h"

/* --- D7.1: isSubroutine without the DEFINED(SETUPSUBROUTINES(ID)) paste ---
 *
 * Replicates the original OR-chain, replacing the
 * `IF(NOT(DEFINED(SETUPSUBROUTINES(ID))))(| SETUPSUBROUTINES(ID))` tail with
 * _PORT_SS_OR(ID), which expands to `| SETUPSUBROUTINES(ID)` only when the
 * setup defined SETUPSUBROUTINES before including this header (the documented
 * pattern; see the comment above isSubroutine in the real header). #ifdef
 * works on function-like macros, so this is a faithful "is it defined" test.
 * When undefined (the case for every file in this tree) it expands to
 * nothing, matching the IDO result of DEFINED(SETUPSUBROUTINES(ID)) == 1.
 */
#ifdef SETUPSUBROUTINES
#    define _PORT_SS_OR(ID) | SETUPSUBROUTINES(ID)
#else
#    define _PORT_SS_OR(ID)
#endif

#undef isSubroutine
#define isSubroutine(ID) ((ID == GAILIST_PLAY_IDLE_ANIMATION) |\
                            (ID == GAILIST_BASH_KEYBOARD) | \
                            (ID == GAILIST_ATTACK_BOND) | \
                            (ID == GAILIST_RUN_TO_BOND) | \
                            (ID == GAILIST_STARTLE_AND_RUN_TO_BOND) | \
                            (ID == GAILIST_WAIT_ONE_SECOND) \
                            _PORT_SS_OR(ID))

/* --- D7.2: SWITCH() is not shimmed ---------------------------------------
 *
 * See the header comment: the original 49-parameter SWITCH cannot be made
 * GCC-clean without partitioning the caller's already-expanded argument
 * stream, which the C preprocessor cannot express. All active call sites in
 * chraidata.c are `#ifdef PORT`-gated to byte-identical hand-written
 * equivalents, so SWITCH is never expanded on the port. Redefine it as a
 * marker that produces an obvious error if new game code ever uses it here.
 */
#undef SWITCH
#define SWITCH(...) _PORT_SWITCH_NOT_SUPPORTED_ON_PORT /* D7: use a #ifdef PORT hand-written equivalent in chraidata.c style; see docs/PCPortResearch.md §11 */

/* --- D11: CALL() without the double trailing comma ------------------------
 *
 * The generated CALL() is:
 *
 *   IF_ELSE(DEFINED(THIS))(AI_ERR_NO_THIS)((SetReturnAiList(THIS)SetChrAiList(CHR_SELF, X))),
 *
 * SetReturnAiList()/SetChrAiList() are byte-list macros that each end in a
 * trailing comma, and CALL appends one more as the array-initializer
 * separator. IDO's compiler accepted the resulting `...lo , ,` (empty
 * initializer element); GCC rejects it. The byte(s) after the SetChrAiList
 * record are never read: AI_SetChrAiList with CHR_NUM == CHR_SELF sets
 * AiListp/Offset to the called list at offset 0, and AI_Return resumes the
 * return list at offset 0 (chrai.c), so CALL's artifact is dead.
 *
 * This redefinition emits the identical command bytes directly —
 * AI_SetReturnAiList + id16(THIS) + AI_SetChrAiList + CHR_SELF + id16(X) —
 * keeping CALL's own separator comma and leaving the THIS-undefined
 * diagnostic branch (AI_ERR_NO_THIS) untouched. The extra paren level is
 * required: IF_ELSE's selected branch must be a single macro argument, and
 * TRY_EXPAND() then strips the parens and re-emits the byte list flat.
 */
#undef CALL
#define CALL(AI_LIST_ID) IF_ELSE(DEFINED(THIS))(AI_ERR_NO_THIS)((AI_SetReturnAiList, CharArrayFrom16(THIS), AI_SetChrAiList, CHR_SELF, CharArrayFrom16(AI_LIST_ID))),

#else
#include "src/bondaicommands.h"
#endif
