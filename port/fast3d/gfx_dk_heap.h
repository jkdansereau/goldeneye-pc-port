#ifndef GFX_DK_HEAP_H
#define GFX_DK_HEAP_H

/* D973: deko3d backend bookkeeping, no deko3d types (host-tested by
 * tools_pc/dkshgen/dk_heap_test.cpp and dk_ring_test.cpp).
 *
 * Every piece of GPU memory or descriptor slot the CPU recycles is guarded by
 * the serial of the last submission that may still read it: a free becomes
 * reusable only once retire(completed) has seen that serial complete (the
 * backend checks its DkFences). The backend maps "chunk" indices to DkMemBlocks
 * it creates when an alloc reports a new chunk. */

#include <stdint.h>
#include <vector>
#include <algorithm>

struct DkHeapBlock {
    uint32_t chunk;
    uint32_t offset;
    uint32_t size;
};

/* First-fit allocator over chunks of chunk_size bytes (a bigger request gets a
 * chunk of its own size), coalescing free ranges, deferred frees. */
class DkHeap {
public:
    explicit DkHeap(uint32_t chunk_size) : m_chunk_size(chunk_size) {}

    /* out->chunk == chunks() - 1 and grew() true when a chunk was added. */
    bool alloc(uint32_t size, uint32_t align, DkHeapBlock* out) {
        m_grew = false;
        if (size == 0) size = 1;
        if (align == 0) align = 1;
        for (uint32_t c = 0; c < (uint32_t)m_chunks.size(); c++) {
            if (take_from(c, size, align, out)) return true;
        }
        uint64_t need = (uint64_t)size + align;
        uint64_t csz = m_chunk_size;
        while (csz < need) csz += m_chunk_size;
        if (csz > 0xffffffffull) return false;
        Chunk ch;
        ch.size = (uint32_t)csz;
        ch.free.push_back(Range{ 0, ch.size });
        m_chunks.push_back(ch);
        m_grew = true;
        return take_from((uint32_t)m_chunks.size() - 1, size, align, out);
    }
    bool grew() const { return m_grew; }

    void free_after(const DkHeapBlock& b, uint64_t fence_serial) {
        m_pending.push_back(Pending{ b, fence_serial });
    }

    void retire(uint64_t completed_serial) {
        size_t w = 0;
        for (size_t i = 0; i < m_pending.size(); i++) {
            if (m_pending[i].serial <= completed_serial) {
                give_back(m_pending[i].b);
            } else {
                m_pending[w++] = m_pending[i];
            }
        }
        m_pending.resize(w);
    }

    uint32_t chunks() const { return (uint32_t)m_chunks.size(); }
    uint32_t chunk_size(uint32_t c) const { return m_chunks[c].size; }
    size_t pending() const { return m_pending.size(); }

private:
    struct Range { uint32_t off, size; };
    struct Chunk { uint32_t size; std::vector<Range> free; };   /* sorted by off */
    struct Pending { DkHeapBlock b; uint64_t serial; };

    bool take_from(uint32_t c, uint32_t size, uint32_t align, DkHeapBlock* out) {
        std::vector<Range>& fr = m_chunks[c].free;
        for (size_t i = 0; i < fr.size(); i++) {
            const uint32_t start = (fr[i].off + align - 1) / align * align;
            const uint32_t pad = start - fr[i].off;
            if ((uint64_t)pad + size > fr[i].size) continue;
            const Range r = fr[i];
            fr.erase(fr.begin() + i);
            /* the tail after the block, then the head before it, stay free */
            const uint32_t tail_off = start + size, tail = r.off + r.size - tail_off;
            if (tail) fr.insert(fr.begin() + i, Range{ tail_off, tail });
            if (pad) fr.insert(fr.begin() + i, Range{ r.off, pad });
            out->chunk = c;
            out->offset = start;
            out->size = size;
            return true;
        }
        return false;
    }

    void give_back(const DkHeapBlock& b) {
        std::vector<Range>& fr = m_chunks[b.chunk].free;
        size_t i = 0;
        while (i < fr.size() && fr[i].off < b.offset) i++;
        fr.insert(fr.begin() + i, Range{ b.offset, b.size });
        if (i + 1 < fr.size() && fr[i].off + fr[i].size == fr[i + 1].off) {
            fr[i].size += fr[i + 1].size;
            fr.erase(fr.begin() + i + 1);
        }
        if (i > 0 && fr[i - 1].off + fr[i - 1].size == fr[i].off) {
            fr[i - 1].size += fr[i].size;
            fr.erase(fr.begin() + i);
        }
    }

