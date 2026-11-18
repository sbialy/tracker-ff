#include "inflate.h"

// Decoder after the one in RFC 1951 and zlib's contrib/puff: counts of codes per length and the symbols in code order, a code is decoded
// bit by bit. Written for size (about 1 KB of -Os code).
namespace
{
constexpr int kMaxBits = 15;

struct State
{
    const uint8_t* in;
    size_t inLen, inPos;
    uint8_t* out;
    size_t outCap, outPos;
    uint32_t bitBuf;
    int bitCnt;
    int err;   // 0, or the negative result
    InflateScratch* sc;
};

struct Huff
{
    uint16_t* count;    // count[len] = codes of that length, len 0..15
    uint16_t* symbol;   // symbols ordered by code
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

int bits(State* s, int need)
{
    uint32_t val = s->bitBuf;
    while (s->bitCnt < need)
    {
        if (s->inPos >= s->inLen)
        {
            s->err = -3;
            return 0;
        }
        val |= static_cast<uint32_t>(s->in[s->inPos++]) << s->bitCnt;
        s->bitCnt += 8;
    }
    s->bitBuf = val >> need;
    s->bitCnt -= need;
    return static_cast<int>(val & ((1u << need) - 1));
}

// Symbol of the next code, or -1 (no such code, or the input ended: s->err says)
int decode(State* s, const Huff* h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; len++)
    {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = -1;
    return -1;
}

// Build the decoding tables from n code lengths. Returns 0 for a complete code, > 0 for an incomplete one (fine for a lone distance
// code), < 0 for an over-subscribed one
int construct(Huff* h, const uint16_t* length, int n)
{
    uint16_t offs[kMaxBits + 1];
    for (int len = 0; len <= kMaxBits; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++) offs[len + 1] = static_cast<uint16_t>(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = static_cast<uint16_t>(sym);
    return left;
}

// The compressed data of a block: literals and (length, distance) pairs until the end of block symbol
int codes(State* s, const Huff* lencode, const Huff* distcode)
{
    for (;;)
    {
        int sym = decode(s, lencode);
        if (sym < 0) return s->err ? s->err : -1;
        if (sym < 256)
        {
            if (s->outPos >= s->outCap) return -2;
            s->out[s->outPos++] = static_cast<uint8_t>(sym);
        }
        else if (sym == 256)
        {
            return 0;
        }
        else
        {
            sym -= 257;
            if (sym >= 29) return -1;
            size_t len = kLenBase[sym] + static_cast<size_t>(bits(s, kLenExtra[sym]));
            int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return s->err ? s->err : -1;
            size_t dist = kDistBase[dsym] + static_cast<size_t>(bits(s, kDistExtra[dsym]));
            if (s->err) return s->err;
            if (dist > s->outPos) return -1;
            if (len > s->outCap - s->outPos) return -2;
            for (; len; len--, s->outPos++) s->out[s->outPos] = s->out[s->outPos - dist];
        }
    }
}

int stored(State* s)
{
    s->bitBuf = 0;
    s->bitCnt = 0;
    if (s->inLen - s->inPos < 4) return -3;
    size_t len = s->in[s->inPos] | (s->in[s->inPos + 1] << 8);
    size_t nlen = s->in[s->inPos + 2] | (s->in[s->inPos + 3] << 8);
    s->inPos += 4;
    if (len != (~nlen & 0xFFFF)) return -1;
    if (len > s->inLen - s->inPos) return -3;
    if (len > s->outCap - s->outPos) return -2;
    for (size_t i = 0; i < len; i++) s->out[s->outPos++] = s->in[s->inPos++];
    return 0;
}

int fixedBlock(State* s)
{
    InflateScratch* sc = s->sc;
    int sym = 0;
    for (; sym < 144; sym++) sc->lengths[sym] = 8;
    for (; sym < 256; sym++) sc->lengths[sym] = 9;
    for (; sym < 280; sym++) sc->lengths[sym] = 7;
    for (; sym < 288; sym++) sc->lengths[sym] = 8;
    Huff lencode = {sc->lcount, sc->lsym};
    construct(&lencode, sc->lengths, 288);
    for (sym = 0; sym < 30; sym++) sc->lengths[sym] = 5;
    Huff distcode = {sc->dcount, sc->dsym};
    construct(&distcode, sc->lengths, 30);
    return codes(s, &lencode, &distcode);
}

int dynamicBlock(State* s)
{
    InflateScratch* sc = s->sc;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->err) return s->err;
    if (nlen > 286 || ndist > 30) return -1;
    int idx = 0;
    for (; idx < ncode; idx++) sc->lengths[kClOrder[idx]] = static_cast<uint16_t>(bits(s, 3));
    for (; idx < 19; idx++) sc->lengths[kClOrder[idx]] = 0;
    if (s->err) return s->err;
    Huff lencode = {sc->lcount, sc->lsym};
    if (construct(&lencode, sc->lengths, 19) != 0) return -1;   // the code length code must be complete
    idx = 0;
    while (idx < nlen + ndist)
    {
        int sym = decode(s, &lencode);
        if (sym < 0) return s->err ? s->err : -1;
        if (sym < 16)
        {
            sc->lengths[idx++] = static_cast<uint16_t>(sym);
        }
        else
        {
            uint16_t prev = 0;
            int rep;
            if (sym == 16)
            {
                if (idx == 0) return -1;
                prev = sc->lengths[idx - 1];
                rep = 3 + bits(s, 2);
            }
            else if (sym == 17)
            {
                rep = 3 + bits(s, 3);
            }
            else
            {
                rep = 11 + bits(s, 7);
            }
            if (s->err) return s->err;
            if (idx + rep > nlen + ndist) return -1;
            while (rep--) sc->lengths[idx++] = prev;
        }
    }
    if (sc->lengths[256] == 0) return -1;   // no end of block code
    int err = construct(&lencode, sc->lengths, nlen);
    if (err && (err < 0 || nlen - lencode.count[0] != 1)) return -1;
    Huff distcode = {sc->dcount, sc->dsym};
    err = construct(&distcode, sc->lengths + nlen, ndist);
    if (err && (err < 0 || ndist - distcode.count[0] != 1)) return -1;
    return codes(s, &lencode, &distcode);
}
}   // namespace

int inflateRaw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap, InflateScratch* scratch)
{
    State s = {src, srcLen, 0, dst, dstCap, 0, 0, 0, 0, scratch};
    int last, err;
    do
    {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) return s.err;
        err = type == 0 ? stored(&s) : type == 1 ? fixedBlock(&s) : type == 2 ? dynamicBlock(&s) : -1;
        if (err) return err;
    } while (!last);
    return static_cast<int>(s.outPos);
}
