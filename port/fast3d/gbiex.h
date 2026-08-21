/*
 * GE adaptation header for fast3d (see docs/PCPortResearch.md).
 *
 * fast3d was written against Perfect Dark's extended GBI set
 * (pd_port/src/include/gbiex.h + PD's include/PR/gbi.h). GoldenEye shares the
 * same Rare "Indy" engine family but its extension header is
 * include/gbi_extension.h, which defines G_TRI4 (0xb1) and GE's custom
 * combine/RM modes — but NOT the PD-specific opcodes below.
 *
 * GE never emits the PD-only opcodes (its RSP ucode is rsp/graphics/gmain.s,
 * which has no handling for them), so the corresponding dispatch cases in
 * gfx_pc.cpp are dead code for this port; they only need to COMPILE. The
 * values mirror PD's gbiex.h exactly so that if a ROM ever does emit one,
 * behaviour matches the sibling port rather than hitting the fatal "unknown
 * opcode" path.
 *
 * G_COL (7) is "new in PD": GE fills vertex colours directly in Vtx.cn[]
 * and its ucode has no colour-table DMA, so that case is dead here too.
 */
#ifndef FAST3D_GBIEX_H
#define FAST3D_GBIEX_H

#include <PR/gbi.h>
#include "include/gbi_extension.h" /* G_TRI4 (0xb1), GE's own extensions */

/* PD extension opcodes (pd_port/src/include/gbiex.h). */
#define G_COL                      7   /* vertex colour DMA — PD-only */
#define G_SETFB_EXT                0x21
#define G_SETTIMG_FB_EXT           0x23
#define G_INVALTEXCACHE_EXT        0x34
#define G_TEXRECT_WIDE_EXT         0x37
#define G_FILLRECT_WIDE_EXT        0x38
#define G_SETGRAYSCALE_EXT         0x39
#define G_EXTRAGEOMETRYMODE_EXT    0x3a
#define G_SETINTENSITY_EXT         0x40
#define G_COPYFB_EXT               0x41
#define G_IMAGERECT_EXT            0x42
#define G_RDPFLUSH_EXT             0x43
#define G_CLEAR_DEPTH_EXT          0x44
#define G_SETSUBPIXELOFFSET_EXT    0x45

/* Extra geometry/aspect bits used by the PD ucode (dead for GE). */
#define G_ASPECT_LEFT_EXT          0x00000010
#define G_ASPECT_RIGHT_EXT         0x00000020
#define G_ASPECT_WIDE_EXT          0x00000040
#define G_ASPECT_CENTER_EXT        (G_ASPECT_LEFT_EXT | G_ASPECT_RIGHT_EXT)
#define G_ASPECT_MODE_EXT          (G_ASPECT_CENTER_EXT | G_ASPECT_WIDE_EXT)
#define G_NO_CLIPPING_EXT          0x00000100
#define G_MODULATE_EXT             0x00000200
#define G_TF_BLUR_EXT              (1 << G_MDSFT_TEXTFILT)

/* Big-endian read helpers (pd_port/src/include/platform.h). The host is
 * little-endian, so a "BE" value in ROM data must be byte-swapped. */
#if defined(__GNUC__) || defined(__clang__)
#define PD_BSWAP16(x) __builtin_bswap16(x)
#define PD_BSWAP32(x) __builtin_bswap32(x)
#else
#error "Implement PD_BSWAP macros for your compiler."
#endif
#define PD_BE16(x) PD_BSWAP16(x)
#define PD_BE32(x) PD_BSWAP32(x)

#endif /* FAST3D_GBIEX_H */
