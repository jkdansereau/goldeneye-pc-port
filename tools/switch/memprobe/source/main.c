/*
 * memprobe — project-dam feasibility probe (docs/switch/address-model.md).
 *
 * Question: can the Switch host the port's N64 address window, i.e. place
 * three blocks at host addresses whose LOW 32 BITS equal their N64 address
 * (so `(u32)ptr` round-trips, the invariant of port/include/portaddr.h)?
 *
 *   cart   N64 0x10000000  (ROM + converted sidecars)
 *   dram   N64 0x70000000  (8 MB arena; KSEG0 0x80000000 folded onto it in software)
 *   stack  N64 0xA0000000  (game-thread stacks)
 *
 * For each process region (stack / alias / ASLR) it looks for 4 GiB-aligned
 * bases H where H+off is free, both as ONE base for all blocks and per block,
 * then actually maps memory there three ways and checks write/read + the
 * (u32) round trip:
 *   A. svcMapMemory          heap -> stack region
 *   B. svcMapPhysicalMemory  fresh memory -> alias region
 *   C. svcMapProcessCodeMemory + svcSetProcessMemoryPermission(RW)
 *                            heap -> ASLR/code region (libnx JIT technique)
 *
 * Everything is mapped, tested and unmapped again; results go to the screen
 * and to sdmc:/switch/memprobe.txt. Press + to exit.
 */
#include <switch.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIB4 0x100000000ULL

static FILE *g_log;

static void P(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

typedef struct {
    const char *name;
    u64 off;
    u64 size;
} Zone;

static const Zone kZones[] = {
    { "cart ", 0x10000000ULL, 32ULL << 20 },
    { "dram ", 0x70000000ULL,  8ULL << 20 },
    { "stack", 0xA0000000ULL, 16ULL << 20 },
};
#define NZ (sizeof(kZones) / sizeof(kZones[0]))

typedef struct {
    const char *name;
    u64 base, size;
} Region;

static Region g_heap, g_alias, g_stack, g_aslr;

static u64 info(u32 id)
{
    u64 v = 0;
    Result rc = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0);
    return R_FAILED(rc) ? ~0ULL : v;
}

static bool overlaps(u64 a, u64 s, const Region *r)
{
    return a < r->base + r->size && r->base < a + s;
}

static bool insideRegion(u64 a, u64 s, const Region *r)
{
    return a >= r->base && a + s <= r->base + r->size;
}

/* For the ASLR region, code memory must avoid the heap/alias/stack regions. */
static bool allowed(u64 a, u64 s, const Region *r)
{
    if (!insideRegion(a, s, r)) {
        return false;
    }
    if (r == &g_aslr) {
        if (overlaps(a, s, &g_heap) || overlaps(a, s, &g_alias) || overlaps(a, s, &g_stack)) {
            return false;
        }
    }
    return true;
}

static bool unmapped(u64 a, u64 s)
{
    while (s) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, a)) || mi.type != MemType_Unmapped) {
            return false;
        }
        u64 end = mi.addr + mi.size;
        if (end >= a + s) {
            return true;
        }
        s -= end - a;
        a = end;
    }
    return true;
}

static bool zoneFits(u64 H, unsigned z, const Region *r)
{
    u64 a = H + kZones[z].off;
    return allowed(a, kZones[z].size, r) && unmapped(a, kZones[z].size);
}

/* Lowest 4 GiB-aligned H where every zone fits; returns the count found. */
static int findSingle(const Region *r, u64 *first)
{
    int n = 0;
    *first = 0;
    for (u64 H = (r->base / GIB4) * GIB4; H < r->base + r->size; H += GIB4) {
        bool ok = true;
        for (unsigned z = 0; z < NZ && ok; z++) {
            ok = zoneFits(H, z, r);
        }
        if (ok) {
            if (!n) {
                *first = H;
            }
            n++;
        }
    }
    return n;
}

static int findZone(const Region *r, unsigned z, u64 *first)
{
    int n = 0;
    *first = 0;
    for (u64 H = (r->base / GIB4) * GIB4; H < r->base + r->size; H += GIB4) {
        if (zoneFits(H, z, r)) {
            if (!n) {
                *first = H;
            }
            n++;
        }
    }
    return n;
}

static void survey(const Region *r)
{
    u64 h;
    int n = findSingle(r, &h);
    P("  %-6s single base: %3d candidate(s)%s", r->name, n, n ? "" : "\n");
    if (n) {
        P(", first H=0x%010lx\n", h);
    }
    for (unsigned z = 0; z < NZ; z++) {
        n = findZone(r, z, &h);
        P("         %s alone : %3d candidate(s)", kZones[z].name, n);
        if (n) {
            P(", first H=0x%010lx", h);
        }
        P("\n");
    }
}

