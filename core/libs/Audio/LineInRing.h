#ifndef LIBS_AUDIO_LINE_IN_RING_H_
#define LIBS_AUDIO_LINE_IN_RING_H_

#include <stdint.h>
#include <string.h>

// Line in block handoff between the input DMA interrupt and the render (MK66).
//
// Before: the input interrupt only stored a pointer into the live two-half DMA buffer and the render copied from it whenever
// it ran, with no check that it read exactly one new block. Two renders in one output interrupt (output_i2s.cpp catch-up)
// read the same block twice and the next render skipped one, a 2.9 ms splice on the line in only.
//
// Now: push() runs in the input interrupt (priority 4, above the output interrupt that renders) and copies the half the DMA just
// finished into a ring with a write sequence number. fetch() runs once per render and takes exactly one block in order. The
// read side keeps one block of cushion (fixed +1 block, 2.9 ms, of line in latency), so render jitter around the input
// interrupt never starves it. A block is repeated only on a real underrun (more renders than input blocks), and blocks are
// dropped only when an extra block stays queued for 16 renders or the ring is half full (fewer renders than input blocks),
// both counted. A double render can hold one more block for a render or two, the steady latency is the one cushion block.
//
// The counters (since power on, wrap at 2^32):
//   dup    renders that found no new input block since the render before (the old code read the same block twice)
//   skip   input blocks the old code would have skipped (more than one new block since the render before)
//   under  renders that had to repeat the last block (ring empty)
//   drop   blocks thrown away to bring the latency back to one block
// dup and skip measure the old race, under and drop what the fixed path did about it.
template <uint32_t kBlocks, uint32_t kBlockWords>
struct LineInRing
{
    static_assert((kBlocks & (kBlocks - 1)) == 0 && kBlocks >= 8, "power of two, the backlog cap and writes in flight");
    static constexpr uint32_t kMaxAvail = kBlocks / 2;   // the handed out slot is rewritten only after kBlocks - kMaxAvail more pushes
    static constexpr uint32_t kBacklogRenders = 16;      // 46 ms of extra backlog before it is dropped

    uint32_t (*slots)[kBlockWords] = nullptr;   // kBlocks blocks, set before the first push (SDRAM on the unit)
    volatile uint32_t written = 0;              // blocks pushed, the interrupt is the only writer
    uint32_t read = 0;                          // the next block fetch() hands out
    uint32_t seenAtLastFetch = 0;               // written at the last fetch (old race counters)
    uint32_t backlogRenders = 0;               // renders in a row that left more than the cushion block
    bool started = false;
    volatile uint32_t dup = 0;
    volatile uint32_t skip = 0;
    volatile uint32_t under = 0;
    volatile uint32_t drop = 0;

    // input interrupt: copy one finished block (kBlockWords words, one stereo frame per word)
    void push(const void* src)
    {
        const uint32_t w = written;
        memcpy(slots[w & (kBlocks - 1)], src, sizeof(slots[0]));
        written = w + 1;   // published after the copy, the render never sees a half written block
    }

    // render: the block to use now, nullptr while no input has arrived yet
    const uint32_t* fetch()
    {
        const uint32_t w = written;   // one read, the interrupt may push while the render copies
        if (!started)
        {
            if (w == 0) return nullptr;
            started = true;
            seenAtLastFetch = w;
            read = w >= 2 ? w - 2 : 0;   // one block of cushion from the start (none if only one block exists yet)
        }
        else
        {
            const uint32_t fresh = w - seenAtLastFetch;
            seenAtLastFetch = w;
            if (fresh == 0) dup = dup + 1;
            else if (fresh > 1) skip = skip + (fresh - 1);
        }

        uint32_t avail = w - read;
        if (avail > kMaxAvail)
        {   // far behind (more input than renders for a while): keep the newest two, the slot handed out is never rewritten
            drop = drop + (avail - 2);
            read = w - 2;
            avail = 2;
        }
        if (avail == 0)
        {   // real underrun: repeat the last block
            under = under + 1;
            backlogRenders = 0;
            return slots[(read - 1) & (kBlocks - 1)];
        }
        const uint32_t* block = slots[read & (kBlocks - 1)];
        read++;
        // more than the one cushion block left after this render: a double render or a late input interrupt evens that out
        // within a render or two, so only a backlog that stays for kBacklogRenders renders is dropped (latency back to one)
        if (avail - 1 >= 2)
        {
            if (++backlogRenders >= kBacklogRenders)
            {
                drop = drop + (avail - 2);
                read = w - 1;
                backlogRenders = 0;
            }
        }
        else
        {
            backlogRenders = 0;
        }
        return block;
    }
};

#endif /* LIBS_AUDIO_LINE_IN_RING_H_ */
