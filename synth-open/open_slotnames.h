// Open PolySynth shim for MK66 - names of the patch slots of a part (the side file <P>.names).
// Plain C++, header only, no PolySynth includes: the codec is tested on the host without the core.
//
// The .patch files carry no name, so the name a slot shows on the Synth screen is kept in a small text file next to the slot
// files, in Workspace/ and in the project folder (copyWorkspacePatches copies it like a slot file, the project open prunes it in the Workspace):
//
//     tffnames 1\n
//     b=<name>\n               the name of the slot 0 backup of an engine change (<P>-b.patch), when there is one
//     <slot>=<name>\n          one line per named slot, slot 1..15 in decimal, ascending
//     m=<slot>\n               the Morph target of the part (Morph step FX), 0..15, only when it is not kMorphDefault
//
// Slot 0 is not in the file (the project stores its name), name[0] of SlotNames is the backup's. A name is 1..31 bytes of 0x20..0x7E or higher, never a control
// character. The reader takes lines as they come, skips what it does not understand (unknown slot, empty or over-long name, control
// character, duplicate keeps the last) and never reads or writes past its buffers. A file without the first line is ignored whole.
// Stock firmware never opens the file. Firmware without the Morph FX skips the m= line (no digit before '=') and drops it when
// it writes the file again (a slot rename), the part then morphs to kMorphDefault.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace polysynth::open
{

constexpr int kSlotNameSlots = 16;           // equals kPatchBankSlots
constexpr size_t kSlotNameMax = 31;          // bytes of a name (the project's patchName is char[32] too)
constexpr size_t kSlotNamesFileMax = 1024;   // 11 + 15 * (2 + 1 + 31 + 1) = 536 bytes at most, a longer file is cut here
constexpr const char* kSlotNamesExt = ".names";

constexpr int kMorphDefault = 1;             // Morph target of a part whose file has no m= line

struct SlotNames
{
    char name[kSlotNameSlots][kSlotNameMax + 1] = {}; // [0] is the name of the slot 0 backup, an empty string is "no name"
    int8_t morph = -1;                                // Morph target 0..15, -1: not in the file (kMorphDefault)
};

// features-140: the same as out = SlotNames(), in place. The assignment builds a temporary of the whole struct on the stack once
// morph has an initializer (patchNamesRead and decodeSlotNames crossed the 512 B frame rule of the stack ratchet)
inline void clearSlotNames(SlotNames& out)
{
    std::memset(out.name, 0, sizeof(out.name));
    out.morph = -1;
}

// The file has something to say about the Morph target: a target other than the default
inline bool morphTargetSet(const SlotNames& n)
{
    return n.morph >= 0 && n.morph < kSlotNameSlots && n.morph != kMorphDefault;
}

namespace slotnames_detail
{
constexpr char kHeader[] = "tffnames 1\n";
constexpr size_t kHeaderLen = sizeof(kHeader) - 1;
inline bool printable(unsigned char c)
{
    return c >= 0x20 && c != 0x7F;
}
} // namespace slotnames_detail

// Bytes written to `out` (cap >= kSlotNamesFileMax is always enough), 0 when cap is too small. A name with a control
// character is written up to it, a name longer than kSlotNameMax is cut.
inline size_t encodeSlotNames(const SlotNames& in, uint8_t* out, size_t cap)
{
    using namespace slotnames_detail;
    if (cap < kHeaderLen)
    {
        return 0;
    }
    size_t n = kHeaderLen;
    std::memcpy(out, kHeader, kHeaderLen);
    for (int slot = 0; slot < kSlotNameSlots; slot++) // 0 is the backup, written as "b"
    {
        size_t len = 0;
        while (len < kSlotNameMax && in.name[slot][len] && printable(static_cast<unsigned char>(in.name[slot][len])))
        {
            len++;
        }
        if (len == 0)
        {
            continue;
        }
        const size_t digits = slot >= 10 ? 2 : 1;
        if (n + digits + 1 + len + 1 > cap)
        {
            return 0;
        }
        if (slot == 0)
        {
            out[n++] = 'b';
        }
        else
        {
            if (slot >= 10)
            {
                out[n++] = uint8_t('0' + slot / 10);
            }
            out[n++] = uint8_t('0' + slot % 10);
        }
        out[n++] = '=';
        std::memcpy(out + n, in.name[slot], len);
        n += len;
        out[n++] = '\n';
    }
    if (morphTargetSet(in))
    {
        if (n + 6 > cap)
        {
            return 0;
        }
        out[n++] = 'm';
        out[n++] = '=';
        if (in.morph >= 10)
        {
            out[n++] = uint8_t('0' + in.morph / 10);
        }
        out[n++] = uint8_t('0' + in.morph % 10);
        out[n++] = '\n';
    }
    return n;
}

// Parses `len` bytes. `out` is left holding the names found, all empty when nothing valid was there. False when the header is
// missing (nothing is taken from such a file).
inline bool decodeSlotNames(const uint8_t* data, size_t len, SlotNames& out)
{
    using namespace slotnames_detail;
    clearSlotNames(out);
    if (!data || len < kHeaderLen || std::memcmp(data, kHeader, kHeaderLen) != 0)
    {
        return false;
    }
    if (len > kSlotNamesFileMax)
    {
        len = kSlotNamesFileMax;
    }
    size_t i = kHeaderLen;
    while (i < len)
    {
        size_t end = i;
        while (end < len && data[end] != '\n')
        {
            end++;
        }
        size_t lineEnd = end;
        if (lineEnd > i && data[lineEnd - 1] == '\r')
        {
            lineEnd--;
        }
        // <slot>=<name>, or b=<name> for the backup (stored as slot 0)
        size_t p = i;
        int slot = 0;
        size_t digits = 0;
        bool backup = false;
        if (p + 1 < lineEnd && data[p] == 'm' && data[p + 1] == '=')
        {   // m=<0..15>: the Morph target
            int v = 0;
            size_t q = p + 2;
            while (q < lineEnd && q < p + 4 && data[q] >= '0' && data[q] <= '9')
            {
                v = v * 10 + (data[q] - '0');
                q++;
            }
            if (q == lineEnd && q > p + 2 && v < kSlotNameSlots)
            {
                out.morph = int8_t(v);
            }
            i = end + 1;
            continue;
        }
        if (p + 1 < lineEnd && data[p] == 'b' && data[p + 1] == '=')
        {
            backup = true;
            digits = 1;
            p++;
        }
        else
        {
            while (p < lineEnd && data[p] >= '0' && data[p] <= '9' && digits < 3)
            {
                slot = slot * 10 + (data[p] - '0');
                p++;
                digits++;
            }
        }
        if (digits > 0 && p < lineEnd && data[p] == '=' && (backup || (slot >= 1 && slot < kSlotNameSlots)))
        {
            p++;
            const size_t nameLen = lineEnd - p;
            bool ok = nameLen >= 1 && nameLen <= kSlotNameMax;
            for (size_t k = 0; ok && k < nameLen; k++)
            {
                ok = printable(data[p + k]);
            }
            if (ok)
            {
                std::memcpy(out.name[slot], data + p, nameLen);
                out.name[slot][nameLen] = 0;
            }
        }
        i = end + 1;
    }
    return true;
}

inline bool anySlotName(const SlotNames& n)
{
    for (int slot = 0; slot < kSlotNameSlots; slot++)
    {
        if (n.name[slot][0])
        {
            return true;
        }
    }
    return false;
}

} // namespace polysynth::open
