/*
 * PC port shim for bondtypes.h (see docs/PCPortResearch.md).
 *
 * The real header is src/bondtypes.h. It includes game/chrobjdata.h at line 31,
 * but chrobjdata.h declares extern arrays of ItemModelFileRecord /
 * ChrModelFileRecord, which are only defined later in src/bondtypes.h
 * (circular include). IDO accepts `extern struct S arr[];` with incomplete S;
 * modern GCC/Clang reject it as a hard error.
 *
 * This shim (found first via the port/shim include path) includes
 * chrobjdata.h first. chrobjdata.h's own `#include <bondtypes.h>` then
 * resolves back to this shim, which includes the real bondtypes.h in full
 * (its chrobjdata.h include is a no-op because chrobjdata.h's guard is
 * already set). Control returns to chrobjdata.h with all types complete,
 * so its extern array declarations are valid.
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#if defined(PORT)
#include "game/chrobjdata.h"
#include "src/bondtypes.h"

/* The decomp's New_Vector/New_Coord3d macros are declared with exactly 3
 * parameters (x, y, z) and use the IF_ELSE(IS_EMPTY(..)) trick to default each
 * to 0. But the game code calls them with ZERO args (e.g. `New_Vector()` at
 * chrai.c:1358), relying on IDO's leniency with empty macro arguments. GCC
 * rejects `New_Vector()` against a 3-parameter macro ("requires 3 arguments,
 * but only 1 given"). The game code only ever calls them with 0 args (to make
 * a zero vector/coord), so redefine to accept any arg count and expand to a
 * zero initializer. See docs/PCPortResearch.md. */
#undef New_Vector
#undef New_Coord3d
#define New_Vector(...) {0, 0, 0}
#define New_Coord3d(...) {0, 0, 0}

#else
#include "src/bondtypes.h"
#endif
