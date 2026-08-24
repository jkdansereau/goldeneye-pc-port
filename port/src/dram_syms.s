/*
 * dram_syms.s — absolute symbols inside the "virtual" (s32-safe) DRAM view.
 * See port/src/dram.c: one 8 MB backing store is mapped at BOTH 0x70000000
 * (V1, where these live) and 0x80000000 (V2, the KSEG0 mirror). These are
 * committed host addresses, so they are live pointers on the PC.
 *
 * cfb_16        replaces src/cfb.c (excluded from the build): the two
 *               320x240x16-bit framebuffers, 0x4B000 bytes total.
 * _bssSegmentEnd  end-of-.bss marker; boss.c:217 starts the mempools at
 *               PHYS_TO_K0(osVirtualToPhysical(&_bssSegmentEnd)).
 */
.section .data

.global cfb_16
.set cfb_16, 0x70000000

.global _bssSegmentEnd
.set _bssSegmentEnd, 0x70050000

/* D59: animation-frame scratch buffer (src/game/initanitable.c). The game
 * addresses it through s32 fields (Model.unk34/38/64/68) and loadAnimationFrame
 * computes its address with a (u32) cast, so on PC it must live below 4 GiB;
 * the BSS copy at 0x140xxxxxx truncates to wild 0x40xxxxxx. Parked at the top
 * of DRAM V1 (above the mempool sentinel at +0x2F4400). */
.global animations_frame_buffer
.set animations_frame_buffer, 0x707FFD30
