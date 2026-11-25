#pragma once
// IMX port P7 H6 (ref/ff-imx-port-plan.md R2.1e): the run time engine list of the stock synth path (no FEATURE_SYNTH_OPEN, Tracker+ and
// Mini). The stock list was a fixed array of the 7 built-in types (Synth::kSynthTypes). Now the built-ins come first under their stock
// numbers (stored 0..6 = position in the stock picker, the numbers a stock project already has), then the plugin engines by stored number.
// A project stores the engine's number, never its position here.
// - Stored 6 is the built-in Chiptune. chip.plg has stored 6 too and is refused (plan D10: the built-in stays on IMX), as is any
//   plugin whose stored number or type is taken (the plug_abi.h rule of the MK66 list).
// - Missing: a stored number in the plugin range that is not in the list (a project from a unit with more plugins), or a listed plugin
//   that is not attached (no PlugVoice for it yet). The part keeps its number and its patch files, it plays nothing, its editor pages
//   stay closed, and the number comes back when the engine is there.
// Header only, no polysynth types, so the host test (synth-open/tools/synth_types_test.cpp) builds it as the image does.
#include <cstdint>
#include <cstdio>

namespace synthtypes
{
constexpr int kBuiltins = 7;           // ACD FAT VAP WTFM PERC DIRT CHIP (core/synth/registry.cpp)
constexpr int kPlugStoredFirst = 7;    // 6 is the built-in Chiptune on IMX (D10), MK66 plugins start at 6 (open_engine.h)
constexpr int kStoredLast = 255;       // a project stores the engine as one byte
constexpr int kMax = 64;               // entries: 7 built-ins and the plugin engines (the 1.4 set has 12)

struct Entry
{
    int16_t stored;
    int16_t builtin;   // position among the built-ins (their list icon), -1 for a plugin engine (shown by its name)
    int32_t typeId;    // polysynth::InstrumentTypeId
    const char* name;
    bool attached;     // a plugin engine that can play now (built-ins always)
};

class List
{
  public:
    // The built-ins, in the stock order, under stored 0..kBuiltins-1
    void reset(const int32_t (&builtinTypes)[kBuiltins], const char* const (&builtinNames)[kBuiltins])
    {
        n_ = 0;
        for (int i = 0; i < kBuiltins; i++)
        {
            e_[n_++] = Entry{ int16_t(i), int16_t(i), builtinTypes[i], builtinNames[i], true };
        }
    }
    // A plugin engine, kept in stored number order after the built-ins. False: the list is full, the number is out of the plugin
    // range, or the number or the type is taken (CHIP's 6 among them).
    bool add(int stored, int32_t typeId, const char* name, bool attached)
    {
        if (n_ >= kMax || stored < kPlugStoredFirst || stored > kStoredLast || !name)
        {
            return false;
        }
        int at = n_;
        for (int i = 0; i < n_; i++)
        {
            if (e_[i].stored == stored || e_[i].typeId == typeId)
            {
                return false;
            }
            if (at == n_ && e_[i].builtin < 0 && e_[i].stored > stored)
            {
                at = i;
            }
        }
        for (int i = n_; i > at; i--)
        {
            e_[i] = e_[i - 1];
        }
        e_[at] = Entry{ int16_t(stored), -1, typeId, name, attached };
        n_++;
        return true;
    }
    bool setAttached(int stored, bool attached)
    {
        const int ui = uiForStored(stored);
        if (ui < 0 || e_[ui].builtin >= 0)
        {
            return false;
        }
        e_[ui].attached = attached;
        return true;
    }
    int count() const { return n_; }
    const Entry& at(int ui) const { return e_[ui]; }
    int uiForStored(int stored) const
    {
        for (int i = 0; i < n_; i++)
        {
            if (e_[i].stored == stored)
            {
                return i;
            }
        }
        return -1;
    }
    // A number a project may hold: a listed engine, or a plugin number this build has not got (Missing)
    static bool storedValid(int stored) { return stored >= 0 && stored <= kStoredLast; }
    bool missing(int stored) const
    {
        const int ui = uiForStored(stored);
        return ui < 0 ? (stored >= kPlugStoredFirst && stored <= kStoredLast) : !e_[ui].attached;
    }
    // The list entry's name, else "#<number>"
    const char* name(int stored) const
    {
        const int ui = uiForStored(stored);
        if (ui >= 0)
        {
            return e_[ui].name;
        }
        snprintf(unknown_, sizeof(unknown_), "#%d", stored);
        return unknown_;
    }

  private:
    Entry e_[kMax];
    int n_ = 0;
    mutable char unknown_[8] = {};
};

}  // namespace synthtypes
