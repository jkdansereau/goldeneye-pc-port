/*
 * PC port shim for bondconstants.h (see docs/PCPortResearch.md).
 *
 * The real header is src/bondconstants.h. Its BITFLAG macro is only defined
 * under `#ifdef __sgi` (the N64/IDO compiler); on other compilers it is a
 * no-op. ATTACKTYPE was manually reverted to a plain enum by the decomp
 * author (its BITFLAG call is #if 0'd), and RUNTIMEBITFLAG has a set of
 * `#define` fallbacks for some names (OWNER, 00000001, 00000080, 00000100,
 * 00001000, 00000800, PADLOCKEDDOOR, HASOWNER). The remaining bitflag enum
 * members are therefore missing on non-SGI compilers.
 *
 * This shim includes the real header, then provides the missing members as
 * enums with the exact values the BITFLAG macro would generate (NAME_NONE =
 * 0, flags at 1 << 0 .. 1 << 31). Names that are already `#define`d in the
 * real header are omitted here (the macro shadows the member at use sites,
 * exactly as on SGI).
 *
 * If the decompilation adds a new active BITFLAG enum/member, add it here
 * too (unless it gets a `#define` fallback).
 *
 * Inert in the N64 build (no -DPORT): pure pass-through.
 */
#if defined(PORT)
#include "src/bondconstants.h"

#ifndef _PORT_BITFLAG_ENUMS_DEFINED
#define _PORT_BITFLAG_ENUMS_DEFINED

typedef enum FLAGS2
{
    FLAGS2_NONE = 0,
    FLAGS2_DONT_POINT_AT_BOND = 0x00000001,
    FLAGS2_02 = 0x00000002,
    FLAGS2_04 = 0x00000004
} FLAGS2;

typedef enum PS_FLAGS2
{
    PS_FLAGS2_NONE = 0,
    PS_FLAGS2_NO_DISTANCEQ = 0x00000001
} PS_FLAGS2;

typedef enum DOOR_LOCK
{
    DOOR_LOCK_NONE = 0,
    DOOR_LOCK_0 = 0x00000001,
    DOOR_LOCK_1 = 0x00000002,
    DOOR_LOCK_2 = 0x00000004,
    DOOR_LOCK_3 = 0x00000008,
    DOOR_LOCK_4 = 0x00000010,
    DOOR_LOCK_5 = 0x00000020,
    DOOR_LOCK_6 = 0x00000040,
    DOOR_LOCK_7 = 0x00000080
} DOOR_LOCK;

typedef enum PLAYERFLAG
{
    PLAYERFLAG_NONE = 0,
    PLAYERFLAG_LOCKCONTROLS = 0x00000001,
    PLAYERFLAG_NOCONTROL = 0x00000002,
    PLAYERFLAG_NOTIMER = 0x00000004
} PLAYERFLAG;

/* Members already #define'd in src/bondconstants.h are omitted:
 * RUNTIMEBITFLAG_OWNER, RUNTIMEBITFLAG_00000001, RUNTIMEBITFLAG_00000080,
 * RUNTIMEBITFLAG_00000100, RUNTIMEBITFLAG_00001000, RUNTIMEBITFLAG_00000800,
 * RUNTIMEBITFLAG_PADLOCKEDDOOR, RUNTIMEBITFLAG_HASOWNER */
typedef enum RUNTIMEBITFLAG
{
    RUNTIMEBITFLAG_NONE = 0,
    RUNTIMEBITFLAG_00000002 = 0x00000002,
    RUNTIMEBITFLAG_REMOVE = 0x00000004,
    RUNTIMEBITFLAG_ISRETICK = 0x00000008,
    RUNTIMEBITFLAG_TAGGED = 0x00000010,
    RUNTIMEBITFLAG_THROWING_KNIFE_RELATED = 0x00000020,
    RUNTIMEBITFLAG_EMBEDDED = 0x00000040,
    RUNTIMEBITFLAG_HASPROJECTILE = 0x00000080,
    /* RUNTIMEBITFLAG_00000080 is the #define'd numeric name for the same bit */
    RUNTIMEBITFLAG_BEENOPENED = 0x00000200,
    RUNTIMEBITFLAG_DESTROYED = 0x00000400,
    /* RUNTIMEBITFLAG_00000800 / _00001000 / _PADLOCKEDDOOR are #define'd */
    RUNTIMEBITFLAG_ACTIVATED = 0x00004000,
    RUNTIMEBITFLAG_00008000 = 0x00008000,
    RUNTIMEBITFLAG_00010000 = 0x00010000,
    RUNTIMEBITFLAG_00020000 = 0x00020000,
    RUNTIMEBITFLAG_00040000 = 0x00040000,
    /* RUNTIMEBITFLAG_HASOWNER is #define'd */
    RUNTIMEBITFLAG_00100000 = 0x00100000,
    RUNTIMEBITFLAG_00200000 = 0x00200000,
    RUNTIMEBITFLAG_00400000 = 0x00400000,
    RUNTIMEBITFLAG_00800000 = 0x00800000,
    RUNTIMEBITFLAG_01000000 = 0x01000000,
    RUNTIMEBITFLAG_02000000 = 0x02000000,
    RUNTIMEBITFLAG_04000000 = 0x04000000,
    RUNTIMEBITFLAG_08000000 = 0x08000000,
    RUNTIMEBITFLAG_10000000 = 0x10000000,
    RUNTIMEBITFLAG_20000000 = 0x20000000,
    RUNTIMEBITFLAG_40000000 = 0x40000000,
    RUNTIMEBITFLAG_80000000 = 0x80000000
} RUNTIMEBITFLAG;

