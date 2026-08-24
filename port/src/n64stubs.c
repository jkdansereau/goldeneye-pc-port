/*
 * N64-boot-specific symbol stubs.
 *
 * We compile src/init.c (for mainproc() — see A3 in docs/PCPortResearch.md),
 * but its N64-only init() is never called on the PC. The compiler still needs
 * the symbols init() references to resolve at link time, so this file provides
 * inert stubs for the ones that are NOT part of the libultra OS API (those
 * live in libultra.c).
 *
 * These are the boot/segment/decompress/TLB symbols, normally provided by the
 * linker script, boot.s, the inflate code, and the tlb_*.s assembly, plus the
 * rmon (remote monitor) host-communication functions from src/rmon.c.
 *
 * STATUS: scaffolding — the stubs let the compiled set link. The init() ones
 * are never executed on the PC (init() is not called; mainproc() is); the
 * rmon ones are inert (rmon is the N64 host debugger).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <PR/ultratypes.h>
#include <PR/os.h>
#include <tlb_manage.h>

/* --- Segment start/end getters (normally linker-script symbols) --------- */
/* On the PC the ROM is loaded by romdata.c; these are unused. Return 0.   */

u32 get_csegmentSegmentStart(void)   { return 0; }
u32 get_cdataSegmentRomStart(void)   { return 0; }
u32 get_cdataSegmentRomEnd(void)     { return 0; }
u32 get_inflateSegmentRomStart(void) { return 0; }
u32 get_inflateSegmentRomEnd(void)   { return 0; }

/*
 * Linker segment boundary symbols.
 *
 * The ROM-backed ones (every _XSegment{Start,RomStart,RomEnd,End} from
 * ge007.ld, plus all obseg/ramrom/music asset labels) are now ABSOLUTE cart
 * addresses in the generated port/src/romassets_u.s — see
 * scripts/gen_romassets.py. romdata.c maps the .z64 at 0x10000000 so `&sym`
 * yields a live address and romCopy() works.
 *
 * What remains here are pure-RAM segment symbols that have no ROM presence:
 * the host's own .bss/.csegment ends and the N64 vaddr markers (only used by
 * the never-run N64 boot path on the PC).
 *
 * _bssSegmentEnd is NOT here: it is an ABSOLUTE symbol in dram_syms.s
 * (0x70050000, inside the s32-safe DRAM view — boss.c starts the mempools
 * at PHYS_TO_K0(osVirtualToPhysical(&_bssSegmentEnd)); see port/src/dram.c).
 */
u32 *_csegmentSegmentStart       = NULL;
u32 *_csegmentSegmentEnd         = NULL;
u32 *_inflateSegmentVaddrStart   = NULL;
u32 *_inflateSegmentVaddrEnd     = NULL;
u32 *_gameSegmentVaddrStart      = NULL;
u32 *_gameSegmentVaddrEnd        = NULL;

/* --- Decompressor (src/inflate, not built for PC) ----------------------- */
/* init() calls this to unpack the data segment. Unused on the PC.         */
u32 jump_decompressfile(u32 source, u32 target, u32 buffer)
{
    (void)source; (void)target; (void)buffer;
    return 0;
}

/* --- TLB (src/tlb_*.s + src/tlb_manage.c, not built for PC) ------------- */
/* The PC has its own MMU; the N64 TLB miss handler is irrelevant.         */
void initTLBPrepareContext(void) { /* no-op */ }

/* Address of the N64 TLB-miss handler. init() copies it to K0BASE; unused. */
void resolve_TLBaddress_for_InvalidHit(void) { /* no-op */ }

