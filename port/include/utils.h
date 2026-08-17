#ifndef PORT_UTILS_H
#define PORT_UTILS_H

/*
 * Small shared helpers for the port layer.
 * Modelled on the PD port's port/include/utils.h.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARRAYCOUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Read a big-endian 16/32-bit value from an (possibly unaligned) buffer. */
uint16_t readU16BE(const void *p);
uint32_t readU32BE(const void *p);
void     writeU16BE(void *p, uint16_t v);
void     writeU32BE(void *p, uint32_t v);

/* Clamp helpers. */
int   clampi(int v, int lo, int hi);
float clampf(float v, float lo, float hi);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UTILS_H */
