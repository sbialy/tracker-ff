// Host test of the Inserts page list index logic (synth-open/plug_ui_nav.h): arrow direction against the stock master screen,
// clamping at both ends, the repeat-press wrap, and that no step ever leaves [0, n).
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_ui_nav_test.cpp -o /tmp/plug_ui_nav_test && /tmp/plug_ui_nav_test
#include "plug_ui_nav.h"

#include <cstdio>
#include <cstring>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
}   // namespace


// A model of the Master module's screen switching, line for line the control flow of core/interface/masterParams/
// fMasterParams.cpp (functSwitchModeMaster, switchScreen, returnToScreen, switchToMaster/Mixer/MixerDelayReverb, start())
// with the FEATURE_PLUG_HOST hooks. The model only tracks which screen is up. plugnav::Stop is the reference order.
namespace
{
enum class Disp { kValues, kMixer, kMixerDelayReverb };

struct MasterModel
{
    bool inCycle = false;       // plugUiInCycle(): a plugin on the card or an insert slot assigned
    bool onMaster = true;       // the Master module is the page on screen
    Disp disp = Disp::kValues;
    bool inserts = false;       // plugUiActive(): the Inserts page is up (displayType stays kValues)
    bool delaySub = false;      // a values sub-screen (Reverb, Delay, Limiter, EQ)
    bool peeking = false;       // !showMasterVolumeOnHold
    Disp snapDisp = Disp::kValues;
    bool snapSub = false;
    bool volumeScreen = false;  // the master volume peek is up
    int shownTitlePages = 3;

    void toMaster() { inserts = false; disp = Disp::kValues; delaySub = false; }
    void toMixer() { inserts = false; disp = Disp::kMixer; }
    void toMixerDR() { inserts = false; disp = Disp::kMixerDelayReverb; }
    void openInserts() { inserts = true; }
    plugnav::Stop stop() const
    {
        if (inserts) return plugnav::Stop::kInserts;
        return disp == Disp::kValues ? plugnav::Stop::kValues
                                     : disp == Disp::kMixer ? plugnav::Stop::kMixer : plugnav::Stop::kGlobalMixer;
    }

    void switchScreen()
    {
        if (inserts) { toMaster(); return; }
        if (disp == Disp::kValues) toMixer();
        else if (disp == Disp::kMixerDelayReverb)
        {
            toMaster();
            if (inCycle) openInserts();
        }
        else if (disp == Disp::kMixer) toMixerDR();
    }
    void returnToScreen()
    {
        if (snapDisp == Disp::kMixer) toMixer();
        else if (snapDisp == Disp::kValues) toMaster();   // the sub-screens are not modelled apart
        else toMixerDR();
    }
    void pressMaster()
    {
        snapDisp = disp;
        snapSub = delaySub;
        peeking = false;
    }
    void holdMaster()
    {
        if (!peeking)
        {
            peeking = true;
            toMaster();
            volumeScreen = true;
        }
    }
    void releaseMaster()
    {
        if (!peeking) switchScreen();
        else returnToScreen();
        volumeScreen = false;
        peeking = false;
    }
    void tap() { pressMaster(); releaseMaster(); }
    void hold()
    {
        pressMaster();
        holdMaster();
        holdMaster();   // auto-repeat: only the first hold event acts
        releaseMaster();
    }
    // Shift + Saturation on the Master values screen
    void shiftSaturation()
    {
        if (onMaster && disp == Disp::kValues && !inserts && !delaySub) openInserts();
    }
    // Pattern, Song, ... Params, any top-row button but Master: the Master module stops, its screen state stays
    void leave() { onMaster = false; }
    // Master pressed from another page: start() shows the remembered screen, the values one through switchToMaster
    void enterMaster()
    {
        onMaster = true;
        if (disp == Disp::kValues) toMaster();
        else if (disp == Disp::kMixer) toMixer();
        else toMixerDR();
    }
};
}   // namespace

