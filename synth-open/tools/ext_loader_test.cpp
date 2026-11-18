// Host test of the extension module loader rules (synth-open/ext_loader.h, what ext_module.cpp runs on the card): every refusal
// leaves the hooks alone and the entry uncalled, the crash marker skips one boot, the status line says why.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open -I core/include synth-open/tools/ext_loader_test.cpp -o /tmp/ext_loader_test
//   /tmp/ext_loader_test [module.plg IMAGE_CRC RUN_ADDRESS]
// With the three arguments it also loads a real module file (synth-open/plug/pack_ext.py output) as the image it was packed for
// would, and checks that one bit of image id or one byte of the file is refused.
#include "ext_loader.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

struct MemFs
{
    std::map<std::string, std::string> files;
    std::string cur;
    size_t pos = 0;
    bool isOpen = false;
    int reads = 0;
    bool exists(const char* p) { return files.count(p) != 0; }
    bool remove(const char* p) { return files.erase(p) != 0; }
    bool writeText(const char* p, const char* t, int n)
    {
        files[p] = std::string(t, size_t(n));
        return true;
    }
    bool open(const char* p)
    {
        auto it = files.find(p);
        if (it == files.end()) return false;
        cur = it->second;
        pos = 0;
        isOpen = true;
        return true;
    }
    bool read(uint8_t* dst, uint32_t n)
    {
        reads++;
        if (!isOpen || pos + n > cur.size()) return false;
        memcpy(dst, cur.data() + pos, n);
        pos += n;
        return true;
    }
    void close() { isOpen = false; }
};

constexpr uint32_t kImageCrc = 0x1234ABCDu;
constexpr uint32_t kLoadBytes = 3072, kSlotBytes = kLoadBytes + EXT_NOINIT_BYTES;

// A module file as pack_ext.py writes it: header, manifest, image (text then data), no rebase table.
std::string makeModule(uint32_t imageCrc, uint32_t run, uint32_t textBytes = 256, uint32_t dataBytes = 32, uint32_t bss = 128)
{
    std::string manifest(96, '\0');
    uint32_t ms = 96;
    memcpy(&manifest[0], &ms, 4);
    memcpy(&manifest[4], "module", 6);
    std::string image(textBytes + dataBytes, '\0');
    for (size_t i = 0; i < image.size(); i++) image[i] = char(i * 7 + 1);
    plug_file_header h;
    memset(&h, 0, sizeof(h));
    h.magic = PLUG_MAGIC;
    h.header_size = 64 + 96;
    h.abi_major = PLUG_ABI_MAJOR;
    h.abi_minor = PLUG_ABI_MINOR;
    h.kind = EXT_PLUG_KIND;
    h.cpu = PLUG_CPU_CM4F;
    h.min_host = 1;
    h.image_size = uint32_t(image.size());
    h.bss_size = bss;
    h.entry_offset = 0;
    h.rw_offset = textBytes;
    std::string payload = manifest + image;
    h.crc32 = plug::crc32(0, reinterpret_cast<const uint8_t*>(payload.data()), uint32_t(payload.size()));
    h.reserved[0] = imageCrc;
    h.reserved[1] = run;
    return std::string(reinterpret_cast<const char*>(&h), sizeof(h)) + payload;
}
// re-seal the file CRC after editing the header (a well formed file with one wrong field)
std::string reseal(std::string f)
{
    plug_file_header h;
    memcpy(&h, f.data(), sizeof(h));
    h.crc32 = plug::crc32(0, reinterpret_cast<const uint8_t*>(f.data()) + 64, uint32_t(f.size() - 64));
    memcpy(&f[0], &h, sizeof(h));
    return f;
}
template <class F> std::string withHeader(std::string f, F edit)
{
    plug_file_header h;
    memcpy(&h, f.data(), sizeof(h));
    edit(h);
    memcpy(&f[0], &h, sizeof(h));
    return f;
}

struct Run
{
    ext::Status st;
    int entered;
    std::string status;
    bool marker;
};

