#ifndef PORT_ADDR_H
#define PORT_ADDR_H

/*
 * The N64 address-space window (M1 of the macOS port).
 *
 * The port realises the N64's 32-bit address space as a range of *host*
 * addresses, because the game stores addresses in 32-bit fields and 32-bit
 * arithmetic and expects `(u32)ptr` to round-trip:
 *
 *   0x00000000..0x0fffffff  segmented DL addresses (never host memory)
 *   0x10000000..0x1fffffff  cart image + sidecars (CART_BASE)
 *   0x40000000..0x6fffffff  image-relative encoding (see below)
 *   0x70000000..0x707fffff  DRAM V1 ("virtual", s32-safe; all RAM symbols)
 *   0x80000000..0x807fffff  DRAM V2 (KSEG0 mirror of V1)
 *   0xa0000000..0xbfffffff  game-thread stacks
 *
 * On Windows/Linux that range starts at host address 0, so `(u32)ptr ==
 * offset` and the pointer IS the N64 address. Native arm64 macOS cannot map
 * below 4 GiB (__PAGEZONE; see docs/dev/MACOS-ARM64-PLAN.md §1), so there the
 * whole range is shifted up by PORT_ADDR_BASE. Every game-visible 32-bit value
 * is then IDENTICAL to the Windows build; only the u32->host-pointer direction
 * adds the base. PORT_ADDR_BASE is 0 everywhere but macOS, so those builds are
 * unchanged.
 *
 * Image-relative encoding: pointers into the executable image cannot be
 * truncated into the window (the image is not inside it, and on arm64 it is
 * PIE with a per-run slide). They are encoded as 0x40000000 + (ptr - image
 * base). On Windows the image base is 0x140000000, so this equals the raw
 * `(u32)ptr` truncation the D131 fix relied on; on macOS it makes the round
 * trip explicit. Ported game code must use portN64ToHost/portHostToN64 (or
 * PORT_N64PTR) rather than a bare cast wherever a 32-bit field carries a
 * pointer that is not a plain N64 address.
 */

#include <stdint.h>
#include "platform.h"

#ifndef PORT_ADDR_BASE
#if defined(PLATFORM_MACOS) && defined(PLATFORM_ARM)
/* 16 TiB: reserved cleanly on Apple Silicon, far above libmalloc's regions and
 * the mmap hint area, and well below fast3d_ptr_ok()'s 128 TiB bound. The
 * generated Mach-O symbol files are produced with the SAME base (CMake's
 * PORT_ADDR_BASE cache var, fed to scripts/gen_macho_syms.py --base);
 * portAddrInit() checks the two agree at startup via cfb_16.
 *
 * Overridable from CMake (-DPORT_ADDR_BASE=...) for tooling that needs the
 * window somewhere else -- e.g. an AddressSanitizer build, whose shadow
 * region overlaps 16 TiB (it covers 0x027e00024000..0x10700001ffff, so a
 * sanitizer build uses a LowMem base like 0x4000000000). */
#define PORT_ADDR_BASE 0x100000000000ULL
#else
#define PORT_ADDR_BASE 0x0ULL
#endif
#endif

/* Size of the N64 address space. */
#define PORT_ADDR_WINDOW (1ULL << 32)

/* Bound on the executable image's size, for classifying a host pointer as
 * image-relative. The binary is a few MB; 256 MiB is comfortably generous. */
#define PORT_IMAGE_MAX (256u << 20)

/* Set by portAddrInit(): the executable's load address, and whether the image
 * lies outside the window (so a truncated pointer needs the 0x40000000
 * encoding). False on Linux, true on Windows and macOS. */
#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t g_portImageBase;
extern int g_portUseImageRel;

#ifdef PORT_ADDR_STRICT
/* Debug gate (-DPORT_ADDR_STRICT=1). Validates that a u32 being turned into a
 * host pointer actually names one of the N64 regions the port maps. The whole
 * D299/D300/D301/D302 bug family shares one signature -- a truncated or
 * never-rebased address reaching portN64ToHost -- and it is almost always
 * diagnosed from a fault somewhere far away, long after the bad value was
 * created. This reports it AT THE CONVERSION, with a backtrace naming the
 * caller. Compiled out entirely by default; costs one call when enabled. */
void portAddrStrictCheck(uint32_t a);
#endif

/* Reserve the window (macOS only) and record the image base. Must run before
 * any of the port's fixed mappings and before large allocations. */
void portAddrInit(void);

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
 * very next store faults (D301). Returning NULL restores the decomp's
 * intended semantics. */
static inline void *portN64ToHost(uint32_t a)
{
    if (a == 0) {
        return NULL;
    }
#ifdef PORT_ADDR_STRICT
    portAddrStrictCheck(a);
#endif
    if (g_portUseImageRel && a >= 0x40000000u && a < 0x70000000u) {
        return (void *)(g_portImageBase + (uintptr_t)(a - 0x40000000u));
    }
    return (void *)((uintptr_t)PORT_ADDR_BASE + (uintptr_t)a);
}

/* Host pointer -> u32, the inverse of portN64ToHost for in-window and image
 * pointers. Out-of-range pointers fall back to raw truncation (the pre-M1
 * behaviour); they should not be stored in 32-bit fields. */
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

/* Is a host pointer inside the N64 address window (DRAM / cart / stacks)?
 * Use this instead of hardcoded numeric bounds: the window is shifted by
 * PORT_ADDR_BASE on arm64, so an absolute bound that is correct on x86_64
 * silently rejects every real pointer there. */
static inline int portAddrIsInWindow(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    return v - (uintptr_t)PORT_ADDR_BASE < (uintptr_t)PORT_ADDR_WINDOW;
}

/* Cast-site helper for game code (the D3x ABI/layout class): reads a 32-bit
 * address out of a field/expression and yields a typed pointer. Identity on
 * Windows/Linux (PORT_ADDR_BASE == 0). */
#define PORT_N64PTR(T, expr) ((T *)portN64ToHost((uint32_t)(expr)))

#endif /* PORT_ADDR_H */
