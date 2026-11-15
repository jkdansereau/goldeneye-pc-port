/* D973: host test of DkRing (port/fast3d/gfx_dk_heap.h).
 *   g++ -std=c++17 -O2 -Wall -I port/fast3d -o <temp>/dkrt tools_pc/dkshgen/dk_ring_test.cpp && <temp>/dkrt */
#include <stdio.h>
#include "gfx_dk_heap.h"

static int s_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); s_fail++; } } while (0)
#define RUN(name) do { int f0 = s_fail; name(); printf("%s %s\n", s_fail == f0 ? "ok  " : "FAIL", #name); } while (0)

static void ring_wraps_only_past_fence(void) {
    DkRing r(1024);
    DkRingAlloc a;
    CHECK(r.alloc(600, 4, 1, &a) && a.chunk == 0 && a.new_chunk, "frame 1 starts chunk 0");
    CHECK(r.alloc(600, 4, 1, &a) && a.chunk == 1 && a.new_chunk, "frame 1 spills into a second chunk");
    CHECK(r.alloc(600, 4, 2, &a) && a.chunk == 2 && a.new_chunk, "frame 2: chunks 0/1 still in flight -> new chunk");
    r.retire(1);
    CHECK(r.alloc(600, 4, 3, &a) && (a.chunk == 0 || a.chunk == 1) && !a.new_chunk && a.offset == 0,
          "after serial 1 retired, a frame-1 chunk is offered again");
    const uint32_t reused = a.chunk;
    CHECK(r.alloc(600, 4, 3, &a) && a.chunk != reused && a.chunk != 2 && !a.new_chunk, "the other frame-1 chunk next");
    CHECK(r.alloc(600, 4, 4, &a) && a.new_chunk, "nothing retired since: grow again");
    CHECK(r.chunks() == 4, "4 chunks in all");
    DkRingAlloc big;
    CHECK(r.alloc(5000, 256, 4, &big) && big.new_chunk && r.chunk_size(big.chunk) >= 5000, "oversized request");
}

static void ring_alignment_256_for_uniforms(void) {
    DkRing r(1 << 16);
    DkRingAlloc a;
    r.alloc(10, 4, 1, &a);
    CHECK(r.alloc(24, 256, 1, &a) && a.offset == 256, "uniform block at 256");
    CHECK(r.alloc(4, 4, 1, &a) && a.offset == 280, "packed after it");
    CHECK(r.alloc(24, 256, 1, &a) && a.offset == 512, "next uniform block at 512");
}

int main() {
    RUN(ring_wraps_only_past_fence);
    RUN(ring_alignment_256_for_uniforms);
    printf("dk_ring_test: %s\n", s_fail ? "FAILED" : "all PASS");
    return s_fail ? 1 : 0;
}
