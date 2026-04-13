#ifndef _RLE_H_
#define _RLE_H_

#include <ultra64.h>

void rle_expand_8bit(void *src, u8 *dst);
void rle_expand_rgb_to_u16_5551(void *src, u16 *dst);
void rle_expand_rgb_to_rgba32(void *src, u8 *dst)
#endif
