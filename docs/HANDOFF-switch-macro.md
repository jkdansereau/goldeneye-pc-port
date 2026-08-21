# HANDOFF — SWITCH/CALL macro port (D7/D8) — IN PROGRESS

## Status: BLOCKED on SWITCH expansion mangling (root cause now fully understood)

The `ge007` PC build fails on `src/game/chraidata.c` (the **only** file that uses
`SWITCH()`; 4 call sites) due to IDO-vs-GCC preprocessor differences in the AI-list
macro system. The `isSubroutine`/`MODELSKELETON` shims are done and working. The
**variadic `SWITCH` shim produces a mangled expansion** — this session confirmed the
exact mechanism and uncovered the *intended design* of the macro, which reframes the
fix.

---

## What was done this session (investigation + empirical confirmation)

### 1. PD port is NOT a reference (kills the previous top-priority lead)
`PD_PORT_CHECKOUT` has **no `bondaicommands.h` / `aicommands*.h`
at all** — Perfect Dark is a different game with no AI-list macro system. Do not look
there for a SWITCH shim. (It does have `include/PR/os_message.h` with `IS_EMPTY`,
unrelated.)

### 2. Empirically CONFIRMED the content-splitting mechanism
Minimal test `build-pc/swtest.c` (still on disk): a `PlayAnimation(A..E)` macro that
expands to a trailing-comma list, a 6-param `IMPL`, and a variadic
`SWITCH(VAR, ...) → IMPL(VAR, __VA_ARGS__, ,,,,,)`. Result:

```
error: macro 'IMPL' passed 16 arguments, but takes just 6
```

