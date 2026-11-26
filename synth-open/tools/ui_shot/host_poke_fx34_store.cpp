// ui_shot script commands of the FX3 / FX4 storage tests (fx34_store_sim.sh, fx34_compat_sim.sh). Host only, NOT firmware.
//   fx34popup [clear]          the text lines of the last info popup (showInfoPopup stays up until hidden), "fx34popup 'a' 'b'" or "fx34popup none"
//   fx34undo store|undo|redo   the pattern undo of the file manager (storePatternUndoRevision, undoPattern, redoPattern)
//   fx34state                  "fx34state on N parked P refused R": the switch, the entries of the pattern on screen, the sidecars parked
//                              by the loads since the project load, a refusal pending (reads and clears it)
//   fx34fill N                 puts N extra FX into the pattern on screen (track by track, step by step, alternating FX3 and FX4)
//                              and prints "fx34fill stored K full F"
//   fx34setv T S V             the volume byte of a step (-1 = none, 0..127: the V of the Mic/Vol view)
//   fx34load N                 the pattern file N of the Workspace into the edit buffer, as a pattern change does
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#define private public   // host only: the info popup keeps its text private
#include "core/interfacePopups.h"
#undef private

#include "fileManager.h"
#include "fx34_io.h"
#include "mtSequencer.h"

extern Sequencer sequencer;

bool hostFx34StoreCommand(const std::string& line, bool* ok)
{
    std::istringstream is(line);
    std::string c, a;
    is >> c;
    *ok = true;
    if (c == "fx34undo")
    {
        is >> a;
        if (a == "store") newFileManager.storePatternUndoRevision();
        else if (a == "undo") newFileManager.undoPattern();
        else if (a == "redo") newFileManager.redoPattern();
        else *ok = false;
        return true;
    }
    if (c == "fx34popup")
    {
        is >> a;
        if (a == "clear")
        {
            mtPopups.popupData.textLinesCount = 0;
            return true;
        }
        printf("fx34popup");
        if (mtPopups.popupData.multiLineText && mtPopups.popupData.textLinesCount)
            for (int i = 0; i < mtPopups.popupData.textLinesCount; i++)
                printf(" '%s'", mtPopups.popupData.multiLineText[i] ? mtPopups.popupData.multiLineText[i] : "");
        else
            printf(" none");
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "steppopup")
    {   // the step popup (Note, Instrument or FX list) a held step button opens: 0 = none
        printf("steppopup %d\n", int(mtPopups.getStepPopupState()));
        fflush(stdout);
        return true;
    }
    if (c == "fx34state")
    {
        const fx34::Set* s = fx34::setOf(sequencer.getPatternToUI());
        printf("fx34state on %d n %d parked %u refused %d\n", fx34::enabled() ? 1 : 0, int(fx34::count(s)), fx34io::parkedCount(),
               fx34::takeRefusal() ? 1 : 0);
        fflush(stdout);
        return true;
    }
    if (c == "fx34fill")
    {
        int n = 0, stored = 0, full = 0;
        is >> n;
        fx34::Set* s = fx34::setOf(sequencer.getPatternToUI());
        for (int i = 0; i < n; i++)
        {
            const int step = (i / 2) % 128, track = (i / 256) % 16, rank = 2 + (i & 1);
            if (fx34::put(s, uint8_t(track), uint8_t(step), uint8_t(rank), uint8_t(1 + i % 50), uint8_t(i & 127)) == fx34::kFull) full++;
            else stored++;
        }
        printf("fx34fill stored %d full %d\n", stored, full);
        fflush(stdout);
        return true;
    }
    if (c == "fx34setv")
    {
        int t = 0, s = 0, v = -1;
        is >> t >> s >> v;
        sequencer.getPatternToUI()->track[t].step[s].volume = int8_t(v);
        return true;
    }
    if (c == "fx34load")
    {
        int n = 1;
        is >> n;
        const bool r = newFileManager.loadWorkspacePatternNow(uint8_t(n));
        printf("fx34load %d %s\n", n, r ? "ok" : "failed");
        fflush(stdout);
        return true;
    }
    return false;
}
