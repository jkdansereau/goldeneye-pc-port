#include <ultra64.h>
#include <deb.h>
#include "memp.h"
#include "game/language.h"

/**
 * EU .data, offset from start of data_seg : 0x3640
*/

/**
 * @file memp.c
 * This file contains code for memp.
 */

//bss
MemoryPool g_mempPools[MEMPOOL_COUNT];

//data
void *ptr_memp_c_debug_debug_notice_list = 0;
s32 needmemallocation = 0;
s32 D_80024408 = 0;
s32 D_8002440C = 0;
s32 D_80024410 = 0;

//overloaded
struct s_mempMVALS sdefaultmvals = {
    MEMPOOL_MF + 1,    0,  // MEMPOOL_MF
    MEMPOOL_ML + 1,    82, // MEMPOOL_ML
    MEMPOOL_ME + 1,    15, // MEMPOOL_ME
    0,                 0   // MEMPOOL_END
};

void mempInit(void)
{
    debTryAdd(&ptr_memp_c_debug_debug_notice_list, "memp_c_debug");
}

const char *tokenFind(s32 arg0, const char *arg1);
long int strtol(const char *str, char **endptr, int base);
#ifdef PORT
/* D453: arena start is a host pointer; carry it in uintptr_t, not s32. */
void mempCheckMemflagTokens(uintptr_t poolAreaStart, s32 poolAreaSize)
#else
void mempCheckMemflagTokens(s32 poolAreaStart, s32 poolAreaSize)
#endif
{
    s_mempMVALS poolSizes;

    //set pool 0 to what boss wants (room_model_buffer)
    //pool 0 = TotalPoolArea
#ifdef PORT
    g_mempPools[MEMPOOL_TOTAL].start = (u8 *)poolAreaStart;
    g_mempPools[MEMPOOL_TOTAL].end = (u8 *)(poolAreaStart + poolAreaSize);
#else
    g_mempPools[MEMPOOL_TOTAL].start = poolAreaStart;
    g_mempPools[MEMPOOL_TOTAL].end = poolAreaStart + poolAreaSize;
#endif

    poolSizes = sdefaultmvals;

    if (tokenFind(1, "-mf"))
    {
        poolSizes.mf = strtol(tokenFind(1, "-mf"), NULL, 0);
    }
    if (tokenFind(1, "-ml"))
    {
        poolSizes.ml = strtol(tokenFind(1, "-ml"), NULL, 0);
    }
    if (tokenFind(1, "-me"))
    {
        poolSizes.me = strtol(tokenFind(1, "-me"), NULL, 0);
    }
    if (poolSizes.me == 0)
    {
        poolSizes.mf = 0;
#if defined(__x86_64__)
        /* D36 (PC port): the PERMANENT bank must also hold the enlarged
         * music heap (MUSIC_ALLOCATION_BYTES in src/music.c, 0x2E000 ->
         * 0x32000 on x86-64 due to libaudio pointer bloat). Grow the bank on
         * PC only; STAGE absorbs the difference. N64 value unchanged.
         * See docs/dev/findings.md D36.
         *
         * D37 (PC port): MUSIC_ALLOCATION_BYTES grows again to 0x38000 (the
         * re-laid-out bank images live in the music heap; init demand
         * measures 0x33530). Pre-music PERMANENT usage is ~0x1BCA0, so the
         * bank must be at least 0x1BCA0 + 0x38000 plus post-music headroom;
         * 352/368 KiB leaves ~17-19 KiB. STAGE absorbs the difference. */
        poolSizes.me = ((j_text_trigger ? 368 : 352) * 1024);
#else
        poolSizes.me = ((j_text_trigger ? 308 : 296) * 1024);
#endif
        poolSizes.ml = poolAreaSize - poolSizes.me;
    }

    mempSetBankStarts((s32*)&poolSizes);
}

