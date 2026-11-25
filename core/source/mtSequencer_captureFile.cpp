// Capture take file, the writer and the reader (ref/capture-move-redesign.md 4.4, mtCapture.h namespace capfile). In the release image this file
// is the second Capture tenant of /Plugins/Ext/extras.plg: it has a sub-slot of its own (ext_abi.h EXT_SUB_TAKEFILE,
// synth-open/modit/extras.ld, output section .takefile) and the take logic (mtSequencer_captureMod.cpp, the Capture tenant, the last one) calls it
// directly, so the image has no more than the forwarder of capture::flushNow. Elsewhere (the ui_shot host) it is linked in.
//
// /Capture/take.cap (hidden directory) holds the take being played: a 512 B header sector, then 32,768 records of 16 B (CaptureEvent with the
// take id and the CRC-8 the writer fills, the seq field is the event number inside the take mod 65,536). Record n of a take sits in slot
// n % 32,768, so a take longer than the file keeps its newest 32,768 events, and a new take needs no erase: the records of an older take carry
// another take id. The size never changes after the file is made, so an append never touches the FAT chain or the directory entry.
//
// The writer runs from the 60 ms tick (Sequencer::captureOp TICK), main loop only. A flush opens the file, writes at most 4 sectors and closes it:
// no handle is kept over a USB drive mode or a remount. It waits (the ring absorbs about 57 s) while the file manager works, a sample stream
// reads the card (isAsyncReadBusy), the card is lent to the computer or missing, or a skip-back save runs.
#include "Platform.h"
#include "core/interface.h"
#include "fileManager.h"
#include "log.h"
#include "mtCapture.h"
#include "mtStructs.h"
#ifdef FEATURE_SKIPBACK
#include "skipback.h"
#endif

#include <string.h>

