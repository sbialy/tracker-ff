// .patch codec for the open PolySynth shim: protobuf wire reader and writer for the Polyend
// Tracker Mini / stock `Patch` message, trimmed from OpenTracker's formats/patch_pb.cpp
// (https://github.com/sandlbn/polyend-tracker-re, commit 927b1516385e, Apache-2.0, see
// licenses/LICENSE-OpenTracker-Apache-2.0.txt). Changed from the original (Apache-2.0 section 4b):
// rewritten to a much smaller form for the MK66 flash budget, works on the shim's PatchData,
// the engine tables, validation and the OpenTracker extension field are not carried over
// (the extension field and the other unknown fields are kept as raw bytes and written back).
//
// Layout (field numbers): 1 engine, 6 repeated ParamValue{1: fixed32 value}, 7 four names,
// 8 eight Macro{1 name, 2 bipolar, 3 target_count, 4 six Target{1 param, 2 fixed32 amount}},
// 9 Extra (constant block, regenerated), 64 synth automation (version 1, below), anything else (2-5, 10, 11, 16, ...) kept raw
// in file order. Field 64 is this firmware's own: {1 version, 2 repeated Slot{1 type, 2 kind, 3 index, 4 name, 5 nth, 6 amount (sint32),
// 7 shape, 8 speed, 9 attack ms, 10 decay ms, 11 sustain %, 12 release ms}}, varints, zero omitted, an Off slot is empty, trailing Off
// slots are not written, the whole field is left out when every slot is Off. A version above 1 is kept raw (a later image's block), a
// malformed block is dropped (automation Off, the sound still loads). Ranges are checked at import (open_cold.cpp).
// Field 100: macro depth mode (varint, 1 = Scale, absent = Add; written after the automation block and only for Scale).
// Built at -Os (cold code).
#include "open_patchdata.h"

#include "PolySynth/config.h"

