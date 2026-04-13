#include <ultra64.h>



#ifdef NONMATCHING
void rle_expand_8bit(void *src, u8 *dst)
{
    u8 *p;
    u16 w;
    u16 h;
    u32 remaining;
    u8 *run;
    u8 count;
    u8 value;
    u32 i;

    p = (u8 *)src;
    w = *(u16 *)(p + 0);
    h = *(u16 *)(p + 2);
    remaining = (u32)w * (u32)h;
    run = p + 0xA;

    while (remaining > 0) {
        count = run[0];
        value = run[1];
        run += 2;
        remaining -= (u32)count;
        i = (u32)count;
        while (i > 0) {
            *dst++ = value;
            i--;
        }
    }
}
#else
GLOBAL_ASM(
.text
glabel rle_expand_8bit
/* 04FC10 7F01B0E0 94820000 */  lhu   $v0, ($a0)
/* 04FC14 7F01B0E4 94830002 */  lhu   $v1, 2($a0)
/* 04FC18 7F01B0E8 2487000A */  addiu $a3, $a0, 0xa
/* 04FC1C 7F01B0EC 00430019 */  multu $v0, $v1
/* 04FC20 7F01B0F0 00003012 */  mflo  $a2
/* 04FC24 7F01B0F4 00000000 */  nop   
/* 04FC28 7F01B0F8 00000000 */  nop   
/* 04FC2C 7F01B0FC 90E20000 */  lbu   $v0, ($a3)
.L7F01B100:
/* 04FC30 7F01B100 90E30001 */  lbu   $v1, 1($a3)
/* 04FC34 7F01B104 24E70002 */  addiu $a3, $a3, 2
/* 04FC38 7F01B108 00C23023 */  subu  $a2, $a2, $v0
/* 04FC3C 7F01B10C 2442FFFF */  addiu $v0, $v0, -1
.L7F01B110:
/* 04FC40 7F01B110 0002202A */  slt   $a0, $zero, $v0
/* 04FC44 7F01B114 2442FFFF */  addiu $v0, $v0, -1
/* 04FC48 7F01B118 A0A30000 */  sb    $v1, ($a1)
/* 04FC4C 7F01B11C 1480FFFC */  bnez  $a0, .L7F01B110
/* 04FC50 7F01B120 24A50001 */   addiu $a1, $a1, 1
/* 04FC54 7F01B124 5CC0FFF6 */  bgtzl $a2, .L7F01B100
/* 04FC58 7F01B128 90E20000 */   lbu   $v0, ($a3)
/* 04FC5C 7F01B12C 03E00008 */  jr    $ra
/* 04FC60 7F01B130 00000000 */   nop   
)
#endif


/**
 * Decode RLE-compressed RGB data into RGBA5551 pixels.
 */
void rle_expand_rgb_to_u16_5551(u8 *src, u16 *dst)
{
    u8 *in;
    s32 remaining;
    s32 count;
    u8 r, g, b;
    u16 w, h;
    s32 pixel;

    w = *(u16 *)&src[0];
    h = *(u16 *)&src[2];
    remaining = w * h;
    in = &src[10];

    do {
        count = in[0];
        r = in[1];
        g = in[2];
        b = in[3];
        pixel = ((b >> 3) << 11) | ((g >> 3) << 6) | ((r >> 3) << 1) | 1;
        remaining -= count;
        in += 4;
        count--;

        do {
            *dst++ = pixel;
        } while (count-- > 0);
    } while (remaining > 0);
}

/**
 * Decode RLE-compressed RGB data into RGBA32 pixels.
 */
void rle_expand_rgb_to_rgba32(u8 *src, u8 *dst)
{
    u8 *in;
    u8 *out;
    s32 remaining;
    s32 count;
    u8 r, g, b;
    u16 w, h;
    
    out = dst;
    w = *(u16 *)&src[0];
    h = *(u16 *)&src[2];
    remaining = w * h;
    in = &src[10]; // Start of RLE stream.

    do {
        count = in[0];
        r = in[1];
        g = in[2];
        remaining -= count;
        b = in[3];
        in += 4;
        count--;
        do {
            out[0] = b;
            out[1] = g;
            out[2] = r;
            out[3] = 0xFF; 
            out += 4;
        } while (count-- > 0);
    } while (remaining > 0);
}