namespace capfile
{
namespace
{
// Every handle here comes from openFile without a handle of the caller, so closeFile must give the slot of the SdFile pool back (the default
// deleteOnClose). closeFile(h, false) keeps it: 24 flushes later (FF_FS_LOCK) no file of the card opens until the next boot.
constexpr uint8_t kQueue = 4;          // take starts waiting for the writer (two within one busy spell are rare: a take needs a 30 s gap)
constexpr uint32_t kRetryTicks = 1000; // after a failed create: about 60 s of 60 ms ticks
constexpr uint32_t kFailTicks = 17;    // after a failed write: about 1 s
constexpr uint32_t kNowSectors = 48;   // flushNow: the whole ring is 1,016 events = 32 sectors, plus the headers

struct Seg
{
    uint32_t start;    // the ring event number the take starts at
    uint32_t tUs;
    uint32_t bpm100;
    uint32_t nameHash;
};

// The sector 0 of the file (32 bytes used)
struct Header
{
    uint32_t magic;
    uint16_t version;
    uint8_t take;
    uint8_t flags;     // bit 0: events were lost
    uint32_t slots;
    uint32_t startUs;
    uint32_t bpm100;
    uint32_t nameHash;
    uint32_t lost;
    uint8_t reserved[3];
    uint8_t crc;       // CRC-8 of bytes 0..30
};
static_assert(sizeof(Header) == 32, "the take file header");

struct State
{
    Seg q[kQueue];
    Seg cur;             // the take being written
    uint8_t qn;          // takes waiting in q
    uint8_t id;          // the file take id of cur
    uint8_t lastId;      // the last id handed out, continued from the header of the file on disk (so a new boot never reuses the one of the file)
    bool haveTake;
    bool idSet;
    bool headerDue;
    bool lostDirty;      // the header must say more lost events
    bool force;          // a take ended: write what is pending without waiting for 32 events or 2 s
    bool ready;          // the file has its full size
    bool tailValid;      // s_sec holds the sector tailSector as the file has it
    uint8_t state;
    uint32_t tailSector;
    uint32_t wr;         // the next ring event to write
    uint32_t cons;       // the take logic's cursor at the last tick
    uint32_t lost;       // events of cur lost
    uint32_t ticks;
    uint32_t retryAt;
    uint32_t stats[kStatCount];
};
State s;
alignas(4) uint8_t s_sec[kSectorBytes]; // the sector being filled (kept between flushes while it is partial)
alignas(4) uint8_t s_tmp[kSectorBytes]; // the header, zeros for the creation, the scan of the reader

uint32_t hashName()
{
    const char* n = newFileManager.getProjectNamefromProjectFile();
    uint32_t h = 2166136261u;
    for (; n && *n; n++)
    {
        h = (h ^ uint8_t(*n)) * 16777619u;
    }
    return h;
}

FileSystem* card()
{
    return newFileManager.getFileSystem();
}

// The card is free for the writer. A card that is lent or gone also drops the "file is whole" knowledge: it is checked again afterwards
bool busy(FileSystem* fs)
{
    if (!fs || !fs->isSdCardInserted() || mtInterface.isFileTransferMode())
    {
        s.ready = false;
        s.tailValid = false;
        return true;
    }
    if (fs->isAsyncReadBusy() || newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation)
    {
        return true;
    }
#ifdef FEATURE_SKIPBACK
    if (skipback::saving())
    {
        return true;
    }
#endif
    return false;
}

bool headerValid(const Header& h)
{
    return h.magic == kHeaderMagic && h.version == kHeaderVersion && h.slots == kSlots && h.take != 0 &&
           h.crc == crc8(&h, sizeof(Header) - 1);
}

void failed(uint32_t afterTicks)
{
    s.stats[kStatErrors]++;
    s.ready = false;
    s.tailValid = false;
    s.retryAt = s.ticks + afterTicks;
}

// The file is whole, or a step of its creation was done (8 zero sectors, the file only ever grows by these, so a power cut leaves a file
// of a whole number of sectors that the next call continues). True: the file is whole and a flush may follow now.
bool ensureFile(FileSystem* fs)
{
    if (s.ready)
    {
        return true;
    }
    if (s.retryAt && s.ticks < s.retryAt)
    {
        return false;
    }
    s.retryAt = 0;
    size_t size = 0;
    const bool there = fs->exists(kPath);
    if (there)
    {
        size = fs->getFileSize(kPath);
        if (size == kFileBytes)
        {
            // whole: the take id counter continues from the header on disk
            auto [ok, h] = fs->openFile(kPath, FileSystem::kRead);
            if (ok)
            {
                Header hd;
                if (fs->readFile(&hd, sizeof(hd), h) == int32_t(sizeof(hd)) && headerValid(hd))
                {
                    s.lastId = hd.take;
                }
                fs->closeFile(h);
                s.ready = true;
                s.state = 2;
                return true;
            }
            failed(kFailTicks);
            return false;
        }
        if (size > kFileBytes || size % kSectorBytes != 0)
        {
            fs->remove(kPath); // not ours, or torn: start again
            size = 0;
        }
    }
    else if (!fs->exists(kDir) && !fs->mkdir(1, kDir))
    {
        s.state = 3;
        LOGE("capture: no /Capture directory");
        failed(kRetryTicks);
        return false;
    }
    auto [ok, h] = fs->openFile(kPath, size == 0 ? FileSystem::kWriteCreateNew : FileSystem::kWriteAppend);
    if (!ok)
    {
        s.state = 3;
        LOGE("capture: take file not created");
        failed(kRetryTicks);
        return false;
    }
    memset(s_tmp, 0, sizeof(s_tmp));
    bool wrote = true;
    for (uint32_t i = 0; i < kCreateSectors && size + i * kSectorBytes < kFileBytes && wrote; i++)
    {
        wrote = fs->writeFile(s_tmp, kSectorBytes, h) == int32_t(kSectorBytes);
    }
    fs->closeFile(h);
    s.stats[kStatCreated]++;
    s.state = 1;
    if (!wrote)
    {
        s.state = 3;
        LOGE("capture: take file write failed (card full?)");
        failed(kRetryTicks);
    }
    return false;
}

void promote()
{
    s.cur = s.q[0];
    for (uint8_t i = 1; i < s.qn; i++)
    {
        s.q[i - 1] = s.q[i];
    }
    s.qn--;
    s.haveTake = true;
    s.idSet = false;
    s.headerDue = true;
    s.lostDirty = false;
    s.tailValid = false;
    s.lost = 0;
    if (s.wr < s.cur.start)
    {
        s.wr = s.cur.start;
    }
    else if (s.wr > s.cur.start)
    {
        s.lost = s.wr - s.cur.start; // the first events of the take left the ring before the writer saw them
        s.lostDirty = true;
        s.stats[kStatLost] += s.lost;
    }
}

uint8_t nextId()
{
    return uint8_t(s.lastId >= 255 ? 1 : s.lastId + 1);
}

bool writeHeader(FileSystem* fs, FileSystem::FileHandle h)
{
    Header hd;
    memset(&hd, 0, sizeof(hd));
    hd.magic = kHeaderMagic;
    hd.version = kHeaderVersion;
    hd.take = s.id;
    hd.flags = s.lost ? 1 : 0;
    hd.slots = kSlots;
    hd.startUs = s.cur.tUs;
    hd.bpm100 = s.cur.bpm100;
    hd.nameHash = s.cur.nameHash;
    hd.lost = s.lost;
    hd.crc = crc8(&hd, sizeof(hd) - 1);
    memset(s_tmp, 0, sizeof(s_tmp));
    memcpy(s_tmp, &hd, sizeof(hd));
    if (!fs->seekFile(0, h) || fs->writeFile(s_tmp, kSectorBytes, h) != int32_t(kSectorBytes))
    {
        return false;
    }
    s.stats[kStatHeaders]++;
    return true;
}

// Write up to `budget` sectors (header sectors count). The state moves only after a sector is on the card.
// True: the card was free and nothing failed.
bool flush(uint32_t budget)
{
    FileSystem* fs = card();
    if (busy(fs))
    {
        s.stats[kStatSkips]++;
        return false;
    }
    if (!ensureFile(fs))
    {
        return false;
    }
    auto [ok, h] = fs->openFile(kPath, FileSystem::kWriteOpen);
    if (!ok)
    {
        failed(kFailTicks);
        return false;
    }
    bool good = true;
    uint32_t wrote = 0;
    while (budget > 0 && good)
    {
        if (!s.haveTake)
        {
            if (!s.qn)
            {
                break;
            }
            promote();
        }
        const uint32_t lim = s.qn ? s.q[0].start : s.cons;
        if (s.wr >= lim)
        {
            if (s.qn)
            {
                s.haveTake = false; // this take is written out, the next one is waiting
                continue;
            }
            break;
        }
        if (!s.idSet)
        {
            s.id = nextId();
            s.lastId = s.id;
            s.idSet = true;
        }
        if (s.headerDue || s.lostDirty)
        {
            good = writeHeader(fs, h);
            if (good)
            {
                s.headerDue = false;
                s.lostDirty = false;
                budget--;
            }
            continue;
        }
        const uint32_t rel = s.wr - s.cur.start;
        const uint32_t slot = rel & (kSlots - 1u);
        const uint32_t sec = slot / kRecordsPerSector;
        const uint32_t off = (1u + sec) * kSectorBytes;
        if (!s.tailValid || s.tailSector != sec)
        {
            // a sector that this take already wrote in (a lap, or a start in the middle after lost events) keeps its other records
            memset(s_sec, 0, sizeof(s_sec));
            if (rel >= kSlots || slot % kRecordsPerSector != 0)
            {
                if (fs->seekFile(off, h))
                {
                    fs->readFile(s_sec, kSectorBytes, h);
                }
            }
            s.tailSector = sec;
            s.tailValid = true;
        }
        uint32_t n = 0;
        for (uint32_t k = slot % kRecordsPerSector, w = s.wr; k < kRecordsPerSector && w < lim; k++, w++, n++)
        {
            CaptureEvent r = captureRingAt(w);
            r.take = s.id;
            r.seq = uint16_t(w - s.cur.start);
            r.crc = crc8(&r, 15);
            memcpy(s_sec + k * sizeof(CaptureEvent), &r, sizeof(CaptureEvent));
        }
        if (!fs->seekFile(off, h) || fs->writeFile(s_sec, kSectorBytes, h) != int32_t(kSectorBytes))
        {
            good = false;
            break;
        }
        s.wr += n;
        s.stats[kStatWritten] += n;
        s.stats[kStatSectors]++;
        if ((slot % kRecordsPerSector) + n >= kRecordsPerSector)
        {
            s.tailValid = false; // the sector is full
        }
        wrote++;
        budget--;
    }
    fs->closeFile(h);
    if (!good)
    {
        failed(kFailTicks);
        return false;
    }
    if (wrote)
    {
        s.stats[kStatFlushes]++;
    }
    return true;
}

// the events of the current take not written yet, and the end of them (the next take's start, or the take logic's cursor)
uint32_t pendingEnd()
{
    return s.qn ? s.q[0].start : s.cons;
}
} // namespace

uint8_t crc8(const void* data, uint32_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint8_t c = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int b = 0; b < 8; b++)
        {
            c = (c & 0x80) ? uint8_t((c << 1) ^ 0x07) : uint8_t(c << 1);
        }
    }
    return c;
}