void mempSetBankStarts(s32 poolSizes[MEMPOOL_COUNT+1])
{
    s32 i;
    s32 bankstarts[MEMPOOL_COUNT] = {0};
    s32 mempLen;
    s32 mempRequested;
#ifdef PORT
    uintptr_t mempStart; /* D453 */
#else
    s32 mempStart;
#endif

    //set MF, ML, ME first
    i = 0;
    do
    {
        // assign the "xxxIndex" the value of xxx+1 then skip "Indices", 0=2=mf, 2=4=ml, 4=6=me, 6=8=end
        bankstarts[poolSizes[i]] = poolSizes[i+1];
        i += 2;
    } while (poolSizes[i] != 0); //while sizes not = 0 (bank 7 = 0)
    //  0 1 2 3            4           5     6
    // {0,0,0,0,poolAreaSize - 303104, 0, 303104}

    //for each bankstart, add current to next
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT - 1; i++)
    {
        bankstarts[i + 1] += bankstarts[i];
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}


    mempRequested = bankstarts[MEMPOOL_COUNT - 1]; //total accumulated size of banks = poolAreaSize
    mempLen  = (g_mempPools[MEMPOOL_TOTAL].end - g_mempPools[MEMPOOL_TOTAL].start);

    //for each bankstart, multiply by total pool size, then divide by size of banks 1-7
    //spread each bank evenly
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT; i++)
    {
        bankstarts[i] = ((s64)bankstarts[i] * mempLen) / mempRequested;
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}

    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT; i++)
    {
        bankstarts[i] = ALIGN16_b(bankstarts[i]);
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}


#ifdef PORT
    mempStart = (uintptr_t)g_mempPools[MEMPOOL_TOTAL].start;
#else
    mempStart = g_mempPools[MEMPOOL_TOTAL].start;
#endif
    //for each bank 1-7, add new start position
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT - 1; i++)
    {
#ifdef PORT
        g_mempPools[i + 1].start = (u8 *)(bankstarts[i] + mempStart);
        g_mempPools[i + 1].pos   = 0;
        g_mempPools[i + 1].end   = (u8 *)(bankstarts[i + 1] + mempStart);
#else
        g_mempPools[i + 1].start = bankstarts[i] + mempStart;
        g_mempPools[i + 1].pos   = 0;
        g_mempPools[i + 1].end   = bankstarts[i + 1] + mempStart;
#endif
    }
    /*
                           rel-start              size
    g_memPools[TOTAL]      0                      poolArea
    g_memPools[MF]         0                      0
    g_memPools[2]          0                      0
    g_memPools[ML]         0                      0
    g_memPools[STAGE]      0                      poolAreaSize - 303104
    g_memPools[ME]         poolAreaSize - 303104  0
    g_memPools[PERMANENT]  poolAreaSize - 303104  303104
    */
}


#ifdef PORT
static void *mempAllocBytesInBankRaw(u32 bytes, u8 poolnum)
#else
void *mempAllocBytesInBank(u32 bytes, u8 poolnum)
#endif
{
    /*
     * Retain this address expression. Using
     * &g_mempPools[poolnum] changes regalloc.
     */
    MemoryPool *pool = (MemoryPool *)(((u8 **)g_mempPools) + ((poolnum * 2) << 1));
    u8 *allocation = pool->pos;

#ifdef DEBUG
    if ((poolnum < 0) || (4 < poolnum))
    {
        osSyncPrintf("mempAllocBytesInBank from invalid heap %d!", poolnum);
    }
#endif

    if (pool->pos == NULL)
    {
        while (1);
    }

    if (pool->pos > pool->end)
    {
        nulled_mempLoopAllMemBanks();

        while (1);
    }

    if (pool->pos + bytes > pool->end)
    {
        if (g_mempPools[MEMPOOL_PERMANENT].pos + bytes <= g_mempPools[MEMPOOL_PERMANENT].end)
        {
            /*
             * There was probably debug code in the original that got mostly
             * stripped, but it still perturbs register allocation. These
             * statements fill t1/t3/t4 so the registers match.
             */
            if (needmemallocation);
            if (&D_8002440C == &D_80024408);
            if (needmemallocation);
            if (&D_80024410 == &D_80024408);
            if (!needmemallocation);

            needmemallocation = TRUE;

#ifdef PORT
            return mempAllocBytesInBankRaw(bytes, MEMPOOL_PERMANENT);
#else
            return mempAllocBytesInBank(bytes, MEMPOOL_PERMANENT);
#endif
        }

        nulled_mempLoopAllMemBanks();

        while (1);
    }

    pool->pos += bytes;
    pool->prevpos = allocation;

    if (needmemallocation);

    return allocation;
}


