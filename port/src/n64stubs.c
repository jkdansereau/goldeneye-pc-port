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
#include <tlb_manage.h>

/* --- Segment start/end getters (normally linker-script symbols) --------- */
/* On the PC the ROM is loaded by romdata.c; these are unused. Return 0.   */

u32 get_csegmentSegmentStart(void)   { return 0; }
u32 get_cdataSegmentRomStart(void)   { return 0; }
u32 get_cdataSegmentRomEnd(void)     { return 0; }
u32 get_inflateSegmentRomStart(void) { return 0; }
u32 get_inflateSegmentRomEnd(void)   { return 0; }

/*
 * Linker segment boundary pointers (normally linker-script symbols, declared
 * `extern u32 *` in bondgame.h and assorted game headers). Game code takes
 * their ADDRESSES for size calcs + PI DMA / romCopy; on the PC the values are
 * meaningless until Phase 2 ROM loading remaps them (romdata.c). All defined
 * NULL: address-of gives a valid host pointer, and pairs of NULLs compute to
 * zero-length copies. Complete set = every `*Segment*` symbol referenced by
 * the compiled game sources.
 */
u32 *_bssSegmentEnd              = NULL;

u32 *_codeSegmentStart           = NULL;
u32 *_codeSegmentEnd             = NULL;
u32 *_codeSegmentRomStart        = NULL;
u32 *_codeSegmentRomEnd          = NULL;

u32 *_csegmentSegmentStart       = NULL;
u32 *_csegmentSegmentEnd         = NULL;
u32 *_cdataSegmentRomStart       = NULL;
u32 *_cdataSegmentRomEnd         = NULL;

u32 *_inflateSegmentVaddrStart   = NULL;
u32 *_inflateSegmentVaddrEnd     = NULL;
u32 *_inflateSegmentRomStart     = NULL;
u32 *_inflateSegmentRomEnd       = NULL;

u32 *_gameSegmentVaddrStart      = NULL;
u32 *_gameSegmentVaddrEnd        = NULL;
u32 *_gameSegmentRomStart        = NULL;
u32 *_gameSegmentRomEnd          = NULL;

u32 *_animation_dataSegmentRomStart = NULL;
u32 *_animation_dataSegmentStart    = NULL;
u32 *_animation_dataSegmentEnd      = NULL;
u32 *_animation_entriesSegmentRomStart = NULL;

u32 *_alt_startSegmentRomStart   = NULL;
u32 *_alt_startSegmentStart      = NULL;

/* Font / image / music segment bases (referenced by textrelated.c,
 * image_bank.c, music.c — data for the font/image ones is compiled from
 * assets/; the ROM-base markers themselves come from ge007.ld on N64). */
u32 *_efontchardataSegmentRomStart   = NULL;
u32 *_jfontchardataSegmentRomStart   = NULL;
u32 *_fontbankgothicSegmentStart     = NULL;
u32 *_fontbankgothicSegmentEnd       = NULL;
u32 *_fontbankgothicSegmentRomStart  = NULL;
u32 *_fontzurichboldSegmentStart     = NULL;
u32 *_fontzurichboldSegmentEnd       = NULL;
u32 *_fontzurichboldSegmentRomStart  = NULL;
u32 *_fontdlSegmentRomStart          = NULL;
u32 *_fontdlSegmentRomEnd            = NULL;
u32 *_imagesSegmentRomStart          = NULL;
u32 *_GlobalimagetableSegmentStart   = NULL;
u32 *_GlobalimagetableSegmentEnd     = NULL;
u32 *_GlobalimagetableSegmentRomStart = NULL;
u32 *_instrumentsctlSegmentRomStart  = NULL;
u32 *_instrumentstblSegmentRomStart  = NULL;
u32 *_musicsampletblSegmentRomStart  = NULL;
u32 *_sfxctlSegmentRomStart          = NULL;
u32 *_sfxtblSegmentRomStart          = NULL;
u32 *_rarewarelogoSegmentStart       = NULL;
u32 *_rarewarelogoSegmentEnd         = NULL;
u32 *_rarewarelogoSegmentRomStart    = NULL;

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
 * bossInitMainthreadData(); inert on the PC.
 */
void tlbmanageEstablishManagementTable(void) { /* no-op */ }
void tlbmanageResetCurrentEntriesCount(void)  { /* no-op */ }
void tlbmanageTranslateLoadRomFromTlbAddress(u32 address)
{
    (void)address;
}
u8 (*tlbmanageGetTlbAllocatedBlock(void))[TLB_BLOCK_SIZE] { return NULL; }

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
}