void takeStart(uint32_t index, uint32_t tUs)
{
    if (s.qn == kQueue)
    {
        // the writer is far behind (the card was busy for more than kQueue takes): the oldest waiting take is given up
        s.stats[kStatLost] += 1;
        for (uint8_t i = 1; i < s.qn; i++)
        {
            s.q[i - 1] = s.q[i];
        }
        s.qn--;
    }
    Seg& g = s.q[s.qn++];
    g.start = index;
    g.tUs = tUs;
    g.bpm100 = uint32_t(mtProject.values.globalTempo * 100.0f + 0.5f);
    g.nameHash = hashName();
}

void takeEnd()
{
    s.force = true;
}

void tick(uint32_t cons, uint32_t now)
{
    s.ticks++;
    s.cons = cons;
    if (!s.haveTake)
    {
        if (!s.qn)
        {
            s.wr = cons; // no take: nothing to write (a note off before the first note on is not part of a take)
            s.force = false;
            return;
        }
        promote();
    }
    // the ring was lapped while the card was busy: the oldest events are gone, the take file shows the hole
    if (cons - s.wr > kCaptureReadMax)
    {
        const uint32_t oldestKept = cons - kCaptureReadMax;
        const uint32_t gone = oldestKept - s.wr;
        s.wr = oldestKept;
        s.stats[kStatLost] += gone;
        s.lost += gone;
        s.lostDirty = true;
    }
    const uint32_t lim = pendingEnd();
    const uint32_t pending = lim > s.wr ? lim - s.wr : 0;
    // a take waiting behind this one means this one is complete: flush it without waiting for 32 events or 2 s
    bool want = pending >= kFlushEvents || s.qn > 0 || (s.force && pending > 0);
    if (!want && pending > 0 && now - captureRingAt(lim - 1u).tUs >= kFlushIdleUs)
    {
        want = true;
    }
    if (!want)
    {
        if (pending == 0)
        {
            s.force = false;
        }
        return;
    }
    if (flush(kFlushSectors))
    {
        s.force = false;
    }
}