**Mechanism (now proven, not hypothesized):**
1. At SWITCH parse time the content `PlayAnimation(...) BREAK` is **one** argument
   (its commas are inside `PlayAnimation`'s parens).
2. C rule: each macro arg is **fully macro-expanded before substitution**. The content
   expands to a **top-level comma list** (`AI_PlayAnimation , ... , BREAK`).
3. Forwarding `__VA_ARGS__` into `_PORT_SWITCH_IMPL(...)` **re-parses** that token
   stream at top-level commas → the content is **split into N separate args**, which
   mis-fill `CASE0..CASE_CONTENTF`. The trailing comma of `PlayAnimation` adds one
   extra empty arg (hence 16, not 15).

### 3. Preprocessed the real file — mangled output confirmed
`gcc -E src/game/chraidata.c` (full flags in §Build) shows at the line-183 call:

```
AI_SetNewRandom 0}; Error: Switch Limited to 15 elements + 1 default u8 (183) & 0x00FF[] = {
  ((0) & 0xFF00) >> 8 ((0) & 0x00FF,lblNext) 0x10(lblDone)
  AI_Label , lblNext , AI_PlayAnimation (((ANIM_scratching_leg) & 0xFF00) >> 8,lblNext)
  ((183) & 0xFF00) >> 8(lblDone) AI_Label , lblNext , AI_GotoNext , ANIM_scratching_leg , & 0x00FF(lblDone) ...
```

Content fragments each get `(lblNext)`/`(lblDone)` spliced in; `CASEF` gets filled
(triggers "Switch Limited"); a fragment forms `BREAK(<2 args>)` → "BREAK passed 2
arguments" at chraidata.c:202.

### 4. Discovered the INTENDED DESIGN of the content (key reframing)
The content is **not** meant to survive as a single arg. It is a **template whose last
token is a dangling function-like macro name**, and the SWITCH body **appends
`(lblDone)` to complete that call**:

- `BREAK(LABEL)` → `GotoNext(LABEL)` → `AI_GotoNext , LABEL ,`  (aicommands2.h:70)
- `GotoNext(LABEL)` → `AI_GotoNext , LABEL ,`  (aicommands2.h:52)
- `PlayAnimation(...)` → `AI_PlayAnimation , CharArrayFrom16(..) , ... , INTERPOL ,`
  (trailing comma; aicommands2.h:386)
- `Label(ID)` → `AI_Label , ID ,` (aicommands2.h:137)
- `SetNewRandom()` → `AI_SetNewRandom ,` (aicommands2.h:1555)
- `IFRandomGreaterThan(BYTE,LBL)` → `AI_IFRandomGreaterThan , BYTE , LBL ,` (aicommands2.h:1607)
- `IFNewRandomGreaterThan(BYTE,LBL)` → `SetNewRandom()IFRandomGreaterThan(BYTE,LBL)` (aicommands2.h:1623)

Game code writes the content as e.g. `PlayAnimation(...) BREAK` (line 183) or
`TRYFiringRun` / `... TRYFireAtBondKneeling` (line 305) — i.e. a run of macro
calls/names **ending in a bare function-like name** (`BREAK`, `TRYFiringRun`,
`TRYFireAtBondKneeling`). The body line
`EXPAND_ARGS_STACK(CASE_CONTENTx)(lblDone)` therefore expands the dangling name into
`NAME(lblDone)`.

**Consequences for the port:**
- `EXPAND_ARGS_STACK(CASE_CONTENTx)` is *not* a 1-arg call in practice: after
  substitution the content's comma list fills `A,B,C,...` of its 33 params and the
  rest are empty (IDO leniency). Its body re-joins the non-empty args with `COMMA()`
  → it is effectively an **identity / full-expansion pass** over the content. So the
  shim's replacement of `EXPAND_ARGS_STACK(CASE_CONTENTx)` with bare `CASE_CONTENTx`
  is semantically fine *if* the content arrives intact.
- `IS_EMPTY(CASE_CONTENTx)` becomes a **multi-arg call** to a 1-param macro after
  substitution. IDO tolerated it (first-arg → `_IS_EMPTY_<tok>_` undefined → 0 =
  "not empty", which is the correct answer when content is present). GCC hard-errors
  (the `pasting '_' and '('` at CPPLib.h:257 is this).

### 5. Exact arg counts of the 4 call sites (corrects the old "4–25" note)
Counted top-level commas (comments stripped, paren-aware):

| line | args | shape |
|---|---|---|
| 183 | 20 | VAR + 5×(CASE,VAL,CONTENT) + `,,`default + CONTENT |
| 236 | 14 | VAR + 3×triple + `,,`default + CONTENT |
| 305 | 11 | VAR + 6×triple + `,,` + CONTENT + `,,` + CONTENT (two defaults) |
| 623 | 26 | VAR + 8×triple + `,,`default + CONTENT |

So the range is **11–26 args**, always `VAR` + a run of `(CASE, VAL, CONTENT)` triples
where a "default" is encoded as `,,` (empty CASE, empty VAL) before a CONTENT.

---

## The core blocker (precise statement)

The content must reach the body **as one argument** (so `IS_EMPTY`/`EXPAND_ARGS_STACK`
see it whole and `(lblDone)` lands on the dangling name). But:

- It is **one arg only at parse time**; C expands it to a top-level comma list before
  substitution (step 2 above).
- Any forwarding of that expanded list through `__VA_ARGS__` into a fixed-param macro
  **re-parses** it and splits it. That is exactly what the current shim does.

The original 49-param SWITCH avoided this because the content was bound to a **named
param** (`CASE_CONTENTx`) at parse time and substituted directly — never re-parsed.
GCC won't let us call a 49-param macro with 11–26 args, which is why we went variadic,
which reintroduces the re-parse. **That tension is the whole problem.**

---

## Directions for the next session (in suggested order)

1. **Protect the content from re-parsing by wrapping, not by re-parsing.**
   The only C protection for a comma list is parentheses. Investigate a `SWITCH(VAR,
   ...)` that forwards each arg **parenthesized** so the content's commas can't split
   it, then strips the parens in the body. The open sub-problem: identifying which
   args are content vs. simple CASE/VAL without already having split them. Note CASE
   and VAL are always simple single tokens (`IFRandomGreaterThan`, `250`); only
   CONTENT carries commas. A positional scheme (content = every 3rd arg) may be
   exploitable *if* the wrap happens before expansion — verify whether that's
   achievable at all, since expansion precedes any body processing.

2. **Write a fresh GCC-clean SWITCH that emits byte-identical output** (abandon the
   "verbatim original body" approach). First capture the *clean* IDO output for one
   call (e.g. hand-expand the line-236 14-arg call, or reconstruct from the body
   semantics now understood), then design a variadic macro that reproduces it. This
   sidesteps `IS_EMPTY`/`EXPAND_ARGS_STACK` multi-arg issues entirely if the new body
   doesn't route content through them.

3. **Shim `CPPLib.h`** (new `port/shim/CPPLib.h`, PORT-only, includes the real one)
   to make `IS_EMPTY`/`_IS_EMPTY`/`EXPAND_ARGS_STACK` tolerant of multi-arg /
   short-arg calls (variadic + first-token semantics). This is likely needed *in
   addition to* whichever content-protection scheme is chosen, because even an intact
   content becomes a multi-arg `IS_EMPTY`/`EXPAND_ARGS_STACK` call after substitution.
   Keep the N64 build untouched (shim is PORT-gated).

4. **Verify byte-identity** by diffing the PC preprocessed `m_IdleAnimations[]` (and
   the other 3 arrays) against a reference expansion. There is no IDO binary to diff
   against, so the reference must be reconstructed from the body semantics (direction
   2) — treat "compiles + plausible AI byte layout" as the practical bar, and record
   the reasoning.

---

## Files touched / present

| File | State |
|---|---|
| `port/shim/bondaicommands.h` | D7 shim. `isSubroutine` ✅. `SWITCH` variadic → **mangled** (this is the blocker). |
| `port/shim/bondconstants.h` | D8 shim. `MODELSKELETON`/`New_ModelSkeleton` ✅. |
| `build-pc/swtest.c` | Throwaway minimal repro of the split (16-vs-6 args). Keep until fixed, then delete. |

## Build / preprocess commands

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
cd build-pc && cmake --build . --target ge007 2>&1 | grep -iE "error|FAILED" | grep -vE "Building C object"
```

Preprocess just the failing file (flags mirror `build-pc/compile_commands.json`):

```sh
cd REPO_ROOT
gcc -DAVOID_UB=1 -DBUGFIX_R0 -DBYTEMATCH -DLANG_US -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM \
    -DPAL=0 -DPLATFORM_64BIT=1 -DPORT=1 -DREFRESH_NTSC -DVERSION_US -D_LANGUAGE_C=1 \
    -Iport/shim -I. -Iinclude -Iinclude/PR -Isrc -Isrc/game -Isrc/libultra -Isrc/libultra/audio \
    -Iport/include -Ibuild-pc/port/include -std=c11 -funsigned-char \
    -include build-pc/port/include/versioninfo.h -E src/game/chraidata.c
```

## Key references

- `src/bondaicommands.h` ~790–810 — original 49-param `SWITCH` body (ground truth for
  intended expansion).
- `src/aicommands2.h` — all AI command macros (lines cited in §4 above).
- `include/CPPLib.h` — `IS_EMPTY`/`_IS_EMPTY` (256–258), `IS_PROBE`/`PROBE` (227–231),
  `IF_VA`/`_IF_VA_0/1` (~328, 330–331), `EXPAND_ARGS_STACK` (337+), `EVAL` (85–98),
  `COMMA` (16), `CAT` (190).
- `src/game/chraidata.c:183,236,305,623` — the 4 `SWITCH` call sites.
- `docs/PCPortResearch.md` §11 — findings log; record the new findings as **D9+**
  (D7 = isSubroutine/SWITCH shim, D8 = MODELSKELETON, both already logged).

## Constraints (from AGENTS.md)

- Game code (`src/game/*.c`, `src/*.c`) compiles **UNMODIFIED**. All fixes go in `port/`.
- Shims are `PORT`-only and inert for the N64 build.
- `port/shim` is first on the include path, so `<bondaicommands.h>` /
  `<bondconstants.h>` (and, if added, `<CPPLib.h>`) resolve to the shims; shims
  include the real headers via `src/...` / `include/...` paths.
- Output must be byte-identical to the original IDO expansion.
