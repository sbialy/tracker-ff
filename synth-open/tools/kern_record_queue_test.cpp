// Driver of kern_record_queue_test.sh: the real AudioStream.cpp and record_queue.cpp (old: release-1.3.0-int 1d9c033, new: this tree)
// run the same random script, the trace goes to stdout and the script compares the two traces byte for byte.
//
// Streams (construction order = update order): S1, Q1 (fed by S1: the source updates first), Q2 (fed by S2, which is constructed AFTER it:
// the master export tap case, a block is in flight in its input between rounds), S2, S3, plus a plain consumer P1 of S3. A queue is the
// only destination of its source: AudioConnection::disconnect does not unlink a connection that is not first in its source's list. Each round:
// random events on the queues (begin, end, a connection removed and made again, the reader draining), then update_all(). The trace holds, per
// round, the blocks each queue returns to the reader (their data), the number of blocks in the pool in use and the block hashes the plain
// consumer saw, so a changed recording start, a block that is kept or lost and a leak all show.
#include "AudioStream.h"
#include "record_queue.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
uint64_t rngState = 1;
uint32_t rnd()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (uint32_t)(rngState >> 16);
}

uint32_t gRound;

class Source : public AudioStream
{
  public:
    explicit Source(uint32_t id) : AudioStream(0, nullptr), id(id) {}
    void update() override
    {
        audio_block_t* b = allocate();
        if (!b)
            return;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            b->data[i] = (int16_t)(id * 1000 + gRound * 7 + i);
        transmit(b);
        release(b);
    }
    uint32_t id;
};

class Plain : public AudioStream
{
  public:
    Plain() : AudioStream(2, q) {}
    void update() override
    {
        for (int i = 0; i < 2; i++)
        {
            audio_block_t* b = receiveReadOnly(i);
            if (!b)
                continue;
            for (int k = 0; k < AUDIO_BLOCK_SAMPLES; k++)
                hash = hash * 1099511628211ull + (uint16_t)b->data[k];
            release(b);
        }
    }
    uint64_t hash = 14695981039346656037ull;
    audio_block_t* q[2];
};

audio_block_t gPool[40];
} // namespace

int main(int argc, char** argv)
{
    rngState = 0x9E3779B97F4A7C15ull ^ (uint64_t)(argc > 1 ? atoi(argv[1]) : 1) * 0x100000001B3ull;
    AudioStream::initialize_memory(gPool, 40);

    Source s1(1);
    AudioRecordQueue q1;
    AudioRecordQueue q2; // before its source
    Source s2(2);
    Source s3(3);
    Plain p1;
    AudioConnection* c1 = new AudioConnection(&s1, 0, &q1, 0);
    AudioConnection* c2 = new AudioConnection(&s2, 0, &q2, 0);
    AudioConnection c3(&s3, 0, &p1, 0);
    AudioRecordQueue* qs[2] = { &q1, &q2 };
    AudioConnection** cs[2] = { &c1, &c2 };
    AudioStream* srcs[2] = { &s1, &s2 };

    const int rounds = 4000;
    unsigned asleep[2] = { 0, 0 }; // rounds a queue was out of update_all (stderr only: the old tree never is)
    for (gRound = 0; gRound < (uint32_t)rounds; gRound++)
    {
        for (int k = 0; k < 2; k++)
        {
            switch (rnd() % 16)
            {
                case 0: qs[k]->begin(); break;
                case 1: qs[k]->end(); break;
                case 2:
                    if (*cs[k])
                    {
                        delete *cs[k];
                        *cs[k] = nullptr;
                    }
                    break;
                case 3:
                    if (!*cs[k])
                        *cs[k] = new AudioConnection(srcs[k], 0, qs[k], 0);
                    break;
                default: break;
            }
        }
        // a start inside a round, between the sources and the queues, is not modelled: events happen on round boundaries (the render
        // mode of the exporter, and the main loop between two audio interrupts)
        AudioStream::update_all();
        for (int k = 0; k < 2; k++)
        {
            // the reader takes 0..2 blocks per round
            for (uint32_t n = rnd() % 3; n > 0 && qs[k]->available(); n--)
            {
                const int16_t* d = qs[k]->readBuffer();
                uint64_t h = 0;
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
                    h = h * 1099511628211ull + (uint16_t)d[i];
                printf("r%u q%d block %016llx\n", gRound, k + 1, (unsigned long long)h);
                qs[k]->freeBuffer();
            }
        }
        for (int k = 0; k < 2; k++)
            asleep[k] += qs[k]->isActive() ? 0 : 1;
        printf("r%u used %u plain %016llx\n", gRound, (unsigned)AudioStream::memory_used, (unsigned long long)p1.hash);
    }
    fprintf(stderr, "asleep rounds: q1 %u, q2 %u of %d\n", asleep[0], asleep[1], rounds);
    return 0;
}