bool flushNow(uint32_t cons)
{
    s.cons = cons;
    if (!s.haveTake && !s.qn)
    {
        return true;
    }
    s.force = true;
    // the card is not lent yet when this runs, and the file manager was asked to finish (flushWorkspaceSave): the same checks as a tick
    if (!flush(kNowSectors))
    {
        return false;
    }
    const bool done = (s.haveTake ? pendingEnd() <= s.wr : true) && !s.qn;
    if (done)
    {
        s.force = false;
    }
    return done;
}

uint32_t stat(uint32_t which)
{
    switch (which)
    {
    case kStatState: return s.state;
    case kStatPending: return s.haveTake && pendingEnd() > s.wr ? pendingEnd() - s.wr : 0;
    case kStatTake: return s.haveTake && s.idSet ? s.id : 0;
    default: return which < kStatCount ? s.stats[which] : 0;
    }
}

// The reader ---------------------------------------------------------------------------------------------------------------------------

bool readOpen(TakeInfo* info)
{
    memset(info, 0, sizeof(*info));
    FileSystem* fs = card();
    if (!fs || !fs->exists(kPath))
    {
        return false;
    }
    auto [ok, h] = fs->openFile(kPath, FileSystem::kRead);
    if (!ok)
    {
        return false;
    }
    memset(s_tmp, 0, sizeof(s_tmp));
    fs->readFile(s_tmp, kSectorBytes, h);
    Header hd;
    memcpy(&hd, s_tmp, sizeof(hd));
    if (!headerValid(hd))
    {
        fs->closeFile(h);
        return false;
    }
    info->take = hd.take;
    info->lostFlag = hd.flags & 1;
    info->startUs = hd.startUs;
    info->bpm100 = hd.bpm100;
    info->nameHash = hd.nameHash;
    info->lost = hd.lost;
    // the extent: the newest and the oldest valid record of the take (seq compared in the circle of 65,536, every record is within 32,768 of the others).
    // A record is valid when its CRC and take id are right and its seq agrees with its slot. A short read (a file cut by a power loss) leaves zeros: no valid record.
    bool any = false;
    uint16_t newest = 0, oldest = 0;
    uint32_t valid = 0;
    for (uint32_t sec = 0; sec < kSlots / kRecordsPerSector; sec++)
    {
        memset(s_tmp, 0, sizeof(s_tmp));
        if (fs->readFile(s_tmp, kSectorBytes, h) <= 0)
        {
            break;
        }
        for (uint32_t k = 0; k < kRecordsPerSector; k++)
        {
            const CaptureEvent* r = reinterpret_cast<const CaptureEvent*>(s_tmp + k * sizeof(CaptureEvent));
            if (r->take != hd.take || r->crc != crc8(r, 15) || (r->seq & (kSlots - 1u)) != sec * kRecordsPerSector + k)
            {
                continue;
            }
            valid++;
            if (!any)
            {
                any = true;
                newest = oldest = r->seq;
                continue;
            }
            if (uint16_t(r->seq - newest) < 0x8000u)
            {
                newest = r->seq;
            }
            if (uint16_t(oldest - r->seq) < 0x8000u)
            {
                oldest = r->seq;
            }
        }
    }
    fs->closeFile(h);
    info->valid = valid;
    info->newest = newest;
    info->oldest = oldest;
    info->span = any ? uint32_t(uint16_t(newest - oldest)) + 1u : 0;
    return true;
}