/*
 * tlb_manage.c is excluded (it manages the N64's 64-entry TLB for on-demand
 * ROM segment loading). boss.c still calls two of its functions from
 * bossInitMainthreadData().
 *
 * tlbmanageGetTlbAllocatedBlock() is NOT inert: boss.c:218 uses it as the
 * END of the mempool area (mempCheckMemflagTokens(start, block - start)).
 * On the N64 it is page_align_down(&sp_boot) - MAPPING_TABLE_COUNT*PAGE_SIZE
 * = 0x803AB400 - 93*0x2000 = 0x802F4400 (US .stacks). We keep that exact
 * OFFSET (+0x2F4400 from the DRAM base) but express it in the s32-safe V1
 * view (see dram.c): 0x70000000 + 0x2F4400 = 0x702F4400, so [start, block)
 * is live host memory and survives the s32 paths in src/memp.c.
 */
void tlbmanageEstablishManagementTable(void) { /* no-op */ }
void tlbmanageResetCurrentEntriesCount(void)  { /* no-op */ }
void tlbmanageTranslateLoadRomFromTlbAddress(u32 address)
{
    (void)address;
}
u8 (*tlbmanageGetTlbAllocatedBlock(void))[TLB_BLOCK_SIZE]
{
    return (u8 (*)[TLB_BLOCK_SIZE])0x702F4400;
}

/* --- K&R libc helpers (IDO provided these; the host libc does not) ------- */
/* Signatures mirror include/PR/os.h:983 / include/bstring.h exactly.        */
void bcopy(const void *src, void *dst, int n) { memmove(dst, src, (size_t)n); }
void bzero(void *s, int n)                     { memset(s, 0, (size_t)n); }

/* --- libm internals (IDO's libm exposed these; MinGW's does not) --------- */
/* Quiet NaN float, used by gu/cosf.c and game/zlib.c. The VALUE is the same  */
/* qNaN on any endianness (bit pattern 0x7FC00000 read back as a float).      */
float __libm_qnan_f(void)
{
    union { u32 i; float f; } u;
    u.i = 0x7FC00000u;
    return u.f;
}

/* --- Remote monitor (src/rmon.c, not built for PC) ---------------------- */
/* rmon is the N64 remote monitor (host debugger); it has no PC equivalent.  */
/* init.c's rmonCreateThread() starts a thread running rmonMain; stub it as a */
/* no-op so the thread (if ever started) does nothing.                        */
void rmonMain(void) { /* no-op: rmon (remote monitor) is N64-only */ }

/* rmon host I/O + token/status, referenced by game code (indy_commands.c,
 * indy_comms.c, boss.c, token.c). All inert on the PC. */
void osReadHost(void *buffer, u32 size)   { (void)buffer; (void)size; }
void osWriteHost(void *buffer, u32 size)  { (void)buffer; (void)size; }
s32 rmonGetToken(void) { return 0; }
s32 rmonStatus(void)   { return 0; }

/*
 * osSyncPrintf is the game's debug/printf channel (also used by the assert()
 * macros). Route it to stderr so assert failures and debug output are visible
 * during porting. TODO(Phase 1): integrate with the port's sysLogPrintf.
 */
void osSyncPrintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr); /* keep probe lines intact across crashes (block-buffered when redirected) */
}

/* --- VI manager (src/libultrare/io/vimgr.c, not built for PC) ----------- */
/* sched.c's osCreateScheduler() calls osCreateViManager(OS_PRIORITY_VIMGR).  */
/* The N64 VI manager thread serviced video interrupts; on the port the      */
/* cooperative kernel posts the retrace message itself (port/src/libultra.c), */
/* so this is a no-op.                                                        */
void osCreateViManager(OSPri pri) { (void)pri; }

/* --- Crash screen (src/crash.c, not built for PC) ------------------------ */
/* src/crash.c is the game's TLB-fault diagnostics + rmon-driven crash        */
/* screen renderer. It depends on rmon and the N64 exception path, neither of */
/* which exists here; real host faults are caught by port/src/crash.c instead.*/
/* sched.c only calls this for the debug stderr overlay (off at boot), so a   */
/* no-op is safe.                                                             */
void crashRenderFrame(u16 *buffer) { (void)buffer; }