Run boot(MemFs& fs, std::vector<uint8_t>& slot, bool develop, bool entryOk = true, uint32_t imageCrc = kImageCrc,
         uint32_t loadBytes = kLoadBytes)
{
    int entered = 0;
    bool markerAtEntry = false;
    const ext::Slot s{slot.data(), loadBytes, uintptr_t(0x70F50000u)};
    const ext::Status st = ext::boot(fs, develop, s, [&] { return imageCrc; },
                                     [&](const plug_file_header&) {
                                         entered++;
                                         markerAtEntry = fs.exists(ext::kMarker);
                                         return entryOk;
                                     });
    if (entered) check(markerAtEntry, "  the marker is on the card while the entry runs");
    return {st, entered, fs.files.count(ext::kStatus) ? fs.files[ext::kStatus] : "", fs.exists(ext::kMarker)};
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

void synthetic()
{
    const uint32_t run = 0x70F50000u;
    std::vector<uint8_t> slot(kSlotBytes, 0xEE);
    {
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        Run r = boot(fs, slot, false);
        check(r.st == ext::Status::kOff && r.entered == 0 && fs.files.size() == 1 && fs.reads == 0,
              "no develop mode: nothing read, nothing written, no entry");
    }
    {
        MemFs fs;
        Run r = boot(fs, slot, true);
        check(r.st == ext::Status::kNone && r.entered == 0 && startsWith(r.status, "none"), "develop mode, no module file: none");
    }
    {
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        std::fill(slot.begin(), slot.end(), 0xEE);
        Run r = boot(fs, slot, true);
        const std::string& f = fs.files[ext::kPath];
        bool image = memcmp(slot.data(), f.data() + 160, 288) == 0;
        bool bss = true;
        for (int i = 288; i < 288 + 128; i++) bss = bss && slot[i] == 0;
        bool tail = true;
        for (uint32_t i = kLoadBytes; i < kSlotBytes; i++) tail = tail && slot[i] == 0xEE;
        check(r.st == ext::Status::kLoaded && r.entered == 1 && startsWith(r.status, "loaded"), "good module: loaded, entry called once");
        check(image && bss, "  text and data copied, bss cleared");
        check(tail, "  the slot tail past loadBytes (fault and trace records) is not touched");
        check(r.marker, "  the crash marker stays until the main loop ran kMarkerMs");
        check(!ext::markerExpired(fs, 1000, 1000 + ext::kMarkerMs - 1) && fs.exists(ext::kMarker), "  marker kept before 3 s");
        check(ext::markerExpired(fs, 1000, 1000 + ext::kMarkerMs) && !fs.exists(ext::kMarker), "  marker removed at 3 s");
        check(ext::markerExpired(fs, 0xFFFFF000u, 0xFFFFF000u + ext::kMarkerMs + 5), "  marker timer across the millis wrap");
    }
    {   // crash marker left by the last boot: skipped once, then loads again
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        fs.files[ext::kMarker] = "ext";
        Run r = boot(fs, slot, true);
        check(r.st == ext::Status::kCrashed && r.entered == 0 && !r.marker && startsWith(r.status, "skipped"),
              "marker found: module skipped this boot, marker removed");
        check(fs.reads == 0, "  the module file is not even read");
        check(fs.exists(ext::kCrash) && fs.files[ext::kCrash].find("fault record: none") != std::string::npos,
              "  crash.txt written, no fault record in the tail: says none");
        Run r2 = boot(fs, slot, true);
        check(r2.st == ext::Status::kLoaded && r2.entered == 1, "  next boot loads again");
    }
    {   // the boot that finds the marker reports the module's fault record (ext_abi.h EXT_FAULT_*) in /Plugins/Ext/crash.txt
        std::vector<uint8_t> big(EXT_SLOT_BYTES, 0xEE);
        const uint32_t pc = 0x37F8DAF6u, rec[6] = {EXT_FAULT_MAGIC, pc, 0x70D880B3u, 0x21000000u, 0x00000100u, ~(pc ^ EXT_FAULT_MAGIC)};
        const uint32_t at = EXT_LOAD_BYTES + 0x30C;   // 4 aligned, anywhere in the tail
        memcpy(&big[at], rec, sizeof(rec));
        const std::vector<uint8_t> tailBefore(big.begin() + EXT_LOAD_BYTES, big.end());
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, 0x70D88000u);
        fs.files[ext::kMarker] = "ext";
        const ext::Slot s{big.data(), EXT_LOAD_BYTES, uintptr_t(0x70D88000u), 0x0800u};
        const ext::Status st = ext::boot(fs, true, s, [] { return kImageCrc; }, [](const plug_file_header&) { return true; });
        const std::string& c = fs.files[ext::kCrash];
        check(st == ext::Status::kCrashed, "fault record in the tail: module skipped");
        check(c.find("fault record at 70d93b0c: pc 37f8daf6 lr 70d880b3 xpsr 21000000 cfsr 00000100") != std::string::npos,
              "  crash.txt decodes pc, lr, xpsr, cfsr and the record's address");
        check(c.find("RCM_SRS0 00 RCM_SRS1 08") != std::string::npos, "  crash.txt carries the reset reason (SRS1 SW)");
        check(c.find("70d93fe0:") != std::string::npos && c.back() == '\n', "  the whole 2 KB tail in hex, last line complete");
        check(std::equal(tailBefore.begin(), tailBefore.end(), big.begin() + EXT_LOAD_BYTES), "  the tail is only read");
        uint32_t bad[6];
        memcpy(bad, rec, sizeof(bad));
        bad[5] ^= 1u;   // a torn record (reset in the middle of the write) is not reported as one
        memcpy(&big[at], bad, sizeof(bad));
        fs.files[ext::kMarker] = "ext";
        ext::boot(fs, true, s, [] { return kImageCrc; }, [](const plug_file_header&) { return true; });
        check(fs.files[ext::kCrash].find("fault record: none") != std::string::npos, "  a record with a wrong check word: none");
    }
    {   // the entry refuses
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        Run r = boot(fs, slot, true, false);
        check(r.st == ext::Status::kEntry && r.entered == 1 && !r.marker, "entry returns non zero: refused, marker removed");
    }
    struct Bad
    {
        const char* what;
        std::string file;
        ext::Status want;
        const char* text;
    };
    const std::string good = makeModule(kImageCrc, run);
    std::string flipped = good;
    flipped[200] ^= 0x01;   // one bit in the image
    std::string manif = good;
    manif[100] ^= 0x40;     // one bit in the manifest
    const Bad bads[] = {
        {"empty file", "", ext::Status::kHeader, "refused: not an"},
        {"truncated header", good.substr(0, 40), ext::Status::kHeader, "refused: not an"},
        {"truncated image", good.substr(0, good.size() - 9), ext::Status::kRead, "refused: read"},
        {"bad magic", withHeader(good, [](plug_file_header& h) { h.magic ^= 1; }), ext::Status::kHeader, "refused: not an"},
        {"kind synth", reseal(withHeader(good, [](plug_file_header& h) { h.kind = PLUG_KIND_SYNTH; })), ext::Status::kHeader,
         "refused: not an"},
        {"ABI major 2", reseal(withHeader(good, [](plug_file_header& h) { h.abi_major = 2; })), ext::Status::kHeader, "refused: not an"},
        {"image size bound", withHeader(good, [](plug_file_header& h) { h.image_size = 0x7FFFFFF0u; }), ext::Status::kHeader,
         "refused: not an"},
        {"one bit in the image (CRC)", flipped, ext::Status::kCrc, "refused: damaged"},
        {"one bit in the manifest (CRC)", manif, ext::Status::kCrc, "refused: damaged"},
        {"rebase table", reseal(withHeader(good, [](plug_file_header& h) { h.rebase_count = 1; })), ext::Status::kLayout,
         "refused: does not fit"},
        {"too big for the load part", makeModule(kImageCrc, run, 2048, 512, 1024), ext::Status::kLayout, "refused: does not fit"},
        {"other run address", makeModule(kImageCrc, run + 0x100), ext::Status::kOtherBase, "refused: linked for another"},
        {"other image", makeModule(kImageCrc ^ 0x80u, run), ext::Status::kOtherImage, "refused: built for another"},
    };
    for (const Bad& b : bads)
    {
        MemFs fs;
        fs.files[ext::kPath] = b.file;
        std::fill(slot.begin(), slot.end(), 0xEE);
        Run r = boot(fs, slot, true);
        bool tail = true;
        for (uint32_t i = kLoadBytes; i < kSlotBytes; i++) tail = tail && slot[i] == 0xEE;
        char what[160];
        snprintf(what, sizeof(what), "%s: %s, no entry, no marker, tail kept (status \"%.*s\")", b.what, ext::statusText(b.want),
                 int(r.status.size() ? r.status.size() - 1 : 0), r.status.c_str());
        check(r.st == b.want && r.entered == 0 && !r.marker && tail && startsWith(r.status, b.text), what);
    }
    {   // the image id is only computed for a file that passed every cheap check
        MemFs fs;
        fs.files[ext::kPath] = flipped;
        int crcCalls = 0;
        const ext::Slot s{slot.data(), kLoadBytes, uintptr_t(run)};
        ext::boot(fs, true, s, [&] { crcCalls++; return kImageCrc; }, [](const plug_file_header&) { return true; });
        check(crcCalls == 0, "a damaged file never pays for the image CRC");
    }
}

