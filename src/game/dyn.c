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

    g_VtxBuffers[0] = mempAllocBytesInBank(g_VtxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = (g_VtxBuffers[0] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
    g_VtxBuffers[2] = (g_VtxBuffers[1] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);

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
#ifdef PORT
    /* TEMP D63: catch the gun-barrel sub-DL clobber with caller context. */
    if (getenv("GE_D63")) {
        static u32 s_last = 0;
        static int s_init = 0;
        static int s_changes = 0;
        const u32 *slot = (const u32 *)0x7012EC38;
        u32 cur = *slot;
        if (!s_init) { s_init = 1; s_last = cur; }
        else if (cur != s_last && s_changes < 64) {
            osSyncPrintf("D63 vtx-alloc clobber: word@0x7012ec38 %08x -> %08x caller=%p mempos=%p\n",
                         s_last, cur, __builtin_return_address(0), (void *)g_GfxMemPos);
            s_last = cur;
            ++s_changes;
        }
    }
#endif
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
    /* TEMP D63: log buffer bounds + bump position each frame */
    if (getenv("GE_D63")) {
        static int d63dyncount = 0;
        if ((d63dyncount++ % 100) == 0 || g_GfxMemPos > g_VtxBuffers[g_GfxActiveBufferIndex + 1]) {
            osSyncPrintf("D63 dynSwap #%d active=%d gfx=[%p..%p) vtx=[%p..%p) mempos=%p\n",
                         d63dyncount, (int)g_GfxActiveBufferIndex,
                         (void *)g_GfxBuffers[g_GfxActiveBufferIndex],
                         (void *)g_GfxBuffers[g_GfxActiveBufferIndex + 1],
                         (void *)g_VtxBuffers[g_GfxActiveBufferIndex],
                         (void *)g_VtxBuffers[g_GfxActiveBufferIndex + 1],
                         (void *)g_GfxMemPos);
            extern MemoryPool g_mempPools[];
            for (int b = 0; b < 8; b++) {
                if (g_mempPools[b].start || g_mempPools[b].end)
                    osSyncPrintf("D63   pool[%d] start=%p end=%p pos=%p prevpos=%p\n",
                                 b, (void *)g_mempPools[b].start, (void *)g_mempPools[b].end,
                                 (void *)g_mempPools[b].pos, (void *)g_mempPools[b].prevpos);
            }
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