#ifdef PORT
/* D464: GE_MEMPREDZONE=1 -- red-zone checker for the bump allocator.
 * Every allocation that has room is padded with MEMP_RZ_SIZE trailing
 * pattern bytes and recorded in a side table; mempRedzoneCheck() reports
 * blocks whose trailer was scribbled (an overrun: N64-sized allocation
 * indexed at the larger PC stride, the D461/D462 class). The returned
 * pointer, 16-byte alignment and pool bookkeeping are otherwise unchanged
 * (pos just advances RZ more, a multiple of 16). With the flag unset, the
 * only cost is one cached branch per allocation. Game-thread design; the
 * periodic check from the video hook can race a resize and report a
 * transient false positive, so confirm a hit by re-running. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "envflag.h"
#include "system.h"

#define MEMP_RZ_SIZE   64
#define MEMP_RZ_MAX    8192

typedef struct MempRzEntry {
    u8 *block;
    u8 *rz;
    u32 size;
    u32 id;
    void *caller;
    u8 bank;
    u8 reported;
} MempRzEntry;

static MempRzEntry s_rz[MEMP_RZ_MAX];
static u32 s_rzCount;
static u32 s_rzNextId;
static u32 s_rzNoRoom;   /* allocations that had no room for a red zone */
static int s_rzFullSaid;

static u8 mempRzPat(u32 i) { return (u8)(0xA5u + i * 13u); }

static void mempRzFill(u8 *rz)
{
    u32 i;
    for (i = 0; i < MEMP_RZ_SIZE; i++) rz[i] = mempRzPat(i);
}

static int mempRzBankOf(u8 *p)
{
    int i;
    for (i = MEMPOOL_MF; i < MEMPOOL_COUNT; i++) {
        if (g_mempPools[i].start != g_mempPools[i].end &&
            p >= g_mempPools[i].start && p < g_mempPools[i].end) return i;
    }
    return -1;
}

static void mempRzRecord(u8 *block, u32 size, void *caller)
{
    MempRzEntry *e;
    u32 id = s_rzNextId++;
    if (s_rzCount >= MEMP_RZ_MAX) {
        if (!s_rzFullSaid) {
            s_rzFullSaid = 1;
            sysLogPrintf(LOG_WARNING, "MEMPREDZONE: side table full (%d); later allocations are padded but not tracked", MEMP_RZ_MAX);
        }
        return;
    }
    e = &s_rz[s_rzCount];
    e->block = block; e->rz = block + size; e->size = size; e->id = id;
    e->caller = caller; e->bank = (u8)mempRzBankOf(block); e->reported = 0;
    s_rzCount++;
}

static int mempRzFindLast(u8 *block)
{
    int i;
    for (i = (int)s_rzCount - 1; i >= 0; i--) {
        if (s_rz[i].block == block) return i;
    }
    return -1;
}

static void mempRzDropBank(u8 bank)
{
    u32 i, j = 0;
    for (i = 0; i < s_rzCount; i++) {
        if (s_rz[i].bank != bank) s_rz[j++] = s_rz[i];
    }
    s_rzCount = j;
}

void mempRedzoneCheck(const char *why)
{
    u32 i, k, nbad = 0;
    if (!GE_ENVFLAG("GE_MEMPREDZONE")) return;
    for (i = 0; i < s_rzCount; i++) {
        MempRzEntry *e = &s_rz[i];
        u32 first = 0xFFFFFFFFu, changed = 0;
        if (e->reported) continue;
        for (k = 0; k < MEMP_RZ_SIZE; k++) {
            if (e->rz[k] != mempRzPat(k)) {
                if (first == 0xFFFFFFFFu) first = k;
                changed++;
            }
        }
        if (changed) {
            e->reported = 1;
            nbad++;
            sysLogPrintf(LOG_ERROR,
                "MEMPREDZONE: HIT id=%u bank=%u size=%u block=%p rz_first_off=%u changed=%u/%d caller=%p (%s)",
                e->id, (unsigned)e->bank, e->size, (void *)e->block, first, changed, MEMP_RZ_SIZE, e->caller,
                why ? why : "?");
        }
    }
    sysLogPrintf(LOG_INFO, "MEMPREDZONE: check '%s': %u tracked, %u new hit(s), %u untracked(no room)",
                 why ? why : "?", s_rzCount, nbad, s_rzNoRoom);
}

static void *mempAllocImpl(u32 bytes, u8 poolnum, void *caller)
{
    MemoryPool *pool = &g_mempPools[poolnum];
    u8 *p;

    if (!GE_ENVFLAG("GE_MEMPREDZONE")) {
        return mempAllocBytesInBankRaw(bytes, poolnum);
    }
    if (pool->pos != NULL && pool->pos <= pool->end &&
        pool->pos + bytes + MEMP_RZ_SIZE <= pool->end) {
        p = (u8 *)mempAllocBytesInBankRaw(bytes + MEMP_RZ_SIZE, poolnum);
        mempRzFill(p + bytes);
        mempRzRecord(p, bytes, caller);
        return p;
    }
    s_rzNoRoom++;
    return mempAllocBytesInBankRaw(bytes, poolnum);
}

