#include "mtTuneScale.h"
#include "mtNoteName.h"
#include "mtScala.h"
#include "mtTuning.h"

#include "Platform.h"
#include "TrackerImpl.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#if defined(__arm__) || defined(__thumb__)
#define TUNE_SCALE_SDRAM __attribute__((section(".noinit.$RAM4.tunescalebuf")))
#else
#define TUNE_SCALE_SDRAM
#endif

namespace
{
constexpr int kNameMax = tuning::kNameMax;
char s_names[(tunescale::kMaxFiles + 1) * kNameMax] TUNE_SCALE_SDRAM;   // the listing (names the card list returns)
const char* s_list[tunescale::kMaxFiles + 3] TUNE_SCALE_SDRAM;          // [0] EDO, [1..n] files, [n + 1] the copy in the project
char s_text[scala::kMaxBytes + 1] TUNE_SCALE_SDRAM;                      // one file (one byte more than the cap: a larger file is seen)
uint32_t s_deg[scala::kMaxNotes] TUNE_SCALE_SDRAM;                       // the parse result, copied to the live table only when it is good
char s_msg[48] TUNE_SCALE_SDRAM;                                        // the reason of the last refusal, for a popup that keeps the pointer
int s_files;
int s_entries = 1;
const char kEdo[] = "EDO (no file)";
const char kCopy[] = "(project copy)";

FileSystem* cardFs()
{
    TrackerImpl* t = TrackerImpl::getInstance();
    Platform* p = t ? t->getPlatform() : nullptr;
    return p ? p->getFileSystem() : nullptr;
}
bool isScl(const char* n)
{
    const size_t len = strlen(n);
    return len > 4 && strcasecmp(n + len - 4, ".scl") == 0;
}
}   // namespace

#if defined(__arm__) || defined(__thumb__)
namespace tuning
{
uint32_t degMc[kMaxN] MT_TUNE_SDRAM;   // declared in mtTuning.h
char fileName[kNameMax] MT_TUNE_SDRAM;
}   // namespace tuning
#endif

namespace tunescale
{
void scan()
{
    s_files = 0;
    s_list[0] = kEdo;
    FileSystem* fs = cardFs();
    if (fs && fs->isSdCardInserted() && fs->exists("/Tunings"))
    {
        int n = kMaxFiles;
        fs->listDirContents("/Tunings", FileSystem::DirContentType::kFiles, isScl, s_names, int(sizeof(s_names)), kNameMax, n,
                            const_cast<char**>(s_list + 1));
        s_files = n < kMaxFiles ? n : kMaxFiles;
    }
    s_entries = 1 + s_files;
    // a tuning of the project whose file is not on this card: its copy is an entry of its own, so the row shows it
    if (tuning::cfg.file && position() == 0)
        s_list[s_entries++] = tuning::fileName[0] ? tuning::fileName : kCopy;
}

const char* const* list() { return s_list; }
int entries() { return s_entries; }
int files() { return s_files; }

int position()
{
    if (!tuning::cfg.file) return 0;
    for (int i = 0; i < s_files; i++)
        if (strcmp(s_list[1 + i], tuning::fileName) == 0) return 1 + i;
    return s_entries > 1 + s_files ? s_entries - 1 : 0;
}

const char* message() { return s_msg; }

bool load(const char* name)
{
    FileSystem* fs = cardFs();
    char path[40];
    snprintf(path, sizeof(path), "/Tunings/%s", name);
    int n = -1;
    if (fs)
    {
        auto [ok, f] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
        if (ok)
        {
            n = int(fs->readFile(s_text, sizeof(s_text), f));
            fs->closeFile(f);
        }
    }
    if (n < 0)
    {
        snprintf(s_msg, sizeof(s_msg), "Cannot read the file");
        return false;
    }
    const scala::Result r = scala::parse(s_text, n, s_deg);
    if (r.err != scala::kOk)
    {
        scala::errText(r, s_msg, int(sizeof(s_msg)));
        return false;
    }
    memcpy(tuning::degMc, s_deg, size_t(r.n) * sizeof(uint32_t));
    snprintf(tuning::fileName, size_t(kNameMax), "%s", name);
    tuning::set(r.n, r.periodMc, tuning::cfg.root, tuning::cfg.hzE4);
    tuning::cfg.file = 1;
    tuningChanged();
    return true;
}

void off()
{
    tuning::cfg.file = 0;
    tuning::fileName[0] = 0;
    tuningChanged();
}
}   // namespace tunescale
