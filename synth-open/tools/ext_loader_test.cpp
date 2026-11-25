// Host test of the extension module loader rules (synth-open/ext_loader.h, what ext_module.cpp runs on the card): every refusal
// leaves the hooks alone and the entry uncalled, the crash marker skips one boot, the status line says why.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open -I core/include synth-open/tools/ext_loader_test.cpp -o /tmp/ext_loader_test
//   /tmp/ext_loader_test [module.plg IMAGE_CRC RUN_ADDRESS | extras.plg IMAGE_CRC RUN_ADDRESS rel]
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
// table: extras.plg's sub-slot table after the manifest (ext_abi.h ext_rel_table_t), empty for the console module
std::string makeModule(uint32_t imageCrc, uint32_t run, uint32_t textBytes = 256, uint32_t dataBytes = 32, uint32_t bss = 128,
                       const std::string& table = std::string())
{
    std::string manifest(96, '\0');
    uint32_t ms = 96;
    memcpy(&manifest[0], &ms, 4);
    memcpy(&manifest[4], "module", 6);
    manifest += table;
    std::string image(textBytes + dataBytes, '\0');
    for (size_t i = 0; i < image.size(); i++) image[i] = char(i * 7 + 1);
    plug_file_header h;
    memset(&h, 0, sizeof(h));
    h.magic = PLUG_MAGIC;
    h.header_size = uint16_t(64 + manifest.size());
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
// the sub-slot table of a small bundle: MOD/IT 1 KB at 0, the other tenants empty after it (pack_ext.py writes it from the linked elf)
ext_rel_table_t smallTable(uint32_t region = EXT_REL_BYTES)
{
    ext_rel_table_t t;
    memset(&t, 0, sizeof(t));
    t.magic = EXT_REL_TABLE_MAGIC;
    t.version = EXT_REL_TABLE_VERSION;
    t.count = EXT_SUB_COUNT;
    t.region_bytes = region;
    t.sub[0] = {0, EXT_SUB_ALIGN};
    for (uint32_t i = 1; i < EXT_SUB_COUNT; i++) t.sub[i] = {EXT_SUB_ALIGN, 0};
    t.used_bytes = EXT_SUB_ALIGN;
    return t;
}
std::string tableBytes(const ext_rel_table_t& t) { return std::string(reinterpret_cast<const char*>(&t), sizeof(t)); }
std::string makeExtras(uint32_t imageCrc, uint32_t run, const ext_rel_table_t& t = smallTable())
{
    return makeModule(imageCrc, run, 256, 32, 128, tableBytes(t));
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

// extras.plg (MOD/IT first): the same file rules from a second path into the release region of its own (ext_loader.h extrasLoad)
constexpr uint32_t kRelBytes = EXT_REL_BYTES;
struct ModitRun
{
    ext::ModitState st;
    int entered;
    int reads;
    bool statusFile, markerFile;
};
ext_rel_table_t g_table;   // what the last extrasLoad copied
ModitRun modit(MemFs& fs, std::vector<uint8_t>& region, bool entryOk = true, uint32_t imageCrc = kImageCrc, uintptr_t run = 0x70F50000u)
{
    int entered = 0;
    const ext::Slot s{region.data(), kRelBytes, run};
    memset(&g_table, 0, sizeof(g_table));
    const ext::ModitState st =
      ext::extrasLoad(fs, s, [&] { return imageCrc; }, [&](const plug_file_header&) { return ++entered, entryOk; }, &g_table);
    return {st, entered, fs.reads, fs.exists(ext::kStatus), fs.exists(ext::kMarker)};
}

void moditCases()
{
    const uint32_t run = 0x70F50000u;
    std::vector<uint8_t> region(kRelBytes, 0xEE);
    {
        MemFs fs;
        ModitRun r = modit(fs, region);
        check(r.st == ext::ModitState::kMissing && r.entered == 0, "extras: no file: missing, the entry is not called");
    }
    {   // the console's file is not the bundle
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, run);
        ModitRun r = modit(fs, region);
        check(r.st == ext::ModitState::kMissing && r.reads == 0, "extras: only module.plg on the card: missing, the console file is not read");
    }
    {   // a 1.4.0 modit.plg is not read any more
        MemFs fs;
        fs.files["/Plugins/Ext/modit.plg"] = makeModule(kImageCrc, run);
        check(modit(fs, region).st == ext::ModitState::kMissing, "extras: only an old modit.plg: missing");
    }
    {
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run);
        std::fill(region.begin(), region.end(), 0xEE);
        ModitRun r = modit(fs, region);
        const std::string& f = fs.files[ext::kExtrasPath];
        bool image = memcmp(region.data(), f.data() + 160 + sizeof(ext_rel_table_t), 288) == 0;
        bool bss = true;
        for (int i = 288; i < 288 + 128; i++) bss = bss && region[i] == 0;
        check(r.st == ext::ModitState::kReady && r.entered == 1, "extras: good file: ready, entry called once");
        check(image && bss, "  text and data copied, bss cleared");
        check(!r.statusFile && !r.markerFile, "  no status file, no crash marker (loaded on use, never at boot)");
        check(!fs.isOpen, "  the card file is closed");
        check(memcmp(&g_table, &tableBytes(smallTable())[0], sizeof(g_table)) == 0, "  the sub-slot table is read from the file header");
    }
    {   // a round 1 extras.plg (fixed sub-slots, no table after the manifest) is refused before anything is loaded
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeModule(kImageCrc, run);
        ModitRun r = modit(fs, region);
        check(r.st == ext::ModitState::kDamaged && r.entered == 0, "extras: a round 1 file without the sub-slot table: refused (damaged)");
    }
    {   // every broken table is refused, the entry never runs
        struct Bad
        {
            const char* what;
            void (*edit)(ext_rel_table_t&);
        };
        const Bad bad[] = {
          {"table version 1", [](ext_rel_table_t& t) { t.version = 1; }},
          {"table magic", [](ext_rel_table_t& t) { t.magic ^= 1; }},
          {"table count", [](ext_rel_table_t& t) { t.count = EXT_SUB_COUNT - 1; }},
          {"packed for another region size", [](ext_rel_table_t& t) { t.region_bytes = EXT_REL_BYTES + 1024; }},
          {"a sub-slot off the 1 KB grid", [](ext_rel_table_t& t) { t.sub[1].at += 16; }},
          {"overlapping sub-slots", [](ext_rel_table_t& t) { t.sub[1].at = 0; }},
          {"past the region", [](ext_rel_table_t& t) { t.sub[4].bytes = EXT_REL_BYTES; t.used_bytes += EXT_REL_BYTES; }},
          {"used bytes not the last end", [](ext_rel_table_t& t) { t.used_bytes += 1024; }},
          {"text, data and bss past used bytes", [](ext_rel_table_t& t) { t.sub[0].bytes = 0; for (auto& u : t.sub) u.at = 0; t.used_bytes = 0; }},
        };
        bool ok = true;
        for (const Bad& b : bad)
        {
            ext_rel_table_t t = smallTable();
            b.edit(t);
            MemFs fs;
            fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run, t);
            ModitRun r = modit(fs, region);
            if (r.st != ext::ModitState::kDamaged || r.entered != 0) printf("  not refused: %s\n", b.what), ok = false;
        }
        check(ok, "extras: 9 broken sub-slot tables refused (version, magic, count, region, grid, overlap, past the region, used, file size)");
        ext_rel_table_t t = smallTable();   // a tenant with a 40 KB sub-slot of its own still fits when the region is that big
        t.sub[4] = {EXT_SUB_ALIGN, EXT_REL_BYTES - EXT_SUB_ALIGN};
        t.used_bytes = EXT_REL_BYTES;
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run, t);
        check(modit(fs, region).st == ext::ModitState::kReady, "extras: a table that fills the region exactly: ready");
    }
    {
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc ^ 1u, run);
        ModitRun r = modit(fs, region);
        check(r.st == ext::ModitState::kOtherImage && r.entered == 0, "extras: built for another image: refused, entry not called");
    }
    {
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run + 16);
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: linked for another address: damaged");
    }
    {   // a tenant packed on its own at a sub-slot (pack_ext.py --base-offset) is not the bundle: the loader runs only offset 0
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run + 16 * 1024);
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: linked at a sub-slot base (+16 KB): damaged");
    }
    {   // a module linked for the console slot is refused by the address
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, 0x70D8A000u);
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: a module linked at the console slot: damaged");
    }
    {
        MemFs fs;
        std::string f = makeExtras(kImageCrc, run);
        f[f.size() / 2] ^= 0x10;
        fs.files[ext::kExtrasPath] = f;
        ModitRun r = modit(fs, region);
        check(r.st == ext::ModitState::kDamaged && r.entered == 0, "extras: one bit flipped mid file: damaged, entry not called");
    }
    {
        MemFs fs;
        std::string f = makeExtras(kImageCrc, run);
        fs.files[ext::kExtrasPath] = f.substr(0, f.size() - 100);
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: a file cut short: damaged");
    }
    {
        MemFs fs;
        fs.files[ext::kExtrasPath] = std::string(300, 'x');
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: not a module: damaged");
    }
    {   // text, data and bss must stay inside the region
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeModule(kImageCrc, run, 256, 32, kRelBytes, tableBytes(smallTable()));
        check(modit(fs, region).st == ext::ModitState::kDamaged, "extras: bss past the region: damaged");
    }
    {
        MemFs fs;
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run);
        ModitRun r = modit(fs, region, false);
        check(r.st == ext::ModitState::kDamaged && r.entered == 1, "extras: the entry refuses: damaged");
    }
    // two tenants: the console module in its slot, extras.plg in the release region, both orders. Nothing of one is written by the other,
    // the hook table keeps both modules' pointers, the image id is computed once for both.
    for (int extrasFirst = 0; extrasFirst < 2; extrasFirst++)
    {
        const uint32_t conRun = 0x70D8A000u;
        std::vector<uint8_t> con(kSlotBytes, 0xC5);
        std::fill(region.begin(), region.end(), 0xEE);
        MemFs fs;
        fs.files[ext::kPath] = makeModule(kImageCrc, conRun, 400, 64, 64);
        fs.files[ext::kExtrasPath] = makeExtras(kImageCrc, run);
        ext_hooks_t table;
        memset(&table, 0, sizeof(table));
        int crcCalls = 0;
        bool idKnown = false;
        auto id = [&] { if (!idKnown) { crcCalls++; idKnown = true; } return kImageCrc; };
        auto conUpdate = +[]() {};
        auto conLog = +[](const char*) {};
        auto extrasModit = +[](uint32_t) -> uint32_t { return 7; };
        auto bootConsole = [&] {
            const ext::Slot s{con.data(), kLoadBytes, conRun};
            return ext::boot(fs, true, s, id, [&](const plug_file_header&) {
                ext_hooks_t hk;
                memset(&hk, 0, sizeof(hk));
                hk.update = conUpdate;
                hk.log = conLog;
                ext::mergeHooks(table, hk);
                return true;
            });
        };
        ext_rel_table_t relTable;
        auto useExtras = [&] {
            const ext::Slot s{region.data(), kRelBytes, run};
            return ext::extrasLoad(fs, s, id, [&](const plug_file_header&) {
                ext_hooks_t hk;
                memset(&hk, 0, sizeof(hk));
                hk.modit = extrasModit;
                ext::mergeHooks(table, hk);
                return table.modit != nullptr;
            }, &relTable);
        };
        ext::Status cst;
        ext::ModitState est;
        std::vector<uint8_t> conAfterBoot;
        if (extrasFirst)
        {
            est = useExtras();
            cst = bootConsole();
        }
        else
        {
            cst = bootConsole();
            conAfterBoot = con;
            est = useExtras();
        }
        const std::string& cf = fs.files[ext::kPath];
        const std::string& ef = fs.files[ext::kExtrasPath];
        const bool conImage = memcmp(con.data(), cf.data() + 160, 400) == 0;
        const bool relImage = memcmp(region.data(), ef.data() + 160 + sizeof(ext_rel_table_t), 288) == 0;
        bool conTail = true;
        for (uint32_t i = kLoadBytes; i < kSlotBytes; i++) conTail = conTail && con[i] == 0xC5;
        check(cst == ext::Status::kLoaded && est == ext::ModitState::kReady,
              extrasFirst ? "two tenants, extras.plg first: both loaded" : "two tenants, console first: both loaded");
        check(conImage && relImage && conTail, "  each image in its own memory, the console slot tail untouched");
        check(extrasFirst || con == conAfterBoot, "  loading extras.plg writes nothing into the console slot");
        check(table.update == conUpdate && table.log == conLog && table.modit == extrasModit,
              "  the hook table keeps the console's and the bundle's pointers (merged field by field)");
        check(crcCalls == 1, "  the image id is computed once for both");
    }
    {   // mergeHooks: a null field never clears one set before
        ext_hooks_t t, a, b;
        memset(&t, 0, sizeof(t));
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        t.struct_size = sizeof(t);
        a.usbSof = +[]() {};
        a.spinExpired = +[](uint32_t) { return 1; };
        b.modit = +[](uint32_t) -> uint32_t { return 1; };
        b.struct_size = 1234;
        ext::mergeHooks(t, a);
        ext::mergeHooks(t, b);
        check(t.usbSof == a.usbSof && t.spinExpired == a.spinExpired && t.modit == b.modit && t.struct_size == sizeof(t) && !t.update && !t.logv,
              "mergeHooks: set fields copied, null fields keep the old value, struct_size stays the image's");
    }
    check(ext::kRecoveryReplyBytes == 320, "recovery reply: 320 B at the console slot start (release modules never load there)");
    check(sizeof(ext_rel_table_t) == 56 && EXT_SUB_COUNT == 5 && EXT_REL_BYTES % EXT_SUB_ALIGN == 0,
          "sub-slot table: 56 B, five tenants, the region a whole number of 1 KB sub-slot units");
    // the popup lines: one for each refusal, different, short enough for the popup
    {
        const ext::ModitState all[] = {ext::ModitState::kMissing, ext::ModitState::kOtherImage, ext::ModitState::kDamaged};
        bool ok = true;
        for (int i = 0; i < 3; i++)
        {
            ok = ok && strlen(ext::moditLine1(all[i])) > 0 && strlen(ext::moditLine1(all[i])) <= 36 && strlen(ext::moditLine2(all[i])) > 0 &&
                 strlen(ext::moditLine2(all[i])) <= 36;
            for (int j = i + 1; j < 3; j++) ok = ok && strcmp(ext::moditLine1(all[i]), ext::moditLine1(all[j])) != 0;
        }
        check(ok, "popup lines: three refusals, three different first lines, both lines 1 to 36 characters");
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
}

