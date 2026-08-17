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

#include <PR/ultratypes.h>

/* --- Segment start/end getters (normally linker-script symbols) --------- */
/* On the PC the ROM is loaded by romdata.c; these are unused. Return 0.   */

u32 get_csegmentSegmentStart(void)   { return 0; }
u32 get_cdataSegmentRomStart(void)   { return 0; }
u32 get_cdataSegmentRomEnd(void)     { return 0; }
u32 get_inflateSegmentRomStart(void) { return 0; }
u32 get_inflateSegmentRomEnd(void)   { return 0; }

/*
 * Linker segment boundary pointers (normally linker-script symbols, declared
 * `extern u32 *` in bondgame.h). init() takes their ADDRESSES for a size calc
 * and a PI DMA; it is compiled but never called on the PC. Dummy pointers so
 * the symbols exist and link.
 */
u32 *_codeSegmentRomStart      = NULL;
u32 *_inflateSegmentRomStart   = NULL;
u32 *_alt_startSegmentRomStart = NULL;
u32 *_alt_startSegmentStart    = NULL;

/* --- Decompressor (src/inflate, not built for PC) ----------------------- */
/* init() calls this to unpack the data segment. Unused on the PC.         */
u32 jump_decompressfile(u32 source, u32 target, u32 buffer)
{
    (void)source; (void)target; (void)buffer;
    return 0;
}

/* --- TLB (src/tlb_*.s, not built for PC) -------------------------------- */
/* The PC has its own MMU; the N64 TLB miss handler is irrelevant.         */
void initTLBPrepareContext(void) { /* no-op */ }

/* Address of the N64 TLB-miss handler. init() copies it to K0BASE; unused. */
void resolve_TLBaddress_for_InvalidHit(void) { /* no-op */ }

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