// MOD/IT (modit.plg): the same file rules from a second path, one use at a time of the one slot (ext_loader.h moditLoad)
struct ModitRun
{
    ext::ModitState st;
    int entered;
    int reads;
    bool statusFile, markerFile;
};
ModitRun modit(MemFs& fs, std::vector<uint8_t>& slot, ext::Status console, bool consolePending, bool entryOk = true, uint32_t imageCrc = kImageCrc,
               uintptr_t run = 0x70F50000u)
{
    int entered = 0;
    const ext::Slot s{slot.data(), kLoadBytes, run};
    const ext::ModitState st = ext::moditLoad(fs, s, console, consolePending, [&] { return imageCrc; },
                                              [&](const plug_file_header&) { return ++entered, entryOk; });
    return {st, entered, fs.reads, fs.exists(ext::kStatus), fs.exists(ext::kMarker)};
}

void moditCases()
{
    const uint32_t run = 0x70F50000u;
    std::vector<uint8_t> slot(kSlotBytes, 0xEE);
    auto tailIntact = [&] {
        for (uint32_t i = kLoadBytes; i < kSlotBytes; i++)
            if (slot[i] != 0xEE) return false;
        return true;
    };
    {
        MemFs fs;
        ModitRun r = modit(fs, slot, ext::Status::kOff, false);
        check(r.st == ext::ModitState::kMissing && r.entered == 0, "modit: no file: missing, the entry is not called");
    }
    {   // the console's file is not the MOD/IT file
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        ModitRun r = modit(fs, slot, ext::Status::kOff, false);
        check(r.st == ext::ModitState::kMissing && r.reads == 0, "modit: only module.plg on the card: missing, the console file is not read");
    }
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run);
        std::fill(slot.begin(), slot.end(), 0xEE);
        ModitRun r = modit(fs, slot, ext::Status::kOff, false);
        const std::string& f = fs.files[ext::kModitPath];
        bool image = memcmp(slot.data(), f.data() + 160, 288) == 0;
        bool bss = true;
        for (int i = 288; i < 288 + 128; i++) bss = bss && slot[i] == 0;
        check(r.st == ext::ModitState::kReady && r.entered == 1, "modit: good file: ready, entry called once");
        check(image && bss, "  text and data copied, bss cleared");
        check(tailIntact(), "  the slot tail (the recovery console's reply area) is not touched");
        check(!r.statusFile && !r.markerFile, "  no status file, no crash marker (loaded on use, never at boot)");
        check(!fs.isOpen, "  the card file is closed");
    }
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc ^ 1u, run);
        ModitRun r = modit(fs, slot, ext::Status::kOff, false);
        check(r.st == ext::ModitState::kOtherImage && r.entered == 0, "modit: built for another image: refused, entry not called");
    }
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run + 16);
        check(modit(fs, slot, ext::Status::kOff, false).st == ext::ModitState::kDamaged, "modit: linked for another slot address: damaged");
    }
    {
        MemFs fs;
        std::string f = makeModule(kImageCrc, run);
        f[f.size() / 2] ^= 0x10;
        fs.files[ext::kModitPath] = f;
        ModitRun r = modit(fs, slot, ext::Status::kOff, false);
        check(r.st == ext::ModitState::kDamaged && r.entered == 0, "modit: one bit flipped mid file: damaged, entry not called");
    }
    {
        MemFs fs;
        std::string f = makeModule(kImageCrc, run);
        fs.files[ext::kModitPath] = f.substr(0, f.size() - 100);
        check(modit(fs, slot, ext::Status::kOff, false).st == ext::ModitState::kDamaged, "modit: a file cut short: damaged");
    }
    {
        MemFs fs;
        fs.files[ext::kModitPath] = std::string(300, 'x');
        check(modit(fs, slot, ext::Status::kOff, false).st == ext::ModitState::kDamaged, "modit: not a module: damaged");
    }
    {   // text, data and bss must stay inside the load part: the tail is the recovery console's reply area
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run, 256, 32, kLoadBytes);
        check(modit(fs, slot, ext::Status::kOff, false).st == ext::ModitState::kDamaged, "modit: bss past the load part: damaged");
    }
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run);
        ModitRun r = modit(fs, slot, ext::Status::kOff, false, false);
        check(r.st == ext::ModitState::kDamaged && r.entered == 1, "modit: the entry refuses: damaged");
    }
    // the console module owns the slot: nothing is read, nothing is written
    for (int pending = 0; pending < 2; pending++)
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run);
        std::fill(slot.begin(), slot.end(), 0xEE);
        ModitRun r = modit(fs, slot, pending ? ext::Status::kOff : ext::Status::kLoaded, pending != 0);
        bool untouched = std::all_of(slot.begin(), slot.end(), [](uint8_t b) { return b == 0xEE; });
        check(r.st == ext::ModitState::kBusy && r.entered == 0 && r.reads == 0 && untouched,
              pending ? "modit: the console module is about to load (key window): busy, file not read, slot untouched"
                      : "modit: the console module is loaded: busy, file not read, slot untouched");
    }
    // every other state of the console leaves the slot free: a refused or crashed module, none, Shift held (kOff, not pending)
    for (ext::Status c : {ext::Status::kOff, ext::Status::kNone, ext::Status::kOtherImage, ext::Status::kCrashed, ext::Status::kEntry, ext::Status::kCrc,
                          ext::Status::kHeader})
    {
        MemFs fs;
        fs.files[ext::kModitPath] = makeModule(kImageCrc, run);
        std::fill(slot.begin(), slot.end(), 0xEE);
        ModitRun r = modit(fs, slot, c, false);
        check(r.st == ext::ModitState::kReady && r.entered == 1, "modit: console state leaves the slot free: loads");
    }
    check(ext::slotBusy(ext::Status::kLoaded, false) && ext::slotBusy(ext::Status::kOff, true) && !ext::slotBusy(ext::Status::kOff, false),
          "slotBusy: loaded or pending only");
    // the recovery console's reply (ext_recovery.h) moves to the slot tail while modit.plg holds the load part
    check(ext::recoveryReplyOffset(false) == 0 && ext::recoveryReplyOffset(true) == EXT_LOAD_BYTES, "recovery reply: slot start, or the tail while modit is loaded");
    check(ext::recoveryReplyOffset(true) + ext::kRecoveryReplyBytes <= EXT_SLOT_BYTES && ext::kRecoveryReplyBytes == 320,
          "  the reply (320 B) fits the slot tail");
    // the popup lines: one for each refusal, different, short enough for the popup
    {
        const ext::ModitState all[] = {ext::ModitState::kBusy, ext::ModitState::kMissing, ext::ModitState::kOtherImage, ext::ModitState::kDamaged};
        bool ok = true;
        for (int i = 0; i < 4; i++)
        {
            ok = ok && strlen(ext::moditLine1(all[i])) > 0 && strlen(ext::moditLine1(all[i])) <= 36 && strlen(ext::moditLine2(all[i])) > 0 &&
                 strlen(ext::moditLine2(all[i])) <= 36;
            for (int j = i + 1; j < 4; j++) ok = ok && strcmp(ext::moditLine1(all[i]), ext::moditLine1(all[j])) != 0;
        }
        check(ok, "popup lines: four refusals, four different first lines, both lines 1 to 36 characters");
        check(strlen(ext::moditLine1(ext::ModitState::kReady)) == 0, "  nothing to say when ready");
    }
}