// A real extras.plg (build_extras.sh output): loaded on use into the release region by the image and the address it was packed for
void realExtras(const char* path, uint32_t imageCrc, uint32_t run)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        check(false, "real extras.plg: open");
        return;
    }
    std::string bytes;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
    fclose(f);
    plug_file_header h;
    memcpy(&h, bytes.data(), sizeof(h));
    printf("real extras.plg %s: %zu B, image %u B, bss %u B, entry +0x%x, image id %08x, run %08x\n", path, bytes.size(), h.image_size,
           h.bss_size, h.entry_offset, h.reserved[0], h.reserved[1]);
    std::vector<uint8_t> region(EXT_REL_BYTES, 0xEE);
    ext_rel_table_t t;
    memset(&t, 0, sizeof(t));
    auto use = [&](uint32_t crc, uint32_t base) {
        MemFs fs;
        fs.files[ext::kExtrasPath] = bytes;
        int entered = 0;
        const ext::Slot s{region.data(), EXT_REL_BYTES, uintptr_t(base)};
        const ext::ModitState st = ext::extrasLoad(fs, s, [&] { return crc; }, [&](const plug_file_header&) { return ++entered, true; }, &t);
        return std::make_pair(st, entered);
    };
    auto good = use(imageCrc, run);
    check(good.first == ext::ModitState::kReady && good.second == 1, "real extras.plg: ready on use");
    // the sub-slot table pack_ext.py wrote from the linked sizes (extras.ld): every tenant has code, the entry is in the MOD/IT one, the
    // image part of each tenant ends inside its own sub-slot
    {
        static const char* const names[] = {"modit", "theme", "selftest", "takefile", "capture"};
        const uint8_t* img = reinterpret_cast<const uint8_t*>(bytes.data()) + h.header_size;
        bool ok = t.version == EXT_REL_TABLE_VERSION && t.used_bytes <= EXT_REL_BYTES && h.entry_offset < t.sub[0].bytes;
        for (uint32_t i = 0; i < EXT_SUB_COUNT; i++)
        {
            uint32_t last = 0;
            for (uint32_t k = t.sub[i].at; k < t.sub[i].at + t.sub[i].bytes && k < h.image_size; k++) if (img[k]) last = k - t.sub[i].at + 1;
            printf("  %-9s +%6u %6u B, file bytes used %u\n", names[i], t.sub[i].at, t.sub[i].bytes, last);
            ok = ok && last > 0;
        }
        printf("  %u of %u B of the region\n", t.used_bytes, EXT_REL_BYTES);
        check(ok, "real extras.plg: table version 2, five tenants with code, entry in the MOD/IT sub-slot, inside EXT_REL_BYTES");
    }
    check(use(imageCrc ^ 1u, run).first == ext::ModitState::kOtherImage, "  another image: refused with the other-firmware popup");
    check(use(imageCrc, run + 16).first == ext::ModitState::kDamaged, "  another address: damaged");
}
}   // namespace

int main(int argc, char** argv)
{
    synthetic();
    moditCases();
    // a 4th argument "rel": the file is extras.plg (release region), else the console module.plg
    if (argc == 5 && strcmp(argv[4], "rel") == 0) realExtras(argv[1], uint32_t(strtoul(argv[2], nullptr, 0)), uint32_t(strtoul(argv[3], nullptr, 0)));
    else if (argc == 4) realModule(argv[1], uint32_t(strtoul(argv[2], nullptr, 0)), uint32_t(strtoul(argv[3], nullptr, 0)));
    printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
