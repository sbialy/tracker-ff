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