    uint32_t m_chunk_size;
    std::vector<Chunk> m_chunks;
    std::vector<Pending> m_pending;
    bool m_grew = false;
};

/* Descriptor slots (image / sampler descriptor indices), lowest free first. */
class DkSlots {
public:
    explicit DkSlots(uint32_t n) {
        for (uint32_t i = n; i > 0; i--) m_free.push_back((int)i - 1);
    }
    int take() {
        if (m_free.empty()) return -1;
        const int s = m_free.back();
        m_free.pop_back();
        return s;
    }
    void release_after(int slot, uint64_t fence_serial) {
        m_pending.push_back(std::make_pair(slot, fence_serial));
    }
    void retire(uint64_t completed_serial) {
        size_t w = 0;
        bool added = false;
        for (size_t i = 0; i < m_pending.size(); i++) {
            if (m_pending[i].second <= completed_serial) {
                m_free.push_back(m_pending[i].first);
                added = true;
            } else {
                m_pending[w++] = m_pending[i];
            }
        }
        m_pending.resize(w);
        if (added) std::sort(m_free.begin(), m_free.end(), std::greater<int>());
    }
    size_t available() const { return m_free.size(); }

private:
    std::vector<int> m_free;   /* sorted descending: back() is the lowest */
    std::vector<std::pair<int, uint64_t>> m_pending;
};

/* Linear per-submission allocator (vertices, uniforms, upload staging, command
 * memory). Allocations go front to back in the current chunk, which is tagged
 * with the serial of the submission being recorded; when a request does not
 * fit, the chunk is set aside with its tag and a chunk whose tag has retired
 * is taken (or a new one made). A chunk is never written again before the GPU
 * has finished every submission that used it. */
struct DkRingAlloc {
    uint32_t chunk;
    uint32_t offset;
    bool new_chunk;   /* chunk == chunks() - 1 was just created: back it with memory */
};

class DkRing {
public:
    explicit DkRing(uint32_t chunk_size) : m_chunk_size(chunk_size) {}

    bool alloc(uint32_t size, uint32_t align, uint64_t serial, DkRingAlloc* out) {
        out->new_chunk = false;
        if (align == 0) align = 1;
        if (m_cur >= 0) {
            Chunk& ch = m_chunks[m_cur];
            const uint64_t start = ((uint64_t)ch.used + align - 1) / align * align;
            if (start + size <= ch.size) {
                ch.used = (uint32_t)(start + size);
                ch.serial = serial;
                out->chunk = (uint32_t)m_cur;
                out->offset = (uint32_t)start;
                return true;
            }
            m_busy.push_back(m_cur);   /* keeps its tag until retired */
            m_cur = -1;
        }
        /* a retired chunk big enough */
        for (size_t i = 0; i < m_free.size(); i++) {
            if (m_chunks[m_free[i]].size >= size) {
                m_cur = m_free[i];
                m_free.erase(m_free.begin() + i);
                break;
            }
        }
        if (m_cur < 0) {
            Chunk ch;
            ch.size = m_chunk_size;
            while (ch.size < size) ch.size += m_chunk_size;
            ch.used = 0;
            ch.serial = serial;
            m_chunks.push_back(ch);
            m_cur = (int)m_chunks.size() - 1;
            out->new_chunk = true;
            m_grows++;
        }
        Chunk& ch = m_chunks[m_cur];
        ch.used = size;
        ch.serial = serial;
        out->chunk = (uint32_t)m_cur;
        out->offset = 0;
        return true;
    }

    /* The current chunk is used by `serial` too (e.g. command memory carried over a submission). */
    void touch(uint64_t serial) {
        if (m_cur >= 0) m_chunks[m_cur].serial = serial;
    }

    void retire(uint64_t completed_serial) {
        size_t w = 0;
        for (size_t i = 0; i < m_busy.size(); i++) {
            if (m_chunks[m_busy[i]].serial <= completed_serial) {
                m_chunks[m_busy[i]].used = 0;
                m_free.push_back(m_busy[i]);
            } else {
                m_busy[w++] = m_busy[i];
            }
        }
        m_busy.resize(w);
    }

    uint32_t chunks() const { return (uint32_t)m_chunks.size(); }
    uint32_t chunk_size(uint32_t c) const { return m_chunks[c].size; }
    int current() const { return m_cur; }
    uint64_t grows() const { return m_grows; }

private:
    struct Chunk { uint32_t size, used; uint64_t serial; };
    uint32_t m_chunk_size;
    std::vector<Chunk> m_chunks;
    std::vector<int> m_busy, m_free;
    int m_cur = -1;
    uint64_t m_grows = 0;
};

#endif