/* Write a pattern at both ends, read it back, check the (u32) round trip. */
static bool testBlock(u64 addr, unsigned z)
{
    volatile u32 *p = (volatile u32 *)addr;
    u64 words = kZones[z].size / 4;
    p[0] = 0xC0DE0000u | z;
    p[words - 1] = 0x007E0000u | z;
    bool rw = p[0] == (0xC0DE0000u | z) && p[words - 1] == (0x007E0000u | z);
    bool rt = (u32)(uintptr_t)addr == (u32)kZones[z].off;
    P("      %s @0x%010lx  rw=%s  (u32)ptr=0x%08x %s\n", kZones[z].name, addr, rw ? "OK" : "FAIL",
      (u32)(uintptr_t)addr, rt ? "OK" : "MISMATCH");
    return rw && rt;
}

/* Pick H for zone z in region r: the single base if there is one, else per
 * zone. H = 0 is a valid base (v1 of this probe treated it as "none"). */
static bool pickBase(const Region *r, unsigned z, bool haveSingle, u64 single, u64 *H)
{
    if (haveSingle) {
        *H = single;
        return true;
    }
    return findZone(r, z, H) > 0;
}

static void methodMapMemory(void)
{
    P("\n[A] svcMapMemory heap -> stack region\n");
    u64 single;
    bool haveSingle = findSingle(&g_stack, &single) > 0;
    virtmemLock();
    for (unsigned z = 0; z < NZ; z++) {
        u64 H;
        if (!pickBase(&g_stack, z, haveSingle, single, &H)) {
            P("      %s no free aligned slot\n", kZones[z].name);
            continue;
        }
        u64 dst = H + kZones[z].off;
        void *src = memalign(0x1000, kZones[z].size);
        if (!src) {
            P("      %s memalign failed\n", kZones[z].name);
            continue;
        }
        Result rc = svcMapMemory((void *)dst, src, kZones[z].size);
        P("      %s svcMapMemory rc=0x%x\n", kZones[z].name, rc);
        if (R_SUCCEEDED(rc)) {
            testBlock(dst, z);
            rc = svcUnmapMemory((void *)dst, src, kZones[z].size);
            P("      %s unmap rc=0x%x\n", kZones[z].name, rc);
        }
        if (R_SUCCEEDED(rc)) {
            free(src);
        }
    }
    virtmemUnlock();
}

static void methodPhysical(void)
{
    P("\n[B] svcMapPhysicalMemory -> alias region\n");
    u64 single;
    bool haveSingle = findSingle(&g_alias, &single) > 0;
    virtmemLock();
    for (unsigned z = 0; z < NZ; z++) {
        u64 H;
        if (!pickBase(&g_alias, z, haveSingle, single, &H)) {
            P("      %s no free aligned slot\n", kZones[z].name);
            continue;
        }
        u64 dst = H + kZones[z].off;
        Result rc = svcMapPhysicalMemory((void *)dst, kZones[z].size);
        P("      %s svcMapPhysicalMemory rc=0x%x\n", kZones[z].name, rc);
        if (R_SUCCEEDED(rc)) {
            testBlock(dst, z);
            rc = svcUnmapPhysicalMemory((void *)dst, kZones[z].size);
            P("      %s unmap rc=0x%x\n", kZones[z].name, rc);
        }
    }
    virtmemUnlock();
}

static void methodCodeMemory(void)
{
    P("\n[C] svcMapProcessCodeMemory + RW -> ASLR/code region\n");
    Handle self = envGetOwnProcessHandle();
    P("      own process handle: 0x%x%s\n", self, self == INVALID_HANDLE ? " (none: skipped)" : "");
    if (self == INVALID_HANDLE) {
        return;
    }
    u64 single;
    bool haveSingle = findSingle(&g_aslr, &single) > 0;
    virtmemLock();
    for (unsigned z = 0; z < NZ; z++) {
        u64 H;
        if (!pickBase(&g_aslr, z, haveSingle, single, &H)) {
            P("      %s no free aligned slot\n", kZones[z].name);
            continue;
        }
        u64 dst = H + kZones[z].off;
        void *src = memalign(0x1000, kZones[z].size);
        if (!src) {
            P("      %s memalign failed\n", kZones[z].name);
            continue;
        }
        Result rc = svcMapProcessCodeMemory(self, dst, (u64)src, kZones[z].size);
        P("      %s svcMapProcessCodeMemory rc=0x%x\n", kZones[z].name, rc);
        bool mapped = R_SUCCEEDED(rc);
        if (mapped) {
            rc = svcSetProcessMemoryPermission(self, dst, kZones[z].size, Perm_Rw);
            P("      %s set RW rc=0x%x\n", kZones[z].name, rc);
            if (R_SUCCEEDED(rc)) {
                testBlock(dst, z);
            }
            rc = svcUnmapProcessCodeMemory(self, dst, (u64)src, kZones[z].size);
            P("      %s unmap rc=0x%x\n", kZones[z].name, rc);
        }
        if (!mapped || R_SUCCEEDED(rc)) {
            free(src);
        }
    }
    virtmemUnlock();
}

