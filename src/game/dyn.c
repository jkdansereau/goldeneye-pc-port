#include <ultra64.h>
#include "dyn.h"
#include <token.h>
#include <str.h>
#include <memp.h>
#include <macro.h>

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx
 * argument. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

u8 *g_GfxBuffers[3];
u8 *g_VtxBuffers[3];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
s32 g_GfxRequestedDisplayList;
s32 D_800482E0 = 0;
s32 g_GfxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
s32 g_VtxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};

#ifdef PORT
/* D500: see dynInitMemory. Two 384 KB halves (~10x Aztec's budget, ~4x the
 * multiplayer maps') in the unused top of emulated DRAM: [+0x700000,
 * +0x7C0000), above the mempool end (D95, n64stubs.c) and below
 * animations_frame_buffer (+0x7FFD30, dram_syms.S). */
#include <portaddr.h>
#define PORT_VTX_HALF 0x60000
#define PORT_VTX_POOL ((u8 *)(uintptr_t)(PORT_DRAM_V1_BASE + 0x700000UL))
static s32 s_portVtxN64Budget;
static s32 s_portVtxWarned;   /* 1 = past the N64 budget logged, 2 = past the port pool logged */
#endif

char membars_string1[] = ">>>>>>>>>>>>>>>>>>>>>>>>>";
char membars_string2[] = "=========================";
char membars_string3[] = "-------------------------";

void dynInit(void) {
    debTryAdd(&D_800482E0, "dyn_c_debug");
}

void dynInitMemory(void) {
    if (tokenFind(1, "-mgfx")) {
        g_GfxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mgfx"), NULL, 0) * 1024;
    }
    if (tokenFind(1, "-mvtx")) {
        g_VtxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mvtx"), NULL, 0) * 1024;
    }

#ifdef PORT
    /* D95: the -mgfx budget (from boss.c's per-level memallocstringtable) is a
     * byte count sized for N64 8-byte `Gfx` slots. On x86-64 a `Gfx` is 16
     * bytes, so the same master display list needs 2x the bytes -- otherwise
     * `gdl` (bumped with a bare `gdl++` by every render fn, no bounds check)
     * marches past g_GfxBuffers[1]/[2], off the stage mempool, and eventually
     * faults at the end of the 8 MB emulated DRAM (0x70800000) while writing a
     * GBI command. Scale by sizeof(Gfx)/8. Vtx/Mtx are 16/64 bytes on both
     * targets, so g_VtxBuffers is left alone. */
    {
        s32 gfxHalf = g_GfxSizesByPlayerCount[getPlayerCount() - 1] * ((s32)sizeof(Gfx) / 8);
        g_GfxBuffers[0] = mempAllocBytesInBank(gfxHalf * 2, MEMPOOL_STAGE);
        g_GfxBuffers[1] = (g_GfxBuffers[0] + gfxHalf);
        g_GfxBuffers[2] = (g_GfxBuffers[1] + gfxHalf);
    }
#else
    g_GfxBuffers[0] = mempAllocBytesInBank(g_GfxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_GfxBuffers[1] = (g_GfxBuffers[0] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);
    g_GfxBuffers[2] = (g_GfxBuffers[1] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);
#endif

#ifdef PORT
    /* D500: the vtx pool (per-frame vertices, matrices, lights; bump-allocated
     * with no bounds check) is sized per level for what the N64 could see,
     * as low as -mvtx40 (Aztec, Streets). The port's wider FOV, draw distance
     * and LOD put more models in view, so a busy frame could run past its
     * half into the other half, which the render worker may still be drawing
     * (triangles "falling apart", Aztec shuttle room, 2026-10-03). Use a
     * fixed, much larger pool in the free top of emulated DRAM instead of
     * the stage bank (tight, see D294). The addresses stay 0x70xxxxxx, so
     * they survive every s32 path and osVirtualToPhysical like any other
     * game RAM pointer. */
    g_VtxBuffers[0] = PORT_VTX_POOL;
    g_VtxBuffers[1] = (g_VtxBuffers[0] + PORT_VTX_HALF);
    g_VtxBuffers[2] = (g_VtxBuffers[1] + PORT_VTX_HALF);
    s_portVtxN64Budget = g_VtxSizesByPlayerCount[getPlayerCount() - 1];
    s_portVtxWarned = 0;
#else
    g_VtxBuffers[0] = mempAllocBytesInBank(g_VtxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = (g_VtxBuffers[0] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
    g_VtxBuffers[2] = (g_VtxBuffers[1] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
#endif

    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[0];
}

Gfx *dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;

    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex];
}

s32 dynGetFreeGfx2(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

/**
 * Address: 7F0BD6C4
 */
Vtx *dynAllocateVertices(s32 count) 
{
    void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * sizeof(Vtx);
	return ptr;
}

Mtx *dynAllocateMatrix(void)
{
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += sizeof(Mtx);
	return ptr;
}

/**
 * Address: 7F0BD6F8
 */
Light *dynAllocateLights(s32 count)
{
    void *ptr = g_GfxMemPos;
    g_GfxMemPos += count * sizeof(Light);
    return ptr;
}

void *dynAllocate(s32 size) {
    void *ptr = g_GfxMemPos;
	size = ALIGN16_a(size);
	g_GfxMemPos += size;
	return ptr;
}

void dynSwapBuffers(void) {
#ifdef PORT
    {
        /* D500: report (once per level each) a frame that used more than the
         * N64 budget, and one that overran even the port pool. */
        s32 used = (s32)(g_GfxMemPos - g_VtxBuffers[g_GfxActiveBufferIndex]);
        if (used > PORT_VTX_HALF && s_portVtxWarned < 2) {
            s_portVtxWarned = 2;
            osSyncPrintf("D500: vtx pool OVERRUN: frame used %d bytes, port pool %d\n", used, PORT_VTX_HALF);
        } else if (used > s_portVtxN64Budget && s_portVtxWarned < 1) {
            s_portVtxWarned = 1;
            osSyncPrintf("D500: vtx pool: frame used %d bytes, N64 budget %d (port pool %d)\n",
                         used, s_portVtxN64Budget, PORT_VTX_HALF);
        }
    }
#endif
    g_GfxActiveBufferIndex = (g_GfxActiveBufferIndex ^ 1);
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
}

void dynRemovedFunc(Gfx *gdl) {
}

s32 dynGetFreeGfx(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

s32 dynGetFreeVtx(void) {
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Address 0x7F0BD7CC NTSC
void dynCalculateMembarLength(const char* arg0, f32 arg1, f32 arg2)
{
    s32 len;
    f32 zero = 0;
    
    len = strlen(arg0);
    
    arg1 /= arg2;
    
    if(zero);
    
    if (arg1 < zero && len > 1)
    {
        if (len > 1)
        {
            
        }
    }
}

void dynDrawMembars(Gfx *gdl) {
    dynCalculateMembarLength(membars_string2, ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl), ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex]));
    dynCalculateMembarLength(membars_string2, (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos), (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex]));
}
