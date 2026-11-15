/* D973: host test of DkHeap / DkSlots (port/fast3d/gfx_dk_heap.h).
 *   g++ -std=c++17 -O2 -Wall -I port/fast3d -o <temp>/dkht tools_pc/dkshgen/dk_heap_test.cpp && <temp>/dkht */
#include <stdio.h>
#include "gfx_dk_heap.h"

static int s_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); s_fail++; } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { int f0 = s_fail; name(); printf("%s %s\n", s_fail == f0 ? "ok  " : "FAIL", #name); } while (0)

TEST(alloc_respects_alignment) {
    DkHeap h(1 << 16);
    DkHeapBlock a, b, c;
    CHECK(h.alloc(10, 1, &a) && a.offset == 0, "first at 0");
    CHECK(h.alloc(100, 256, &b) && b.offset == 256, "aligned to 256");
    CHECK(h.alloc(4, 4096, &c) && c.offset % 4096 == 0, "aligned to 4096");
    CHECK(h.alloc(5, 1, &a) && a.offset == 10, "the gap before an aligned block stays free");
}

TEST(coalesces_neighbours) {
    DkHeap h(4096);
    DkHeapBlock a, b, c, d;
    h.alloc(1024, 1, &a); h.alloc(1024, 1, &b); h.alloc(1024, 1, &c);
    h.free_after(a, 1); h.free_after(c, 1); h.free_after(b, 1);
    h.retire(1);
    CHECK(h.alloc(4096, 1, &d) && d.chunk == 0 && d.offset == 0, "3 freed neighbours + tail = one 4 KiB range");
    CHECK(h.chunks() == 1, "no new chunk");
}

TEST(deferred_free_not_reused_before_fence) {
    DkHeap h(1 << 16);
    DkHeapBlock a, b, c;
    h.alloc(512, 256, &a);
    h.alloc(512, 256, &b);
    h.free_after(a, 5);
    h.retire(4);
    CHECK(h.alloc(512, 256, &c) && !(c.chunk == a.chunk && c.offset == a.offset), "not reused before serial 5");
    h.retire(5);
    DkHeapBlock d;
    CHECK(h.alloc(512, 256, &d) && d.chunk == a.chunk && d.offset == a.offset, "reused once serial 5 retired");
}

TEST(grows_new_chunk_when_full) {
    DkHeap h(4096);
    DkHeapBlock a, b, c;
    CHECK(h.alloc(3000, 1, &a) && a.chunk == 0 && h.grew(), "first alloc creates chunk 0");
    CHECK(h.alloc(3000, 1, &b) && b.chunk == 1 && h.grew() && h.chunks() == 2, "second chunk");
    CHECK(h.alloc(10000, 256, &c) && c.chunk == 2 && h.chunk_size(2) >= 10256, "oversized request gets its own chunk");
    DkHeapBlock d;
    CHECK(h.alloc(500, 1, &d) && d.chunk == 0 && !h.grew(), "small request fits in chunk 0's tail");
}

TEST(slots_reused_only_after_fence) {
    DkSlots s(4);
    int a = s.take(), b = s.take();
    CHECK(a == 0 && b == 1, "lowest first");
    s.release_after(a, 7);
    s.retire(6);
    int c = s.take();
    CHECK(c == 2, "released slot not reused before serial 7");
    s.retire(7);
    CHECK(s.take() == 0, "reused after serial 7");
    CHECK(s.take() == 3 && s.take() == -1, "exhausted");
}

int main() {
    RUN(alloc_respects_alignment);
    RUN(coalesces_neighbours);
    RUN(deferred_free_not_reused_before_fence);
    RUN(grows_new_chunk_when_full);
    RUN(slots_reused_only_after_fence);
    printf("dk_heap_test: %s\n", s_fail ? "FAILED" : "all PASS");
    return s_fail ? 1 : 0;
}
