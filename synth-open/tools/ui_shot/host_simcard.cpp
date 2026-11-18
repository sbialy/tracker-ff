// Simulated SD card faults and the audio interrupt for ui_shot. Host only, NOT firmware.
//
// The card is the SD folder (host_platform.cpp HFileSystem, the stand-in for the MK66 FileSystemImpl over FatFs). Every call of it
// asks here first. With none of the variables below set nothing changes (the pages render byte identical).
//
//   SIM_CARD_FAULTS=rule[,rule...]   rules, a path matches when it contains SUB (the card path as the firmware asks for it):
//     open=SUB                       openFile fails (FatFs FR_NO_FILE / a card that does not answer)
//     read=SUB@BYTE                  readFile fails (-1, as SdFile::read on FR_DISK_ERR) once the file position is at or past BYTE
//     dir=SUB@N                      nesDir_readItem fails (end of the walk, as SdDir::readItem on an error) after N entries of a dir
//     hidden=SUB                     entries whose name contains SUB come with the hidden attribute (AM_HID / AM_SYS)
//     slow=MS                        every 512 B sector a read touches and every open cost MS virtual ms (a slow card)
//   SIM_AUDIO=1                      the audio interrupt: AudioStream::update_all() every 128 frames at 44,117.6 Hz of the virtual clock,
//                                    with hostp::g_inAudio set. Any card call while it is set is counted (an audio path touching
//                                    the card) and makes ui_shot exit 3. Card time (slow=) lets due blocks run inside the read, as
//                                    the interrupt preempts the main thread on the device.
//   SIM_CARD_LOG=PATH                one line per open/close/read error/slow wait: virtual ms, event, path
// The commands cardstats, audiostats and waitview of ui_shot_main.cpp print what was counted.
#include "host_platform.h"

#include "AudioStream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace hostp
{
volatile bool g_inAudio;
CardStats g_card;
AudioStats g_audio;

namespace
{
struct Rule
{
    std::string kind, sub;
    long arg = 0;
};
std::vector<Rule> s_rules;
long s_slowMs;
bool s_init, s_audioOn;
FILE* s_log;
double s_audioDue;   // virtual ms of the next audio block

void setup()
{
    if (s_init) return;
    s_init = true;
    if (const char* f = getenv("SIM_CARD_FAULTS"))
    {
        std::string all = f;
        size_t p = 0;
        while (p <= all.size())
        {
            size_t e = all.find(',', p);
            if (e == std::string::npos) e = all.size();
            const std::string r = all.substr(p, e - p);
            p = e + 1;
            const size_t eq = r.find('=');
            if (eq == std::string::npos) continue;
            Rule x;
            x.kind = r.substr(0, eq);
            std::string v = r.substr(eq + 1);
            const size_t at = v.rfind('@');
            if (at != std::string::npos)
            {
                x.arg = atol(v.c_str() + at + 1);
                v = v.substr(0, at);
            }
            x.sub = v;
            if (x.kind == "slow")
                s_slowMs = atol(v.c_str());
            else
                s_rules.push_back(x);
        }
    }
    s_audioOn = getenv("SIM_AUDIO") != nullptr;
    if (const char* l = getenv("SIM_CARD_LOG")) s_log = fopen(l, "a");
}

const Rule* match(const char* kind, const std::string& path)
{
    for (const Rule& r : s_rules)
        if (r.kind == kind && path.find(r.sub) != std::string::npos) return &r;
    return nullptr;
}

void log(const char* ev, const std::string& path)
{
    if (s_log)
    {
        fprintf(s_log, "%u %s %s\n", g_nowMs, ev, path.c_str());
        fflush(s_log);
    }
}

// virtual card time: the clock moves, the audio interrupt runs the blocks that fall due in it
void cardWait(long ms)
{
    if (ms <= 0) return;
    g_card.waitMs += uint32_t(ms);
    for (long i = 0; i < ms; i++)
    {
        g_nowMs += 1;
        audioTick();
    }
    syncSystick();
}
}   // namespace

void audioTick()
{
    setup();
    if (!s_audioOn || g_inAudio) return;
    if (s_audioDue == 0) s_audioDue = g_nowMs;
    while (double(g_nowMs) >= s_audioDue)
    {
        // lateness of the block against its due time: the main loop's 5 ms step is the sim's granularity, a card wait runs the
        // blocks as they fall due (the interrupt preempts the main thread on the device)
        const uint32_t late = uint32_t(double(g_nowMs) - s_audioDue);
        if (late > g_audio.maxGapMs) g_audio.maxGapMs = late;
        g_inAudio = true;
        AudioStream::update_all();
        g_inAudio = false;
        g_audio.blocks++;
        s_audioDue += 128.0 * 1000.0 / 44117.64706;
    }
}

void cardAccess(const char* op, const std::string& path)
{
    setup();
    g_card.calls++;
    if (g_inAudio)
    {
        g_card.audioViolations++;
        fprintf(stderr, "SIMCARD: card call %s(%s) from the audio interrupt\n", op, path.c_str());
    }
}

bool cardOpen(const std::string& path)
{
    cardAccess("open", path);
    if (match("open", path))
    {
        g_card.injected++;
        log("open-fail", path);
        return false;
    }
    g_card.opens++;
    log("open", path);
    cardWait(s_slowMs);
    return true;
}

void cardClose(const std::string& path)
{
    cardAccess("close", path);
    log("close", path);
}

long cardRead(const std::string& path, long pos, long n)
{
    cardAccess("read", path);
    if (const Rule* r = match("read", path))
        if (pos + n > r->arg)
        {
            g_card.injected++;
            log("read-fail", path);
            if (pos >= r->arg) return -1;
            return r->arg - pos;   // the bytes before the bad sector, then the error on the next call
        }
    g_card.reads++;
    g_card.bytes += uint64_t(n > 0 ? n : 0);
    if (s_slowMs && n > 0)
    {   // FatFs reads through its sector buffer: a sector costs once, the next small read in it is a memory copy
        const long first = pos / 512, last = (pos + n - 1) / 512;
        static std::string lastPath;
        static long lastSector = -1;
        long sectors = last - first + 1;
        if (lastPath == path && lastSector == first) sectors--;
        lastPath = path;
        lastSector = last;
        cardWait(s_slowMs * sectors);
    }
    return n;
}

bool cardDirItem(const std::string& dir, int index, const char* name, bool& hidden)
{
    cardAccess("dir", dir);
    if (const Rule* r = match("dir", dir))
        if (index >= r->arg)
        {
            g_card.injected++;
            log("dir-fail", dir);
            return false;
        }
    if (const Rule* r = match("hidden", name))
    {
        (void)r;
        hidden = true;
    }
    return true;
}

bool audioViolated()
{
    return g_card.audioViolations != 0;
}
}   // namespace hostp