uint32_t readEvents(const TakeInfo& info, uint32_t from, CaptureEvent* out, uint32_t max)
{
    if (from >= info.span)
    {
        return 0;
    }
    FileSystem* fs = card();
    auto [ok, h] = fs->openFile(kPath, FileSystem::kRead);
    if (!ok)
    {
        return 0;
    }
    const uint32_t n = info.span - from < max ? info.span - from : max;
    uint32_t loaded = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint16_t seq = uint16_t(info.oldest + from + i);
        const uint32_t slot = seq & (kSlots - 1u);
        const uint32_t sec = slot / kRecordsPerSector;
        if (sec != loaded)
        {
            memset(s_tmp, 0, sizeof(s_tmp));
            if (fs->seekFile((1u + sec) * kSectorBytes, h))
            {
                fs->readFile(s_tmp, kSectorBytes, h);
            }
            loaded = sec;
        }
        const CaptureEvent* r = reinterpret_cast<const CaptureEvent*>(s_tmp + (slot % kRecordsPerSector) * sizeof(CaptureEvent));
        if (r->take == info.take && r->crc == crc8(r, 15) && r->seq == seq)
        {
            out[i] = *r;
        }
        else
        {
            memset(&out[i], 0, sizeof(CaptureEvent)); // a hole: take == 0
        }
    }
    fs->closeFile(h);
    return n;
}

} // namespace capfile