/* [D] the PC layout itself (H = 0): cart + dram through svcMapMemory into the
 * stack region at their exact N64 addresses, stacks through code memory at
 * 0xA0000000. All three stay mapped together, then are torn down. */
static void methodIdentity(void)
{
    P("\n[D] identity layout H=0 (PORT_ADDR_BASE == 0, like PC)\n");
    Handle self = envGetOwnProcessHandle();
    void *src[NZ] = { 0 };
    int how[NZ] = { 0 }; /* 0 none, 1 MapMemory, 2 code memory RW, -2 code memory not RW */
    virtmemLock();
    for (unsigned z = 0; z < NZ; z++) {
        u64 dst = kZones[z].off;
        src[z] = memalign(0x1000, kZones[z].size);
        if (!src[z]) {
            P("      %s memalign failed\n", kZones[z].name);
            continue;
        }
        if (allowed(dst, kZones[z].size, &g_stack) && unmapped(dst, kZones[z].size)) {
            Result rc = svcMapMemory((void *)dst, src[z], kZones[z].size);
            P("      %s svcMapMemory @0x%010lx rc=0x%x\n", kZones[z].name, dst, rc);
            if (R_SUCCEEDED(rc)) {
                how[z] = 1;
            }
        } else if (self != INVALID_HANDLE && allowed(dst, kZones[z].size, &g_aslr)
                   && unmapped(dst, kZones[z].size)) {
            Result rc = svcMapProcessCodeMemory(self, dst, (u64)src[z], kZones[z].size);
            P("      %s svcMapProcessCodeMemory @0x%010lx rc=0x%x\n", kZones[z].name, dst, rc);
            if (R_SUCCEEDED(rc)) {
                how[z] = 2;
                rc = svcSetProcessMemoryPermission(self, dst, kZones[z].size, Perm_Rw);
                P("      %s set RW rc=0x%x\n", kZones[z].name, rc);
                if (R_FAILED(rc)) {
                    how[z] = -2;
                }
            }
        } else {
            P("      %s @0x%010lx not free / not mappable here\n", kZones[z].name, dst);
        }
    }
    bool all = true;
    for (unsigned z = 0; z < NZ; z++) {
        if (how[z] > 0) {
            all &= testBlock(kZones[z].off, z);
        } else {
            all = false;
        }
    }
    P("      => identity layout %s\n", all ? "WORKS" : "does NOT work");
    for (unsigned z = 0; z < NZ; z++) {
        Result rc = 0;
        if (how[z] == 1) {
            rc = svcUnmapMemory((void *)kZones[z].off, src[z], kZones[z].size);
        } else if (how[z] == 2 || how[z] == -2) {
            rc = svcUnmapProcessCodeMemory(self, kZones[z].off, (u64)src[z], kZones[z].size);
        }
        if (how[z]) {
            P("      %s unmap rc=0x%x\n", kZones[z].name, rc);
        }
        if (src[z] && R_SUCCEEDED(rc)) {
            free(src[z]);
        }
    }
    virtmemUnlock();
}

static void printRegion(Region *r, const char *name, u32 idA, u32 idS)
{
    r->name = name;
    r->base = info(idA);
    r->size = info(idS);
    P("  %-6s 0x%010lx - 0x%010lx  (%6lu MiB)\n", name, r->base, r->base + r->size, r->size >> 20);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    g_log = fopen("sdmc:/switch/memprobe.txt", "a"); /* one block per run: launch it 2-3 times */

    P("\n=============== project-dam memprobe v2 ===============\n");
    {
        void *h = malloc(16);
        P("this .nro: main @%p  heap block @%p  stack var @%p\n", (void *)main, h, (void *)&pad);
        free(h);
    }
    P("applet type: %d (0 = application / title takeover)\n", (int)appletGetAppletType());
    P("memory: total %lu MiB, used %lu MiB\n", info(InfoType_TotalMemorySize) >> 20,
      info(InfoType_UsedMemorySize) >> 20);
    P("own process handle available: %s\n", envGetOwnProcessHandle() != INVALID_HANDLE ? "yes" : "no");
    P("\nregions:\n");
    printRegion(&g_heap, "heap", InfoType_HeapRegionAddress, InfoType_HeapRegionSize);
    printRegion(&g_alias, "alias", InfoType_AliasRegionAddress, InfoType_AliasRegionSize);
    printRegion(&g_stack, "stack", InfoType_StackRegionAddress, InfoType_StackRegionSize);
    printRegion(&g_aslr, "aslr", InfoType_AslrRegionAddress, InfoType_AslrRegionSize);

    P("\nfree 4 GiB-aligned slots (cart 32M @+0x10000000, dram 8M @+0x70000000, stack 16M @+0xA0000000):\n");
    survey(&g_stack);
    survey(&g_alias);
    survey(&g_aslr);

    methodMapMemory();
    methodPhysical();
    methodCodeMemory();
    methodIdentity();

    P("\ndone. log: sdmc:/switch/memprobe.txt  -- press + to exit\n");
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
