#ifndef PORT_PORTADDR_H
#define PORT_PORTADDR_H

/*
 * portaddr.h — the ONE place that names the N64-DRAM arena windows and
 * converts between 32-bit N64 addresses and live host pointers.
 *
 * D607: unifies D441's arena macros (this file, pre-unification) with the
 * #95 macOS address-window model (port/include/port_addr.h), which is
 * superseded by this file. One header, one shifted base, one guard helper.
 *
 * The port realises the N64's 32-bit address space as a range of *host*
 * addresses (the [PORT_ADDR_BASE, PORT_ADDR_BASE + PORT_ADDR_WINDOW) window),
 * because the game stores addresses in 32-bit fields and 32-bit arithmetic
 * and expects `(u32)ptr` to round-trip:
 *
 *   0x00000000..0x0fffffff  segmented DL addresses (never host memory)
 *   0x10000000..0x1fffffff  cart image + sidecars (CART_BASE)
 *   0x40000000..0x6fffffff  image-relative encoding (see portN64ToHost)
 *   0x70000000..0x707fffff  DRAM V1 ("virtual", s32-safe; all RAM symbols)
 *   0x80000000..0x807fffff  DRAM V2 (KSEG0 mirror of V1)
 *   0xa0000000..0xbfffffff  game-thread stacks
 *
 * On Windows/Linux the window starts at host address 0, so `(u32)ptr ==
 * offset` and the pointer IS the N64 address. Native arm64 macOS cannot map
 * below 4 GiB (__PAGEZONE; docs/dev/MACOS-ARM64-PLAN.md sec 1), so there the
 * whole range is shifted up by PORT_ADDR_BASE = 16 TiB. Every game-visible
 * 32-bit value is then IDENTICAL to the x86_64 build; only the u32->host
 * direction adds the base. PORT_ADDR_BASE is 0 everywhere but arm64 macOS,
 * so those builds are bit-identical to the pre-unification literals (the
 * identity claim the x86_64 golden gates verify).
 *
 * Compile-time constants: no codegen change on x86_64.
 */

/*
 * The shifted base of the window. #ifndef-guarded so a -D on the command
 * line (CMake's GE_HIGHARENA_BASE flavour; the #95 PORT_ADDR_BASE cache var
 * for tooling) can override it. 16 TiB is reserved cleanly on Apple
 * Silicon, far above libmalloc's regions and the mmap hint area, and well
 * below fast3d_ptr_ok()'s 128 TiB bound; an AddressSanitizer build (whose
 * shadow region overlaps 16 TiB) uses a LowMem base like 0x4000000000.
 *
 * The base selection below is shared by every context: preprocessed .S
 * input (dram_syms.S via `cc -E`, where only the macros below matter and
 * platform.h's C content would not survive the assembler) and C/C++ alike.
 * __APPLE__ && __aarch64__ mirrors platform.h's PLATFORM_MACOS &&
 * PLATFORM_ARM selection exactly. portaddr.h stays self-contained (no
 * platform.h include): it is included from every TU, asm and C++ alike.
 */
#ifndef PORT_ADDR_BASE
#if defined(__APPLE__) && defined(__aarch64__)
#define PORT_ADDR_BASE 0x100000000000ULL
#else
#define PORT_ADDR_BASE 0
#endif
#endif

/* Size of the N64 address space. */
#define PORT_ADDR_WINDOW (1ULL << 32)

/* The low PORT_ADDR_LOW_GUARD bytes of the window are rejected by the
 * plausibility checks: nothing is mapped there, and with PORT_ADDR_BASE == 0
 * that range contains NULL, which is exactly what the callers' plausibility
 * checks exist to catch (e.g. langGet() on a cleared bank). */
#define PORT_ADDR_LOW_GUARD 0x10000u

/* Bound on the executable image's size, for classifying a host pointer as
 * image-relative (portHostToN64). The binary is a few MB; 256 MiB is
 * comfortably generous. */
#define PORT_IMAGE_MAX (256u << 20)

/*
 * The N64-DRAM arena windows (D441). port/src/dram.c maps an 8 MB backing
 * store at BOTH views; everything that converts or range-checks an arena
 * address uses these instead of a literal, so moving the arena later (the
 * high-arena test flavour, the arm64 PORT_ADDR_BASE shift) is a change in
 * one header.
 *
 * Each define is #ifndef-guarded so a -D on the command line can override
 * it (the D441 high-arena test flavour: GE_HIGHARENA_BASE=0x90000000 adds
 * -DPORT_DRAM_V1_BASE=0x90000000 for C, CXX and ASM). On x86_64 the
 * defaults expand to exactly the pre-unification literals.
 */
#ifndef PORT_DRAM_V1_BASE
#define PORT_DRAM_V1_BASE  (PORT_ADDR_BASE + 0x70000000UL)  /* s32-safe "virtual" view (game RAM symbols) */
#endif
#ifndef PORT_DRAM_K0_BASE
#define PORT_DRAM_K0_BASE  (PORT_ADDR_BASE + 0x80000000UL)  /* KSEG0 mirror of V1 (same pages) */
#endif
#ifndef PORT_DRAM_SIZE
#define PORT_DRAM_SIZE     0x00800000UL  /* 8 MB, like an 8-MB-RAM N64 */
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>