#endif /* _PORT_BITFLAG_ENUMS_DEFINED */

/*
 * D8: MODELSKELETON / New_ModelSkeleton write `SKELETON(##NAME##)` and
 * `JOINTLIST(##NAME##)`, i.e. a `##` directly adjacent to `(` and `)`. IDO
 * tolerated this (the paste is a no-op), but GCC hard-errors: "pasting '(' and
 * 'NAME' does not give a valid preprocessing token". The outer `##` is
 * redundant: NAME is already used with `##` inside SKELETON/JOINTLIST
 * (`skeleton_##NAME` / `jointlist_##NAME`), so it is not pre-expanded either
 * way. Redefine both to drop the outer `##`; the expansion is byte-identical.
 */
#undef MODELSKELETON
#define MODELSKELETON(NAME, NUMJOINTS, SKELSIZE) ModelSkeleton SKELETON(NAME) = {NUMJOINTS, 0, JOINTLIST(NAME), SKELSIZE, 0};

#undef New_ModelSkeleton
#define New_ModelSkeleton(NAME, SKELSIZE, HASNAMES, NUMJOINTS) \
    ModelSkeleton SKELETON(NAME) = {                   \
    IF_ELSE(IS_EMPTY(NUMJOINTS))                           \
    (                                                      \
        sizeof(JOINTLIST(NAME))/sizeof(ModelJoint)         \
    )                                                      \
    (                                                      \
        NUMJOINTS                                          \
    ),                                                     \
    0,                                                     \
    JOINTLIST(NAME),                                       \
    SKELSIZE,                                              \
    0                                                      \
    IF(AND(DEFINED(DEBUG), BOOL(HASNAMES)))                \
    (                                                      \
        DEFER(COMMA)() jointnames_##NAME                   \
    )                                                      \
    };

/*
 * D10: the file-record macros paste `&` onto NAME (`{& ## NAME ## _header, ...}`
 * in CHRFILERECORD / GUNFILERECORD / SUIT_LFRECORD and `& ## NAME ## _stats`
 * in GUNSTATS). IDO tolerated the failed `&##NAME` paste (leaving `&` alone
 * while NAME pasted with its suffix); GCC hard-errors ("pasting '&' and 'name'
 * does not give a valid preprocessing token"). Rewrite with the `&` kept out
 * of the paste; the expansion is byte-identical (`&NAME_header`, `&NAME_stats`).
 */
#undef CHRFILERECORD
#define CHRFILERECORD(NAME, SCALE, OFFSET, HASHEAD, ISMALE) \
    {&NAME##_header, STR(C##NAME##Z), SCALE, OFFSET, HASHEAD, ISMALE},

#undef GUNSTATS
#define GUNSTATS(NAME) &NAME##_stats

#undef GUNFILERECORD
#define GUNFILERECORD(NAME, NOMODEL, STATS, UPPERTEXTID, LOWERTEXTID, POSX, POSY, POSZ, XROT, YROT, WOCTEXT, EQUIPTEXT, EQUIPX, EQUIPY, EQUIPZ) \
    { &NAME##_header, STR(G##NAME##Z), NOMODEL, STATS, UPPERTEXTID, LOWERTEXTID, POSX, POSY, POSZ, XROT, YROT, WOCTEXT, EQUIPTEXT, EQUIPX, EQUIPY, EQUIPZ},

#undef SUIT_LFRECORD
#define SUIT_LFRECORD(NAME, NOMODEL, STATS, UPPERTEXTID, LOWERTEXTID, POSX, POSY, POSZ, XROT, YROT, WOCTEXT, EQUIPTEXT, EQUIPX, EQUIPY, EQUIPZ) \
    { &NAME##_header, STR(C##NAME##Z), NOMODEL, STATS, UPPERTEXTID, LOWERTEXTID, POSX, POSY, POSZ, XROT, YROT, WOCTEXT, EQUIPTEXT, EQUIPX, EQUIPY, EQUIPZ},

#else
#include "src/bondconstants.h"
#endif
