// Index logic of the Inserts page lists (plug_ui.cpp), kept free of the firmware so the host test
// (synth-open/tools/plug_ui_nav_test.cpp) runs it on the Mac. It mirrors the stock master screen
// (core/interface/masterParams/fMasterParams.cpp):
// - a list moves one entry DOWN on the Down arrow and up on Up (functUp/functDown call changeSaturationType(-1) and (1),
//   list index + 1 is the next entry down), a numeric value goes up on Up (changeBassBoost(1))
// - the encoder moves a list by the value, negated with interfaceGlobals.invertValForList (functEncoder case 5)
// - arrows and the encoder stop at both ends of a list (changeSaturationType clamps)
// - pressing the column button of an already selected list steps to the next entry and wraps from the last to the first
//   (functSelectSaturation)
#pragma once

namespace plugnav
{
// Inserts page columns: Mixer, Insert 1, Insert 2, four parameters of the focused insert, Back
constexpr int kMixerCol = 0;
constexpr int kInsertCol0 = 1;   // insert k (0, 1) sits in column kInsertCol0 + k
constexpr int kParamCol0 = 3;
constexpr int kParamCols = 4;    // columns 3..6
constexpr int kBackCol = 7;

// The insert (0, 1) whose plugin list is column col, -1 for every other column
constexpr int insertOfCol(int col)
{
    return col == kInsertCol0 || col == kInsertCol0 + 1 ? col - kInsertCol0 : -1;
}

constexpr int colOfInsert(int k)
{
    return kInsertCol0 + k;
}

// The value line of an insert column. 9 characters fit the 97 px column in the value font (as the master screen's
// Saturation value field, plug_ui.cpp plugUiSatText); plug_ui.cpp also clips to the measured width.
constexpr int kLabelChars = 9;
enum class Shown
{
    kNone,
    kLoading,
    kName,       // running: the plugin's name
    kBypassed,
    kMissing,    // not on the card
    kFailed,
    kSafe,       // not loaded: safe mode (a crash marker or Shift at power on)
    kOverCpu,    // tripped its CPU budget
};

// out (at least kLabelChars + 1 bytes): a status word, the name, or a short tag and as much of the name as fits ("Safe:BBD")
inline void slotLabel(Shown st, const char* name, char* out)
{
    const char* tag = nullptr;
    const char* word = nullptr;
    switch (st)
    {
        case Shown::kNone: word = "None"; break;
        case Shown::kLoading: word = "Loading"; break;
        case Shown::kBypassed: word = "Bypassed"; break;
        case Shown::kMissing: tag = "Miss:"; break;
        case Shown::kFailed: tag = "Fail:"; break;
        case Shown::kSafe: tag = "Safe:"; break;
        case Shown::kOverCpu: tag = "CPU:"; break;
        case Shown::kName: tag = ""; break;
    }
    int n = 0;
    if (word)
        for (; word[n] && n < kLabelChars; n++) out[n] = word[n];
    else
    {
        for (; tag[n] && n < kLabelChars; n++) out[n] = tag[n];
        for (int i = 0; name && name[i] && n < kLabelChars; i++) out[n++] = name[i];
    }
    out[n] = 0;
}

enum Arrow
{
    kLeft = 0,
    kRight = 1,
    kUp = 2,
    kDown = 3,
};

// The step of an Up or Down arrow: lists +1 on Down (the next entry below), numbers +1 on Up
constexpr int arrowDelta(int dir, bool list)
{
    return list ? (dir == kDown ? 1 : -1) : (dir == kUp ? 1 : -1);
}

// The step of an encoder turn
constexpr int encoderDelta(int value, bool list, bool invertList)
{
    return list && invertList ? -value : value;
}

// pos moved by delta, held inside [0, n)
constexpr int clampStep(int pos, int n, int delta)
{
    if (n <= 0) return 0;
    const int p = pos + delta;
    return p < 0 ? 0 : (p >= n ? n - 1 : p);
}

// The entry after pos, the first after the last (a second press on the selected column)
constexpr int nextWrap(int pos, int n)
{
    if (n <= 0) return 0;
    return pos < 0 || pos >= n - 1 ? 0 : pos + 1;
}
}   // namespace plugnav
