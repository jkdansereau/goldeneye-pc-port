/*
 * RSP/ASP/GSP microcode segment markers.
 *
 * On the N64 these are linker-defined addresses of the RSP/ASP/GSP microcode
 * embedded in the ROM (see src/rspboot.s, src/gspboot.s, src/aspboot.s — the
 * .s files are not compiled for the PC). The game references them to build
 * SP tasks:
 *
 *   src/audi.c:  ucode_boot = (u64*)rspbootTextStart
 *                ucode      = (u64*)aspMainTextStart
 *                ucode_data = (u64*)aspMainDataStart
 *   src/game/rsp.c: ucode_boot_size = (s32)gsp3DTextStart - (s32)rspbootTextStart
 *
 * The game declares them as `extern long long int <name>[];`.
 *
 * STATUS: dummy definitions so the link succeeds.
 *
 *   - GRAPHICS (Phase 2): fast3d interprets the GBI display list directly and
 *     never executes the gsp3D microcode, so gsp3DTextStart/gsp3DDataStart are
 *     unused metadata. The ucode_boot_size differences compute to garbage but
 *     are never read on the graphics path. Dummies are fine permanently here.
 *
 *   - AUDIO (Phase 3): this is the one place it could matter. If the port
 *     emulates the ASP by actually RUNNING the aspMain microcode, then
 *     aspMainTextStart/aspMainDataStart must point at the real microcode bytes
 *     in the loaded ROM (follow the PD port's ld/pd.ld RSP_TEXT_SEGMENT model).
 *     If audio is handled entirely on the CPU (bypassing the ASP), these
 *     dummies are fine permanently. DECISION NEEDED in Phase 3.
 *
 * TODO(Phase 2/3): wire these to the real microcode in the ROM image if the
 * corresponding path executes microcode.
 */

/* RSP boot microcode (src/rspboot.s). */
long long int rspbootTextStart[1] = {0};
long long int rspbootTextEnd[1]   = {0};

/* GSP 3D microcode (src/gspboot.s). Unused by fast3d (see note above). */
long long int gsp3DTextStart[1] = {0};
long long int gsp3DDataStart[1] = {0};

/*
 * ASP audio microcode (src/aspboot.s).
 * TODO(Phase 3): if the port runs the ASP microcode, point these at the real
 * microcode bytes in the ROM (see file header note).
 */
long long int aspMainTextStart[1] = {0};
long long int aspMainDataStart[1] = {0};