void realModule(const char* path, uint32_t imageCrc, uint32_t run)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        check(false, "real module: open");
        return;
    }
    std::string bytes;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
    fclose(f);
    std::vector<uint8_t> slot(EXT_SLOT_BYTES, 0xEE);
    auto one = [&](const std::string& file, uint32_t crc, uint32_t base) {
        MemFs fs;
        fs.files[ext::kPath] = file;
        int entered = 0;
        const ext::Slot s{slot.data(), EXT_LOAD_BYTES, uintptr_t(base)};
        const ext::Status st = ext::boot(fs, true, s, [&] { return crc; }, [&](const plug_file_header&) { return ++entered, true; });
        return std::make_pair(st, entered);
    };
    plug_file_header h;
    memcpy(&h, bytes.data(), sizeof(h));
    printf("real module %s: %zu B, image %u B, bss %u B, entry +0x%x, image id %08x, run %08x\n", path, bytes.size(), h.image_size,
           h.bss_size, h.entry_offset, h.reserved[0], h.reserved[1]);
    auto ok = one(bytes, imageCrc, run);
    check(ok.first == ext::Status::kLoaded && ok.second == 1, "real module: loaded by the image it was packed for");
    check(h.image_size + h.bss_size <= EXT_LOAD_BYTES, "real module: text, data and bss inside the load part of the slot");
    check(one(bytes, imageCrc ^ 1u, run).first == ext::Status::kOtherImage, "real module: refused by another image");
    check(one(bytes, imageCrc, run + 16).first == ext::Status::kOtherBase, "real module: refused at another slot address");
    std::string bad = bytes;
    bad[bad.size() / 2] ^= 0x10;
    check(one(bad, imageCrc, run).first == ext::Status::kCrc, "real module: one bit flipped mid file is refused");
    {   // the same file as /Plugins/Ext/modit.plg (when it is the MOD/IT module): loaded on use, by the image and the address it was packed for
        auto use = [&](uint32_t crc, uint32_t base, ext::Status console, bool pending) {
            MemFs fs;
            fs.files[ext::kModitPath] = bytes;
            int entered = 0;
            const ext::Slot s{slot.data(), EXT_LOAD_BYTES, uintptr_t(base)};
            const ext::ModitState st = ext::moditLoad(fs, s, console, pending, [&] { return crc; }, [&](const plug_file_header&) { return ++entered, true; });
            return std::make_pair(st, entered);
        };
        auto good = use(imageCrc, run, ext::Status::kOff, false);
        check(good.first == ext::ModitState::kReady && good.second == 1, "real module as modit.plg: ready on use");
        check(use(imageCrc ^ 1u, run, ext::Status::kOff, false).first == ext::ModitState::kOtherImage, "  another image: refused with the other-firmware popup");
        check(use(imageCrc, run + 16, ext::Status::kOff, false).first == ext::ModitState::kDamaged, "  another slot address: damaged");
        check(use(imageCrc, run, ext::Status::kLoaded, false).first == ext::ModitState::kBusy, "  console module loaded: busy");
    }
}
}   // namespace

int main(int argc, char** argv)
{
    synthetic();
    moditCases();
    if (argc == 4) realModule(argv[1], uint32_t(strtoul(argv[2], nullptr, 0)), uint32_t(strtoul(argv[3], nullptr, 0)));
    printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