static void mempRzResized(u8 *block, u32 newsize, int idx)
{
    mempRzFill(block + newsize);
    if (idx >= 0) {
        s_rz[idx].size = newsize;
        s_rz[idx].rz = block + newsize;
        s_rz[idx].reported = 0;
    } else {
        mempRzRecord(block, newsize, __builtin_return_address(0));
    }
}

__attribute__((noinline)) void *mempAllocBytesInBank(u32 bytes, u8 poolnum)
{
    return mempAllocImpl(bytes, poolnum, __builtin_return_address(0));
}
#endif

/**
 * Resize the most recent allocation in a pool without moving it.
 */
MEMP_ADD_ENTRY_RESULT mempAddEntryOfSizeToBank(void *allocation, s32 newsize, u8 poolnum)
{
    MemoryPool *pool;
    s32 origsize;
    s32 growsize;
#ifdef PORT
    int rzIdx = -1;
    int rzNew = 0;
#endif

    if (needmemallocation && allocation == g_mempPools[MEMPOOL_PERMANENT].prevpos)
    {
        poolnum = MEMPOOL_PERMANENT;
    }

    allocation = (void *)(u64)allocation;
    pool = &g_mempPools[poolnum];

    if (pool->pos == 0)
    {
        while (TRUE);
    }

    if (allocation != pool->prevpos)
    {
        return MEMP_ADD_ENTRY_NOT_LAST_ALLOCATION;
    }

    origsize = pool->pos - pool->prevpos;
#ifdef PORT
    /* D464: a tracked block carries a trailer; keep it around the new size.
     * An untracked block (whole-bank allocation, shrunk right after the
     * load) gains one when the shrink leaves room for it. */
    if (GE_ENVFLAG("GE_MEMPREDZONE")) {
        rzIdx = mempRzFindLast((u8 *)allocation);
        if (rzIdx >= 0 || (origsize >= newsize + MEMP_RZ_SIZE)) {
            rzNew = 1;
        }
    }
    growsize = newsize + (rzNew ? MEMP_RZ_SIZE : 0) - origsize;
#else
    growsize = newsize - origsize;
#endif

    if (growsize <= 0)
    {
        pool->pos += growsize;
#ifdef PORT
        if (rzNew) mempRzResized((u8 *)allocation, newsize, rzIdx);
#endif
        return MEMP_ADD_ENTRY_SUCCESS;
    }

    if (pool->pos > pool->end)
    {
        nulled_mempLoopAllMemBanks();
        while (TRUE);
    }

    if (pool->pos + growsize > pool->end)
    {
        nulled_mempLoopAllMemBanks();
        while (TRUE);
    }

    pool->pos += growsize;
#ifdef PORT
    if (rzNew) mempRzResized((u8 *)allocation, newsize, rzIdx);
#endif
    return MEMP_ADD_ENTRY_SUCCESS;
}

void nulled_mempLoopAllMemBanks(void) {
    u8 bank;
    for (bank = MEMPOOL_MF; bank < MEMPOOL_COUNT; bank++)
    {
    }
}

s32 mempGetBankSizeLeft(u8 bank) {
    if (needmemallocation) {
        bank = MEMPOOL_PERMANENT;
    }

    if ((bank == MEMPOOL_STAGE) && (g_mempPools[MEMPOOL_STAGE].start == g_mempPools[MEMPOOL_STAGE].end))
    {
        bank = MEMPOOL_PERMANENT;
    }

    return g_mempPools[bank].end - g_mempPools[bank].pos;
}

// Last three bits contains the bank, the rest contains the size.
#ifdef PORT
__attribute__((noinline)) void *mempAllocPackedBytesInBank(u32 sizeandbank) { /* D453: was u32 */
    return mempAllocImpl((sizeandbank >> 3), (sizeandbank & 7), __builtin_return_address(0));
}
#else
u32 mempAllocPackedBytesInBank(u32 sizeandbank) {
    return mempAllocBytesInBank((sizeandbank >> 3), (sizeandbank & 7));
}
#endif

void mempResetBank(u8 bank) {
#ifdef PORT
    mempRedzoneCheck("mempResetBank");
    mempRzDropBank(bank);
#endif
    g_mempPools[bank].prevpos = 0;
    g_mempPools[bank].pos = g_mempPools[bank].start;
}

void mempNullNextEntryInBank(u8 bank) {
#ifdef PORT
    mempRedzoneCheck("mempNullNextEntryInBank");
    mempRzDropBank(bank);
#endif
    nulled_mempLoopAllMemBanks();
    if (g_mempPools[bank].pos != 0) {
        g_mempPools[bank].pos = 0;
    }
}