int main()
{
    using namespace plugnav;
    // stock fMasterParams.cpp: functDown -> changeSaturationType(1), functUp -> changeSaturationType(-1),
    // functUp -> changeBassBoost(1)
    check(arrowDelta(kDown, true) == 1, "list: Down is the next entry below (+1)");
    check(arrowDelta(kUp, true) == -1, "list: Up is the entry above (-1)");
    check(arrowDelta(kUp, false) == 1, "number: Up raises (+1)");
    check(arrowDelta(kDown, false) == -1, "number: Down lowers (-1)");
    check(encoderDelta(3, true, false) == 3 && encoderDelta(-2, true, false) == -2, "encoder on a list, original: as turned");
    check(encoderDelta(3, true, true) == -3, "encoder on a list, Mini (invertValForList): negated");
    check(encoderDelta(3, false, true) == 3, "encoder on a number: never negated");

    // the plugin list: None + 7 plugins
    const int n = 8;
    int pos = 0;
    for (int i = 0; i < 20; i++) pos = clampStep(pos, n, arrowDelta(kDown, true));
    check(pos == n - 1, "20 x Down from None stops on the last plugin");
    for (int i = 0; i < 20; i++) pos = clampStep(pos, n, arrowDelta(kUp, true));
    check(pos == 0, "20 x Up stops on None");
    check(clampStep(0, n, -100) == 0 && clampStep(3, n, 100) == n - 1, "big encoder steps clamp");
    check(clampStep(0, 0, 1) == 0 && clampStep(5, 1, 1) == 0, "empty and one-entry lists stay at 0");

    // repeat press: next entry, the first after the last
    check(nextWrap(0, 3) == 1 && nextWrap(1, 3) == 2 && nextWrap(2, 3) == 0, "Mixer (3 entries) wraps 2 -> 0");
    check(nextWrap(0, 2) == 1 && nextWrap(1, 2) == 0, "two-entry list toggles");
    check(nextWrap(0, 1) == 0 && nextWrap(0, 0) == 0, "one and zero entries stay at 0");
    check(nextWrap(-1, 4) == 0 && nextWrap(9, 4) == 0, "out of range position restarts at the first");
    {
        int p = 0, seen = 0;
        for (int i = 0; i < 4; i++)
        {
            p = nextWrap(p, 4);
            seen |= 1 << p;
        }
        check(p == 0 && seen == 0xF, "4 presses on a 4-entry enum visit every entry and come back");
    }

    // bounds over every list size up to the insert picker's (None + kIndexMax 272 = 273, plug_ui.cpp kPickMax) and every start
    bool inRange = true;
    for (int len = 1; len <= 300; len++)
        for (int p = 0; p < len; p++)
            for (int d = -60; d <= 60; d++)
            {
                const int a = clampStep(p, len, d), b = nextWrap(p, len);
                inRange = inRange && a >= 0 && a < len && b >= 0 && b < len;
            }
    check(inRange, "clampStep and nextWrap stay inside [0, n) for n 1..300");

    // column order: Mixer, Insert 1, Insert 2, parameters, Back
    check(kMixerCol == 0 && colOfInsert(0) == 1 && colOfInsert(1) == 2 && kParamCol0 == 3 && kBackCol == 7,
          "columns: Mixer 1, Insert 1 2, Insert 2 3, parameters 4..7, Back 8 (1 based)");
    check(insertOfCol(1) == 0 && insertOfCol(2) == 1 && insertOfCol(0) == -1 && insertOfCol(3) == -1 && insertOfCol(7) == -1,
          "insertOfCol: only columns 1 and 2 are plugin lists");
    check(kParamCol0 + kParamCols == kBackCol && kParamCol0 == colOfInsert(1) + 1, "parameters right after Insert 2, Back after them");

    // insert column value line: every status with the longest name (kNameLen 15) fits kLabelChars
    {
        const Shown all[] = {Shown::kNone, Shown::kLoading, Shown::kName, Shown::kBypassed,
                             Shown::kMissing, Shown::kFailed, Shown::kSafe, Shown::kOverCpu, Shown::kNoRoom};
        const char* names[] = {"", "BBD", "Phaser", "Console", "ABCDEFGHIJKLMNO"};
        bool fit = true;
        for (Shown st : all)
            for (const char* nm : names)
            {
                char out[32];
                memset(out, 'x', sizeof(out));
                slotLabel(st, nm, out);
                const size_t len = strlen(out);
                fit = fit && len <= size_t(kLabelChars) && out[kLabelChars + 1] == 'x';
                printf("      %-24s -> \"%s\" (%zu)\n", nm, out, len);
            }
        check(fit, "every insert status label is at most 9 characters and writes at most 10 bytes");
        char out[16];
        slotLabel(Shown::kSafe, "BBD", out);
        check(strcmp(out, "Safe:BBD") == 0, "safe mode with BBD reads Safe:BBD");
        slotLabel(Shown::kMissing, "Phaser", out);
        check(strcmp(out, "Miss:Phas") == 0, "missing Phaser reads Miss:Phas");
        slotLabel(Shown::kNoRoom, "Phaser", out);
        check(strcmp(out, "Full:Phas") == 0, "Phaser with no room beside the other insert reads Full:Phas");
        slotLabel(Shown::kName, "Console", out);
        check(strcmp(out, "Console") == 0, "running: the plugin name");
        slotLabel(Shown::kBypassed, "Console", out);
        check(strcmp(out, "Bypassed") == 0, "bypassed: the status word only (the list highlights the plugin)");
    }


    // ---- the Master button repeat-press cycle with the Inserts stop (plug_ui_nav.h, fMasterParams.cpp switchScreen)
    check(!insertsInCycle(0, false) && insertsInCycle(1, false) && insertsInCycle(0, true) && insertsInCycle(7, true),
          "Inserts is a stop with a plugin on the card or an assigned slot, hidden with neither");
    check(cyclePages(true) == 4 && cyclePages(false) == 3, "titles read n/4 with the Inserts stop, n/3 without");
    check(nextStop(Stop::kValues, true) == Stop::kMixer && nextStop(Stop::kMixer, true) == Stop::kGlobalMixer &&
              nextStop(Stop::kGlobalMixer, true) == Stop::kInserts && nextStop(Stop::kInserts, true) == Stop::kValues,
          "reference order with plugins: Master, Track Mixer, Global Mixer, Inserts, Master");
    check(nextStop(Stop::kGlobalMixer, false) == Stop::kValues && nextStop(Stop::kInserts, false) == Stop::kValues,
          "reference order without plugins: Global Mixer wraps to Master");
    for (int withPlugins = 1; withPlugins >= 0; withPlugins--)
    {
        MasterModel m;
        m.inCycle = withPlugins;
        const char* tag = withPlugins ? "plugins" : "no plugins";
        char what[96];
        // walk 9 taps (two full cycles and one) and compare with the reference order
        Stop ref = Stop::kValues;
        bool same = m.stop() == ref;
        int visitedInserts = 0;
        for (int i = 0; i < 9; i++)
        {
            m.tap();
            ref = nextStop(ref, withPlugins != 0);
            same = same && m.stop() == ref;
            visitedInserts += m.stop() == Stop::kInserts;
        }
        snprintf(what, sizeof what, "cycle (%s): 9 taps follow the reference order", tag);
        check(same, what);
        snprintf(what, sizeof what, "cycle (%s): Inserts visited %d times (expect %d)", tag, visitedInserts, withPlugins ? 2 : 0);
        check(visitedInserts == (withPlugins ? 2 : 0), what);
        MasterModel c;
        c.inCycle = withPlugins;
        c.tap(); c.tap(); c.tap();
        snprintf(what, sizeof what, "cycle (%s): third tap lands on %s", tag, withPlugins ? "Inserts" : "Master");
        check(c.stop() == (withPlugins ? Stop::kInserts : Stop::kValues), what);
        c.tap();
        snprintf(what, sizeof what, "cycle (%s): the tap after the last stop is Master (values, inserts closed)", tag);
        check(c.stop() == (withPlugins ? Stop::kValues : Stop::kMixer) && !c.inserts, what);
    }
    {
        MasterModel m;   // Shift + Saturation is unchanged, with or without the stop
        for (int withPlugins = 0; withPlugins <= 1; withPlugins++)
        {
            m = MasterModel();
            m.inCycle = withPlugins;
            m.shiftSaturation();
            check(m.stop() == Stop::kInserts && m.inserts, withPlugins ? "Shift+Saturation opens Inserts (plugins)" : "Shift+Saturation opens Inserts (no plugins, as before)");
            m.tap();
            check(m.stop() == Stop::kValues && !m.inserts, "a tap on Master from Shift+Saturation Inserts wraps to the first Master page");
            m.tap();
            check(m.stop() == Stop::kMixer, "and the cycle goes on with the Track Mixer");
        }
        m = MasterModel();
        m.inCycle = true;
        m.tap();
        m.shiftSaturation();
        check(m.stop() == Stop::kMixer, "Shift+Saturation on the mixer does nothing (the Saturation button is a mixer button there)");
    }
    {
        // hold peek is unchanged: from every stop the hold shows the Master volume and the release restores the snapshot of
        // the screen the press started on (returnToScreen). The Inserts page is a Master values screen to the snapshot, so a
        // peek from it returns to Master values, as it did before the Inserts stop existed
        for (int withPlugins = 0; withPlugins <= 1; withPlugins++)
        {
            MasterModel m;
            m.inCycle = withPlugins;
            m.hold();
            check(m.stop() == Stop::kValues && !m.volumeScreen, "hold on Master values: peek, release back on Master values");
            m.tap();
            m.hold();
            check(m.stop() == Stop::kMixer && !m.inserts, "hold on the Track Mixer: release returns to the Track Mixer (stock snapshot)");
        }
        // stock reference: returnToScreen restores the snapshot, so a hold started on the Global Mixer returns to it
        MasterModel g;
        g.inCycle = true;
        g.tap(); g.tap();
        g.pressMaster(); g.holdMaster();
        check(g.volumeScreen && g.stop() == Stop::kValues, "hold on the Global Mixer shows the Master volume screen");
        g.releaseMaster();
        check(g.stop() == Stop::kGlobalMixer && !g.inserts, "release returns to the Global Mixer, not to Inserts");
        // hold from the Inserts page
        MasterModel i;
        i.inCycle = true;
        i.tap(); i.tap(); i.tap();
        check(i.stop() == Stop::kInserts, "(on Inserts)");
        i.pressMaster(); i.holdMaster();
        check(i.volumeScreen && !i.inserts, "hold on Inserts shows the Master volume screen and closes Inserts");
        i.releaseMaster();
        check(i.stop() == Stop::kValues && !i.inserts, "release returns to Master values (as before the Inserts stop)");
        // a hold never opens Inserts, whatever it starts from
        bool never = true;
        for (int start = 0; start < 3; start++)
        {
            MasterModel h;
            h.inCycle = true;
            for (int k = 0; k < start; k++) h.tap();
            h.hold();
            never = never && !h.inserts;
        }
        check(never, "a hold peek never ends on Inserts");
    }
    {
        // leaving by any top-row button works like leaving any Master page
        for (int from = 0; from < 4; from++)
        {
            MasterModel m;
            m.inCycle = true;
            for (int k = 0; k < from; k++) m.tap();
            const Stop at = m.stop();
            m.leave();
            check(!m.onMaster, "left to another page");
            m.enterMaster();
            const Stop back = m.stop();
            // Master values and Inserts come back as Master values, the mixers as themselves
            const Stop want = at == Stop::kInserts ? Stop::kValues : at;
            char what[96];
            snprintf(what, sizeof what, "leave from stop %d and come back: stop %d (expect %d), inserts closed", int(at), int(back), int(want));
            check(back == want && !m.inserts, what);
            m.tap();
            check(m.stop() == nextStop(want, true), "the next tap continues the cycle from there");
        }
    }

    printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
