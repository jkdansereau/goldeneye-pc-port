/* cvbytes.h -- big-endian reads / little-endian writes for the converters. */
#ifndef CVBYTES_H
#define CVBYTES_H
#include <stddef.h>
#include <stdint.h>

#define CV_MASK 0xFFFFFFu

static inline uint32_t cv_be32(const uint8_t *b, size_t o)
{
    return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) |
           ((uint32_t)b[o + 2] << 8) | b[o + 3];
}
static inline uint32_t cv_bu16(const uint8_t *b, size_t o)
{
    return ((uint32_t)b[o] << 8) | b[o + 1];
}
static inline int32_t cv_bs16(const uint8_t *b, size_t o)
{
    return (int16_t)cv_bu16(b, o);
}
static inline void cv_le32(uint8_t *b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8);
    b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24);
}
static inline void cv_le16(uint8_t *b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8);
}
#endif