/* D573 (#108, italoarruda): the ONE plausibility check for a host pointer
 * read out of game data (sndPlaySfx's ALSound*, langGet's text bank). True
 * only inside the DRAM arena views above or the mapped cart image (ROM +
 * model/bg sidecars, port/src/romdata.c). Replaces the old absolute
 * [64 KiB, 16 GiB) literal, which goes wrong as soon as the arena moves
 * (the arm64 PORT_ADDR_BASE shift, the D441 above-4-GiB tier) and which
 * also accepted any unmapped address in between. The low 64 KiB (NULL-ish)
 * is outside every window, so it is rejected.
 *
 * D607: this is the merged successor of #95's portAddrIsInWindow() -- the
 * #95 re-application folds its guard call sites (src/snd.c,
 * src/game/language.c: the same two guards) into portAddrIsMapped(), and
 * port/src/port_addr.c's self-test uses the window-bound constants above
 * (PORT_ADDR_LOW_GUARD / PORT_ADDR_WINDOW) for its boundary cases. */

/* Set by portAddrInit(): the executable's load address, and whether the
 * image lies outside the window (so a truncated pointer needs the
 * 0x40000000 encoding). False on Linux, true on Windows and macOS.
 * Defined by port/src/port_addr.c (lands with the #95 re-application);
 * nothing in the current tree references them until then. */
#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t g_portImageBase;
extern int g_portUseImageRel;

/* D573: exact check, implemented in port/src/romdata.c (shift-aware via
 * the arena macros above). */
int portAddrIsMapped(const void *p);

#ifdef PORT_ADDR_STRICT
/* Debug gate (-DPORT_ADDR_STRICT=1). Validates that a u32 being turned into
 * a host pointer actually names one of the N64 regions the port maps --
 * reports the bad ones AT THE CONVERSION, with a backtrace naming the
 * caller. Compiled out entirely by default; costs one call when enabled.
 * Defined by port/src/port_addr.c. */
void portAddrStrictCheck(uint32_t a);
#endif

/* Reserve the window (arm64 macOS) and record the image base. Must run
 * before any of the port's fixed mappings and before large allocations.
 * Defined by port/src/port_addr.c. */
void portAddrInit(void);

/* Result of the ROM-free self-test portAddrInit() ran (1 = all checks
 * passed). Used by `--selftest` so CI can gate on it without a ROM. */
int portAddrSelfTestPassed(void);

#ifdef __cplusplus
}
#endif

/* u32 N64/physical address -> live host pointer.
 *
 * N64 address 0 maps to NULL, NOT to PORT_ADDR_BASE. Offset 0 is never a
 * mapped address (the window's lowest live region is the cart at
 * 0x1000_0000), so base+0 lands in the reserved PROT_NONE area and faults.
 * More importantly, game code routinely signals failure by returning an N64
 * address of 0 and then testing the converted pointer for NULL --
 * `p = PORT_N64PTR(T, f()); if (p != NULL) *p = ...` (e.g.
 * chrCreateBloodStain(), objDeform()). With base+0 that test passes and the
 * very next store faults. Returning NULL restores the decomp's intended
 * semantics. Identity on x86_64: with PORT_ADDR_BASE == 0 the image-relative
 * path (0x40000000 + (ptr - image base), image base 0x140000000 on Windows)
 * round-trips the exact truncation the D131 fix relied on. */
static inline void *portN64ToHost(uint32_t a)
{
    if (a == 0) {
        return (void *)0;  /* NULL, without pulling in stddef.h: portaddr.h is included by every TU, asm and C++ alike */
    }
#ifdef PORT_ADDR_STRICT
    portAddrStrictCheck(a);
#endif
    if (g_portUseImageRel && a >= 0x40000000u && a < 0x70000000u) {
        return (void *)(g_portImageBase + (uintptr_t)(a - 0x40000000u));
    }
    return (void *)((uintptr_t)PORT_ADDR_BASE + (uintptr_t)a);
}

/* Host pointer -> u32, the inverse of portN64ToHost for in-window and
 * image pointers. Out-of-range pointers fall back to raw truncation (the
 * pre-shift behaviour); they should not be stored in 32-bit fields. */
static inline uint32_t portHostToN64(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (v - (uintptr_t)PORT_ADDR_BASE < (uintptr_t)PORT_ADDR_WINDOW) {
        return (uint32_t)(v - (uintptr_t)PORT_ADDR_BASE);
    }
    if (g_portUseImageRel && v >= g_portImageBase
            && v - g_portImageBase < (uintptr_t)PORT_IMAGE_MAX) {
        return (uint32_t)(0x40000000u + (v - g_portImageBase));
    }
    return (uint32_t)v;
}

/* Cast-site helper for game code (the D3x ABI/layout class): reads a 32-bit
 * address out of a field/expression and yields a typed pointer. Identity on
 * Windows/Linux (PORT_ADDR_BASE == 0). */
#define PORT_N64PTR(T, expr) ((T *)portN64ToHost((uint32_t)(expr)))

#endif /* __ASSEMBLER__ */

#endif /* PORT_PORTADDR_H */
