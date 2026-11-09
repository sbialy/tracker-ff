#ifndef NEW_ARENA_H
#define NEW_ARENA_H

// A first-fit allocator over one fixed buffer (the SDRAM arena of new.cpp, a host buffer in tools/new_arena_test.cpp). No hardware, no
// allocation of its own. Blocks are 8 byte aligned, each has an 8 byte header (payload size, used flag). Not thread safe: new.cpp holds the
// interrupts off around every call. Free neighbours are merged on every free, the arena is a few dozen blocks at most.
#include <stddef.h>
#include <stdint.h>

class NewArena
{
  public:
    void init(uint8_t* base, size_t bytes)
    {
        base_ = base;
        end_ = base + (bytes & ~size_t(7));
        Hdr* h = reinterpret_cast<Hdr*>(base_);
        h->size = uint32_t(end_ - base_ - sizeof(Hdr));
        h->used = 0;
    }
    bool ready() const { return base_ != nullptr; }
    bool owns(const void* p) const { return base_ && static_cast<const uint8_t*>(p) > base_ && static_cast<const uint8_t*>(p) < end_; }

    void* alloc(size_t n)
    {
        if (!base_ || n == 0)
        {
            return nullptr;
        }
        n = (n + 7) & ~size_t(7);
        for (Hdr* h = reinterpret_cast<Hdr*>(base_); reinterpret_cast<uint8_t*>(h) < end_; h = next(h))
        {
            if (h->used || h->size < n)
            {
                continue;
            }
            if (h->size >= n + sizeof(Hdr) + 8)   // split: the rest stays a free block
            {
                Hdr* rest = reinterpret_cast<Hdr*>(reinterpret_cast<uint8_t*>(h) + sizeof(Hdr) + n);
                rest->size = uint32_t(h->size - n - sizeof(Hdr));
                rest->used = 0;
                h->size = uint32_t(n);
            }
            h->used = 1;
            used_ += h->size;
            if (used_ > peak_)
            {
                peak_ = used_;
            }
            return reinterpret_cast<uint8_t*>(h) + sizeof(Hdr);
        }
        return nullptr;
    }

    void free(void* p)
    {
        Hdr* h = reinterpret_cast<Hdr*>(static_cast<uint8_t*>(p) - sizeof(Hdr));
        h->used = 0;
        used_ -= h->size;
        for (Hdr* a = reinterpret_cast<Hdr*>(base_); reinterpret_cast<uint8_t*>(a) < end_;)
        {
            Hdr* b = next(a);
            if (!a->used && reinterpret_cast<uint8_t*>(b) < end_ && !b->used)
            {
                a->size += uint32_t(sizeof(Hdr) + b->size);   // merge, look at a again against its new neighbour
                continue;
            }
            a = b;
        }
    }

    size_t usedBytes() const { return used_; }   // payload bytes of the live blocks
    size_t peakBytes() const { return peak_; }   // the most usedBytes() has been
    size_t largestFree() const
    {
        size_t m = 0;
        for (const Hdr* h = reinterpret_cast<const Hdr*>(base_); reinterpret_cast<const uint8_t*>(h) < end_; h = next(h))
        {
            if (!h->used && h->size > m)
            {
                m = h->size;
            }
        }
        return m;
    }

    size_t freeBytes() const
    {
        size_t f = 0;
        for (const Hdr* h = reinterpret_cast<const Hdr*>(base_); reinterpret_cast<const uint8_t*>(h) < end_; h = next(h))
        {
            f += h->used ? 0 : h->size;
        }
        return f;
    }

  private:
    struct Hdr
    {
        uint32_t size;
        uint32_t used;
    };
    static Hdr* next(Hdr* h) { return reinterpret_cast<Hdr*>(reinterpret_cast<uint8_t*>(h) + sizeof(Hdr) + h->size); }
    static const Hdr* next(const Hdr* h) { return reinterpret_cast<const Hdr*>(reinterpret_cast<const uint8_t*>(h) + sizeof(Hdr) + h->size); }
    uint8_t* base_ = nullptr;
    uint8_t* end_ = nullptr;
    size_t used_ = 0;
    size_t peak_ = 0;
};

#endif /* NEW_ARENA_H */
