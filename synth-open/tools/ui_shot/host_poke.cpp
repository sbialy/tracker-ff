// Host-side actions for the crash-guard checks (ref/crash-guards-emu-results.md). Host only, NOT firmware.
//
// UISHOT_POKE=PATH: the main loop polls (hostPokePoll, called from the host timer stand-in, every 256th millis()) for the file PATH. When it exists its lines are
// run once and the file is removed, results are appended to PATH.out. Everything goes through the public API the UI itself calls:
//   lastinstr N                          mtProject.values.lastUsedInstrument = N (a synth editor key then meets a non-synth instrument)
//   browsegames                          newFileManager.browseGames()   (no UI page calls it in this build)
//   dumpgames                            the list browseGames produced
//   savestereo NAME FRAMES [SLOT]        newFileManager.saveRecordedSound(...) with two different channel buffers, a ramp each;
//                                        with SLOT the recording is imported into that instrument as the sample recorder does
//   savemono NAME FRAMES                 the same with one buffer (left == right)
//   importlabels                         the three importer label buffers byte by byte (start/end/play time)
//   importplayend                        redraw the three importer labels with the play position at the end of the sample
//   sizes                                sizeof(strProjectFile)
//   status                               file manager status and current operation
//   paramid POS VALUE                    the synth patch editor's section parameter id list at POS gets VALUE (corrupt patch metadata, a value out of range)
//   macroid POS VALUE                    the same for the section id list, which holds the macro index on a macro section
// The same lines run from a ui_shot script as "poke LINE" (ui_shot_main.cpp), at that point of the script.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "Recorder.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <variant>
#define private public // host only: the poke actions reach the editor state the UI keeps private
#include "instrumentEditor/instrumentEditor.h"
#undef private

extern "C" void hostPokeImporterLabels(std::string* out);
extern "C" void hostPokeImporterPlayEnd();

namespace
{
void out(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void out(const char* fmt, ...)
{
    const char* base = getenv("UISHOT_POKE");
    if (!base)
        return;
    std::string path = std::string(base) + ".out";
    FILE* f = fopen(path.c_str(), "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

std::vector<int16_t> g_left, g_right;
char g_name[64];
char g_dir[] = "Recorded";

void run(const char* line)
{
    char cmd[32] = {};
    char a[64] = {};
    long n = 0, slot = -1;
    int got = sscanf(line, "%31s %63s %ld %ld", cmd, a, &n, &slot);
    if (got < 1)
        return;
    if (!strcmp(cmd, "lastinstr"))
    {
        mtProject.values.lastUsedInstrument = atoi(a);
        out("lastUsedInstrument = %d", (int)mtProject.values.lastUsedInstrument);
    }
    else if (!strcmp(cmd, "browsegames"))
    {
        out("browseGames() = %d", (int)newFileManager.browseGames());
    }
    else if (!strcmp(cmd, "dumpgames"))
    {
        char** list = nullptr;
        const int cnt = newFileManager.getGamesList(&list);
        out("games %d status %d", cnt, (int)newFileManager.getStatus());
        for (int i = 0; i < cnt; i++)
            out("game[%d] len %zu '%s'", i, strlen(list[i]), list[i]);
    }
    else if (!strcmp(cmd, "savestereo") || !strcmp(cmd, "savemono"))
    {
        const bool stereo = !strcmp(cmd, "savestereo");
        const uint32_t frames = (uint32_t)(got >= 3 ? n : 4096);
        g_left.assign(frames, 0);
        g_right.assign(frames, 0);
        for (uint32_t i = 0; i < frames; i++)
        {
            g_left[i] = (int16_t)(i & 0x7fff);
            g_right[i] = (int16_t)(-(int)(i & 0x7fff));
        }
        snprintf(g_name, sizeof g_name, "%s", a);
        int16_t* r = stereo ? g_right.data() : g_left.data();
        if (got >= 4 && slot >= 0)
        {
            // what the recorder holds when a take ends: both channel buffers and the length
            Recorder::Config cfg;
            cfg.destinationBufferAddress = g_left.data();
            cfg.destinationBufferSize = (uint32_t)(frames * sizeof(int16_t) * 2);
            cfg.destinationChannels = 2;
            cfg.sourceChannels = 2;
            recorder.setConfig(cfg);
            recorder.undo({ g_left.data(), r }, frames);
        }
        const bool started =
          newFileManager.saveRecordedSound(g_dir,
                                           g_name,
                                           (got >= 4 && slot >= 0) ? std::optional<int8_t>((int8_t)slot) : std::nullopt,
                                           g_left.data(),
                                           r,
                                           frames);
        out("%s %s %u frames slot %ld started=%d", cmd, a, frames, slot, (int)started);
    }
    else if (!strcmp(cmd, "importlabels"))
    {
        std::string t;
        hostPokeImporterLabels(&t);
        out("%s", t.c_str());
    }
    else if (!strcmp(cmd, "importplayend"))
    {
        hostPokeImporterPlayEnd();
        out("importplayend done");
    }
    else if (!strcmp(cmd, "sizes"))
    {
        out("sizeof(strProjectFile)=%zu", sizeof(strProjectFile));
    }
    else if (!strcmp(cmd, "status"))
    {
        out("fm status %d operation %d (fmIdle %d, fmSaveRecordedError %d, fmSaveRecordedSoundEnd %d, fmBrowseGamesEnd %d)",
            (int)newFileManager.getStatus(),
            (int)newFileManager.getCurrentOperation(),
            (int)fmIdle,
            (int)fmSaveRecordedError,
            (int)fmSaveRecordedSoundEnd,
            (int)fmBrowseGamesEnd);
    }
    else if (!strcmp(cmd, "paramid") || !strcmp(cmd, "macroid"))
    {
        int* ids = !strcmp(cmd, "paramid") ? instrumentEditor.sectionParamsIds : instrumentEditor.sectionIds;
        const int pos = atoi(a);
        if (got < 3 || ids == nullptr || pos < 0 || pos >= 16)
            out("%s: no list or bad position", cmd);
        else
        {
            ids[pos] = (int)n;
            out("%s[%d] = %ld", cmd, pos, n);
        }
    }
    else
        out("unknown command '%s'", cmd);
}
} // namespace

extern "C" void hostPokeRun(const char* line)
{
    run(line);
}

extern "C" void hostPokePoll()
{
    static const char* path = getenv("UISHOT_POKE");
    static unsigned calls = 0;
    if (!path || (++calls & 255) != 0)
        return;
    struct stat st;
    if (stat(path, &st) != 0)
        return;
    FILE* f = fopen(path, "r");
    if (!f)
        return;
    std::vector<std::string> lines;
    char buf[256];
    while (fgets(buf, sizeof buf, f))
        lines.push_back(buf);
    fclose(f);
    remove(path);
    for (auto& l : lines)
        run(l.c_str());
}