#include "model/synth_patch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace polysynth::open
{
namespace
{

struct Reader
{
    const uint8_t* p;
    const uint8_t* end;

    bool varint(uint64_t& v)
    {
        v = 0;
        for (int i = 0; i < 10 && p < end; i++)
        {
            const uint8_t b = *p++;
            v |= uint64_t(b & 0x7F) << (7 * i);
            if (!(b & 0x80))
            {
                return true;
            }
        }
        return false;
    }
    bool fixed32(uint32_t& v)
    {
        if (end - p < 4)
        {
            return false;
        }
        std::memcpy(&v, p, 4); // little endian host (Cortex-M and the x86 test host)
        p += 4;
        return true;
    }
    bool len(Reader& sub)
    {
        uint64_t n;
        if (!varint(n) || n > uint64_t(end - p))
        {
            return false;
        }
        sub = { p, p + n };
        p += n;
        return true;
    }
    // tag, then the value is left to the caller except for the types it does not know
    bool tag(uint32_t& field, uint8_t& wt)
    {
        uint64_t t;
        if (!varint(t) || t < 8 || (t >> 3) > 0x1FFFFFFFu)
        {
            return false;
        }
        field = uint32_t(t >> 3);
        wt = uint8_t(t & 7);
        return wt == 0 || wt == 1 || wt == 2 || wt == 5;
    }
    bool skip(uint8_t wt)
    {
        uint64_t v;
        Reader s;
        switch (wt)
        {
            case 0: return varint(v);
            case 1:
                if (end - p < 8)
                {
                    return false;
                }
                p += 8;
                return true;
            case 5:
                if (end - p < 4)
                {
                    return false;
                }
                p += 4;
                return true;
            default: return len(s);
        }
    }
};

float bitsToFloat(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

uint32_t floatToBits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

bool decodeMacro(Reader r, PatchMacroData& m)
{
    int nt = 0;
    uint32_t f;
    uint8_t wt;
    while (r.p < r.end)
    {
        if (!r.tag(f, wt))
        {
            return false;
        }
        Reader s;
        uint64_t v;
        if (f == 1 && wt == 2)
        {
            if (!r.len(s) || s.end - s.p > 32)
            {
                return false;
            }
            const int n = std::min<int>(int(s.end - s.p), kMacroNameMax);
            std::memcpy(m.name, s.p, n);
            m.name[n] = 0;
        }
        else if (f == 2 && wt == 0)
        {
            if (!r.varint(v))
            {
                return false;
            }
            m.bipolar = v != 0;
        }
        else if (f == 3 && wt == 0)
        {
            if (!r.varint(v) || v > 0x7FFFFFFFu)
            {
                return false;
            }
            m.count = int8_t(std::min<uint64_t>(v, kMacroTargets));
        }
        else if (f == 4 && wt == 2)
        {
            if (nt >= kMacroTargets || !r.len(s))
            {
                return false;
            }
            while (s.p < s.end)
            {
                if (!s.tag(f, wt))
                {
                    return false;
                }
                if (f == 1 && wt == 0)
                {
                    if (!s.varint(v))
                    {
                        return false;
                    }
                    m.param[nt] = int16_t(v > 0x7FFF ? -1 : int(v));
                }
                else if (f == 2 && wt == 5)
                {
                    uint32_t b;
                    if (!s.fixed32(b))
                    {
                        return false;
                    }
                    m.amount[nt] = bitsToFloat(b);
                }
                else if (!s.skip(wt))
                {
                    return false;
                }
            }
            nt++;
        }
        else if (!r.skip(wt))
        {
            return false;
        }
    }
    return true;
}

// A slot as the file has it: every field absent is zero (not the editor's defaults)
ModSlot modZero()
{
    ModSlot z;
    z.shape = 0;
    z.speed = 0;
    z.index = 0;
    z.amount = 0;
    z.sustain = 0;
    z.attack = 0;
    z.decay = 0;
    z.release = 0;
    return z;
}

// 1: decoded into out, 0: malformed (dropped), 2: another version (the caller keeps the bytes raw)
int decodeMod(Reader r, ModSlot* out)
{
    ModSlot tmp[kModSlots];
    int n = 0;
    uint64_t version = 0;
    uint32_t f;
    uint8_t wt;
    while (r.p < r.end)
    {
        Reader s;
        uint64_t v;
        if (!r.tag(f, wt))
        {
            return 0;
        }
        if (f == 1 && wt == 0)
        {
            if (!r.varint(version))
            {
                return 0;
            }
        }
        else if (f == 2 && wt == 2)
        {
            if (!r.len(s))
            {
                return 0;
            }
            if (n >= kModSlots)
            {
                continue; // more slots than this image has: the first ones
            }
            ModSlot m = modZero();
            while (s.p < s.end)
            {
                if (!s.tag(f, wt))
                {
                    return 0;
                }
                if (f == 4 && wt == 2)
                {
                    Reader t;
                    if (!s.len(t))
                    {
                        return 0;
                    }
                    const int len = std::min<int>(int(t.end - t.p), kModNameMax);
                    std::memcpy(m.name, t.p, len);
                    m.name[len] = 0;
                    continue;
                }
                if (wt != 0)
                {
                    if (!s.skip(wt))
                    {
                        return 0;
                    }
                    continue;
                }
                if (!s.varint(v))
                {
                    return 0;
                }
                const uint32_t c = uint32_t(std::min<uint64_t>(v, 0xFFFFu)); // every field but the amount is small
                switch (f)
                {
                    case 1: m.type = ModType(std::min<uint32_t>(c, 0xFF)); break;
                    case 2: m.kind = ModKind(std::min<uint32_t>(c, 0xFF)); break;
                    case 3: m.index = int16_t(std::min<uint32_t>(c, 0x7FFF)); break;
                    case 5: m.nth = uint8_t(std::min<uint32_t>(c, 0xFF)); break;
                    case 6:
                    {
                        const int64_t a = int64_t(v >> 1) ^ -int64_t(v & 1); // zigzag
                        m.amount = int8_t(std::clamp<int64_t>(a, -kModAmountMax, kModAmountMax));
                        break;
                    }
                    case 7: m.shape = uint8_t(std::min<uint32_t>(c, 0xFF)); break;
                    case 8: m.speed = uint8_t(std::min<uint32_t>(c, 0xFF)); break;
                    case 9: m.attack = uint16_t(c); break;
                    case 10: m.decay = uint16_t(c); break;
                    case 11: m.sustain = uint8_t(std::min<uint32_t>(c, 0xFF)); break;
                    case 12: m.release = uint16_t(c); break;
                    default: break; // a later field of this version: skipped
                }
            }
            tmp[n++] = m;
        }
        else if (!r.skip(wt))
        {
            return 0;
        }
    }
    if (version != 1)
    {
        return 2;
    }
    for (int i = 0; i < kModSlots; i++)
    {
        out[i] = i < n ? tmp[i] : ModSlot{};
    }
    return 1;
}

struct Writer
{
    uint8_t* p;
    uint8_t* end;
    bool ok = true;

    void byte(uint8_t b)
    {
        if (p < end)
        {
            *p++ = b;
        }
        else
        {
            ok = false;
        }
    }
    void bytes(const void* src, size_t n)
    {
        if (size_t(end - p) < n)
        {
            ok = false;
            return;
        }
        std::memcpy(p, src, n);
        p += n;
    }
    void varint(uint64_t v)
    {
        while (v >= 0x80)
        {
            byte(uint8_t(v) | 0x80);
            v >>= 7;
        }
        byte(uint8_t(v));
    }
    void tag(uint32_t field, uint8_t wt) { varint((uint64_t(field) << 3) | wt); }
    void fixed32(uint32_t v) { bytes(&v, 4); }
    void sub(uint32_t field, const Writer& body, const uint8_t* start)
    {
        tag(field, 2);
        varint(size_t(body.p - start));
        bytes(start, size_t(body.p - start));
    }
};

// The constant Extra block (1293 bytes) the stock encoder always writes: a = 0A 08 | 12 04 08 03 10 01 | 1A 00,
// b = 12 <1280> then 64 x (12 12 | 12 10 <16 zero bytes>).
constexpr size_t kExtraSize = 1293;
void writeExtra(Writer& w)
{
    static const uint8_t kHead[] = { 0x0A, 0x08, 0x12, 0x04, 0x08, 0x03, 0x10, 0x01, 0x1A, 0x00, 0x12, 0x80, 0x0A };
    w.tag(9, 2);
    w.varint(kExtraSize);
    w.bytes(kHead, sizeof(kHead));
    static const uint8_t kRow[4] = { 0x12, 0x12, 0x12, 0x10 };
    static const uint8_t kZero[16] = {};
    for (int i = 0; i < 64; i++)
    {
        w.bytes(kRow, 4);
        w.bytes(kZero, 16);
    }
}

void writeMod(Writer& w, const ModSlot* mod)
{
    int last = -1;
    for (int i = 0; i < kModSlots; i++)
    {
        if (mod[i].type != ModType::kOff)
        {
            last = i;
        }
    }
    if (last < 0)
    {
        return;
    }
    uint8_t buf[kModBlockMax];
    Writer b{ buf, buf + sizeof(buf) };
    b.tag(1, 0);
    b.byte(1);
    for (int i = 0; i <= last; i++)
    {
        const ModSlot& m = mod[i];
        uint8_t sb[64];
        Writer s{ sb, sb + sizeof(sb) };
        if (m.type != ModType::kOff)
        {
            auto put = [&](uint32_t field, uint32_t v)
            {
                if (v)
                {
                    s.tag(field, 0);
                    s.varint(v);
                }
            };
            put(1, uint32_t(m.type));
            put(2, uint32_t(m.kind));
            put(3, uint32_t(std::max<int>(m.index, 0)));
            const size_t n = strnlen(m.name, kModNameMax);
            if (n)
            {
                s.tag(4, 2);
                s.varint(n);
                s.bytes(m.name, n);
            }
            put(5, m.nth);
            put(6, uint32_t((int32_t(m.amount) << 1) ^ (int32_t(m.amount) >> 31))); // zigzag
            put(7, m.shape);
            put(8, m.speed);
            put(9, m.attack);
            put(10, m.decay);
            put(11, m.sustain);
            put(12, m.release);
        }
        b.ok = b.ok && s.ok;
        b.sub(2, s, sb);
    }
    w.ok = w.ok && b.ok;
    w.sub(kModField, b, buf);
}

} // namespace

#ifdef OPEN_HOST_TEST
bool gModDecodeOff = false;
#endif

bool modAnyOn(const ModSlot* slots)
{
    for (int i = 0; i < kModSlots; i++)
    {
        if (slots[i].type != ModType::kOff)
        {
            return true;
        }
    }
    return false;
}

// engineIdForType and typeForEngineId: open_cold.cpp, from the engine list (built in and installed plugin engines)

bool decodePatch(const uint8_t* data, size_t len, PatchData& out)
{
    if (len > kPatchFileMax)
    {
        return false;
    }
    for (float& v : out.values)
    {
        v = NAN;
    }
    Reader r{ data, data + len };
    int nMacros = 0;
    uint32_t f;
    uint8_t wt;
    while (r.p < r.end)
    {
        const uint8_t* start = r.p;
        if (!r.tag(f, wt))
        {
            return false;
        }
        Reader s;
        uint64_t v;
        if (f == 1 && wt == 0)
        {
            if (!r.varint(v))
            {
                return false;
            }
            out.engine = int(v);
        }
        else if (f == 6 && wt == 2)
        {
            if (out.nParams >= kMaxParams || !r.len(s))
            {
                return false;
            }
            float* dst = &out.values[out.nParams++];
            while (s.p < s.end)
            {
                if (!s.tag(f, wt))
                {
                    return false;
                }
                uint32_t b;
                if (f == 1 && wt == 5)
                {
                    if (!s.fixed32(b))
                    {
                        return false;
                    }
                    *dst = bitsToFloat(b);
                }
                else if (!s.skip(wt))
                {
                    return false;
                }
            }
        }
        else if (f == kMacroModeField && wt == 0)
        {
            if (!r.varint(v))
            {
                return false;
            }
            out.macroMode = v == kMacroModeScale ? kMacroModeScale : kMacroModeAdd; // unknown modes read as Add
        }
        else if (f == 8 && wt == 2)
        {
            if (nMacros >= kPatchMacros || !r.len(s) || !decodeMacro(s, out.macros[nMacros++]))
            {
                return false;
            }
        }
        else
        {
            if (!r.skip(wt))
            {
                return false;
            }
            if (f == 9)
            {
                continue; // Extra: contents ignored, regenerated on save
            }
#ifdef OPEN_HOST_TEST
            if (f == kModField && wt == 2 && !gModDecodeOff)
#else
            if (f == kModField && wt == 2)
#endif
            {
                Reader whole{ start, r.p };
                Reader body{}; // set by len(), or the parse is refused below, never read uninitialised
                if (!whole.tag(f, wt) || !whole.len(body))
                {
                    return false; // cannot happen (whole is the span just parsed), but do not decode a half set Reader
                }
                const int got = decodeMod(body, out.mod);
                if (got == 0)
                {
                    for (ModSlot& m : out.mod)
                    {
                        m = ModSlot{}; // malformed: automation Off, the rest of the file still loads
                    }
                }
                if (got != 2)
                {
                    continue;
                }
                // another version: kept raw, as an image without automation keeps it
            }
            const size_t n = size_t(r.p - start);
            if (out.extra.len + n > kRawMax)
            {
                return false;
            }
            std::memcpy(out.extra.bytes + out.extra.len, start, n);
            out.extra.len = uint16_t(out.extra.len + n);
            out.extra.hasNames |= f == 7;
        }
    }
    out.nMacros = uint8_t(nMacros);
    return true;
}

size_t encodePatch(const PatchData& p, uint8_t* out, size_t cap)
{
    Writer w{ out, out + cap };
    size_t pos = 0;
    // A raw field 64 (a later version's block) gives way to the automation of this patch when it has some
    const bool ownMod = modAnyOn(p.mod);
    // Raw fields go in file order ahead of the first known field with a higher number.
    auto flush = [&](uint32_t below)
    {
        while (pos < p.extra.len)
        {
            Reader r{ p.extra.bytes + pos, p.extra.bytes + p.extra.len };
            uint32_t f;
            uint8_t wt;
            if (!r.tag(f, wt) || !r.skip(wt))
            {
                w.ok = false;
                return;
            }
            if (f >= below)
            {
                return;
            }
            if (!(ownMod && f == kModField))
            {
                w.bytes(p.extra.bytes + pos, size_t(r.p - (p.extra.bytes + pos)));
            }
            pos = size_t(r.p - p.extra.bytes);
        }
    };

    flush(1);
    w.tag(1, 0);
    w.varint(uint64_t(p.engine));

    flush(6);
    for (int i = 0; i < p.nParams; i++)
    {
        w.tag(6, 2);
        if (std::isnan(p.values[i]))
        {
            w.byte(0);
        }
        else
        {
            w.byte(5);
            w.tag(1, 5);
            w.fixed32(floatToBits(p.values[i]));
        }
    }

    flush(7);
    if (!p.extra.hasNames)
    {
        for (int i = 0; i < 4; i++)
        {
            w.tag(7, 2);
            w.byte(0);
        }
    }
    flush(8);
    for (int m = 0; m < kPatchMacros; m++)
    {
        const PatchMacroData& md = p.macros[m];
        uint8_t buf[160];
        Writer s{ buf, buf + sizeof(buf) };
        if (md.name[0])
        {
            s.tag(1, 2);
            const size_t n = std::strlen(md.name);
            s.varint(n);
            s.bytes(md.name, n);
        }
        if (md.bipolar)
        {
            s.tag(2, 0);
            s.byte(1);
        }
        if (md.count)
        {
            s.tag(3, 0);
            s.varint(uint64_t(md.count));
        }
        for (int t = 0; t < kMacroTargets; t++)
        {
            uint8_t tb[16];
            Writer ts{ tb, tb + sizeof(tb) };
            if (md.param[t] > 0)
            {
                ts.tag(1, 0);
                ts.varint(uint64_t(md.param[t]));
            }
            if (floatToBits(md.amount[t]) != 0)
            {
                ts.tag(2, 5);
                ts.fixed32(floatToBits(md.amount[t]));
            }
            s.sub(4, ts, tb);
        }
        w.sub(8, s, buf);
    }

    flush(9);
    writeExtra(w);
    flush(kModField);
    writeMod(w, p.mod);
    flush(kMacroModeField);
    if (p.macroMode == kMacroModeScale)
    {
        w.tag(kMacroModeField, 0);
        w.byte(kMacroModeScale);
    }
    flush(0xFFFFFFFFu);
    return w.ok && pos == p.extra.len ? size_t(w.p - out) : 0;
}

} // namespace polysynth::open
