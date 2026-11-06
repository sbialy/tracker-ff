// Host test of the synth plugin logo records (synth-open/plug_logo.h): the PLGI parser, the source order (sidecar, then the
// .plg trailer, then the text name) and the trailer offset the scan takes from the manifest. plug_synth.cpp runs the same code
// on the SD card. Files given on the command line are checked as the firmware would read them: a .plgi must be one good record,
// a .plg must carry a good trailer.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_logo_test.cpp -o /tmp/plug_logo_test && /tmp/plug_logo_test [files]
#include "plug_logo.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace plug;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

std::string makeRecord(uint8_t seed)
{
    std::string r(kLogoRecord, '\0');
    for (uint32_t i = 0; i < kLogoPixels; i++) r[kLogoHeader + i] = char(uint8_t(seed + i * 7));
    LogoHeader h = {kLogoMagic, kLogoVersion, kLogoFormatL2, kLogoMiniW, kLogoMiniH, kLogoMedW, kLogoMedH, 0,
                    crc32(0, reinterpret_cast<const uint8_t*>(r.data()) + kLogoHeader, kLogoPixels)};
    memcpy(&r[0], &h, sizeof(h));
    return r;
}

LogoErr checkStr(const std::string& r)
{
    return logoCheck(reinterpret_cast<const uint8_t*>(r.data()), int(r.size()));
}

// An in-memory card: path -> bytes
struct MemFs
{
    std::map<std::string, std::string> files;
    std::vector<std::string> opened;
    int readAt(const char* p, uint32_t off, void* buf, int cap)
    {
        opened.push_back(p);
        auto it = files.find(p);
        if (it == files.end()) return -1;
        if (off > it->second.size()) return -1;
        const int left = int(it->second.size() - off);
        const int n = left < cap ? left : cap;
        memcpy(buf, it->second.data() + off, size_t(n));
        return n;
    }
};

// a .plg stand-in: header, manifest naming the trailer at off, filler, the record
std::string makePlg(uint32_t trailerOff, const std::string& rec)
{
    plug_file_header h = {};
    h.magic = PLUG_MAGIC;
    h.header_size = sizeof(plug_file_header) + sizeof(plug_manifest);
    plug_manifest m = {};
    m.struct_size = sizeof(m);
    m.reserved[0] = trailerOff;
    std::string f(reinterpret_cast<const char*>(&h), sizeof(h));
    f.append(reinterpret_cast<const char*>(&m), sizeof(m));
    if (trailerOff > f.size()) f.append(trailerOff - f.size(), 'x');
    return f + rec;
}

LogoSource find(MemFs& fs, const char* file, uint32_t off, std::string* got = nullptr, LogoErr* err = nullptr)
{
    uint8_t rec[kLogoRecord + 1];
    memset(rec, 0, sizeof(rec));
    const LogoSource s = logoFind(fs, "/Plugins/Synth", file, off, rec, err);
    if (got) *got = std::string(reinterpret_cast<const char*>(rec), kLogoRecord);
    return s;
}

int checkFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        printf("FAIL  %s does not open\n", path);
        return 1;
    }
    std::string d;
    char b[4096];
    for (size_t n; (n = fread(b, 1, sizeof(b), f)) > 0;) d.append(b, n);
    fclose(f);
    const size_t l = strlen(path);
    if (l > 5 && strcasecmp(path + l - 5, ".plgi") == 0)
    {
        const bool ok = d.size() == kLogoRecord && checkStr(d) == kLogoOk;
        printf("%s  %s: sidecar record, %zu B\n", ok ? "ok  " : "FAIL", path, d.size());
        return ok ? 0 : 1;
    }
    // a .plg: the trailer the scan would find (scanIndex rule: manifest reserved[0] past the header)
    plug_file_header h;
    plug_manifest m;
    if (d.size() < sizeof(h) + sizeof(m))
    {
        printf("FAIL  %s: too short for a .plg\n", path);
        return 1;
    }
    memcpy(&h, d.data(), sizeof(h));
    memcpy(&m, d.data() + sizeof(h), sizeof(m));
    const uint32_t off = m.reserved[0] >= h.header_size ? m.reserved[0] : 0;
    const bool ok = checkHeader(h, h.kind) == kOk && off && off + kLogoRecord <= d.size() && checkStr(d.substr(off)) == kLogoOk;
    printf("%s  %s: logo trailer at %u\n", ok ? "ok  " : "FAIL", path, off);
    return ok ? 0 : 1;
}
}   // namespace

int main(int argc, char** argv)
{
    // ---- the record
    const std::string good = makeRecord(3);
    check(good.size() == 1676 && kLogoPixels == 1660 && kLogoMiniBytes == 580 && kLogoMedBytes == 1080, "record is 16 + 580 + 1080 B");
    check(checkStr(good) == kLogoOk, "a good record passes");
    check(checkStr(good.substr(0, kLogoRecord - 1)) == kLogoShort, "truncated by one byte: short");
    check(checkStr(good.substr(0, 16)) == kLogoShort, "header only: short");
    check(checkStr("") == kLogoShort, "empty: short");
    {
        std::string r = good;
        r[0] = 'X';
        check(checkStr(r) == kLogoBadMagic, "bad magic");
        r = good;
        r[4] = 2;
        check(checkStr(r) == kLogoBadMagic, "version 2");
        r = good;
        r[5] = 3;
        check(checkStr(r) == kLogoBadSize, "format L4");
        r = good;
        r[7] = 28;
        check(checkStr(r) == kLogoBadSize, "mini 80x28 (wrong size)");
        r = good;
        r[8] = 100;
        check(checkStr(r) == kLogoBadSize, "med 100x40 (wrong size)");
        r = good;
        r[kLogoRecord - 1] ^= 1;
        check(checkStr(r) == kLogoBadCrc, "one pixel bit flipped: bad crc");
        r = good;
        r[12] ^= 1;
        check(checkStr(r) == kLogoBadCrc, "crc field damaged");
    }

    // ---- names
    char p[64];
    check(logoSidecarPath(p, sizeof(p), "/Plugins/Synth", "ju6.plg") && strcmp(p, "/Plugins/Synth/ju6.plgi") == 0, "ju6.plg -> ju6.plgi");
    check(logoSidecarPath(p, sizeof(p), "/Plugins/Synth", "FM4.PLG") && strcmp(p, "/Plugins/Synth/FM4.plgi") == 0, "FM4.PLG -> FM4.plgi");
    check(!logoSidecarPath(p, sizeof(p), "/Plugins/Synth", "._ju6.plg"), "macOS ._ju6.plg has no sidecar");
    check(!logoSidecarPath(p, sizeof(p), "/Plugins/Synth", "ju6.plgi"), "a .plgi is no plugin name");
    check(!logoSidecarPath(p, sizeof(p), "/Plugins/Synth", ".plg"), "bare .plg");
    check(!logoSidecarPath(p, 20, "/Plugins/Synth", "ju6.plg"), "path that does not fit");

    // ---- the source order
    const std::string side = makeRecord(50), trail = makeRecord(90);
    const uint32_t off = 400;
    const std::string plg = makePlg(off, trail);
    {
        MemFs fs;
        std::string got;
        LogoErr e = kLogoOk;
        check(find(fs, "ju6.plg", 0, &got, &e) == kLogoNone && e == kLogoShort, "no sidecar, no trailer: text");
        fs.files["/Plugins/Synth/ju6.plg"] = plg;
        check(find(fs, "ju6.plg", off, &got) == kLogoTrailer && got == trail, "trailer only: trailer");
        fs.files["/Plugins/Synth/ju6.plgi"] = side;
        check(find(fs, "ju6.plg", off, &got) == kLogoSidecar && got == side, "sidecar and trailer: the sidecar wins");
        check(fs.opened.back() == "/Plugins/Synth/ju6.plgi", "the trailer is not read when the sidecar is good");
        check(find(fs, "ju6.plg", 0, &got) == kLogoSidecar && got == side, "sidecar, .plg without trailer: sidecar");
        fs.files["/Plugins/Synth/ju6.plgi"] = side + "x";
        check(find(fs, "ju6.plg", off, &got, &e) == kLogoTrailer && got == trail, "sidecar one byte too long: trailer");
        check(find(fs, "ju6.plg", 0, nullptr, &e) == kLogoNone && e == kLogoBadSize, "sidecar too long, no trailer: text (wrong size)");
        fs.files["/Plugins/Synth/ju6.plgi"] = side.substr(0, 1000);
        check(find(fs, "ju6.plg", 0, nullptr, &e) == kLogoNone && e == kLogoShort, "truncated sidecar: text");
        check(find(fs, "ju6.plg", off) == kLogoTrailer, "truncated sidecar with a trailer: trailer");
        std::string bad = side;
        bad[1] = 'Q';
        fs.files["/Plugins/Synth/ju6.plgi"] = bad;
        check(find(fs, "ju6.plg", 0, nullptr, &e) == kLogoNone && e == kLogoBadMagic, "bad magic sidecar: text");
        fs.files["/Plugins/Synth/ju6.plgi"] = "";
        check(find(fs, "ju6.plg", 0, nullptr, &e) == kLogoNone && e == kLogoShort, "empty sidecar: text");
        fs.files.erase("/Plugins/Synth/ju6.plgi");
        fs.files["/Plugins/Synth/ju6.plg"] = plg.substr(0, plg.size() - 10);
        check(find(fs, "ju6.plg", off, nullptr, &e) == kLogoNone && e == kLogoShort, "truncated trailer: text");
        fs.files["/Plugins/Synth/ju6.plg"] = plg;
        check(find(fs, "ju6.plg", off + 4, nullptr, &e) == kLogoNone && e == kLogoShort, "trailer offset off by 4 (runs past the end): text");
        fs.files["/Plugins/Synth/ju6.plg"] = plg + "pad";
        check(find(fs, "ju6.plg", off + 2, nullptr, &e) == kLogoNone && e == kLogoBadMagic, "trailer offset off by 2: text (bad magic)");
        fs.files["/Plugins/Synth/ju6.plg"] = plg;
        check(find(fs, "ju6.plg", 100000, nullptr, &e) == kLogoNone, "trailer offset past the file end: text");
        fs.files.erase("/Plugins/Synth/ju6.plg");
        fs.files["/Plugins/Synth/ju6.plgi"] = side;
        check(find(fs, "ju6.plg", off) == kLogoSidecar, ".plg gone from the card (installed engine), sidecar still counts");
        fs.files["/Plugins/Synth/._ju6.plgi"] = side;
        fs.opened.clear();
        check(find(fs, "._ju6.plg", 0) == kLogoNone && fs.opened.empty(), "._ju6.plg: nothing is read");
    }

    // ---- the scan takes the trailer offset from the manifest
    {
        struct ScanMem
        {
            std::map<std::string, std::string> files;
            void list(const char*, bool (*filter)(const char*), char* names, int, int maxNameLen, int& count, char** list)
            {
                int n = 0;
                for (auto& f : files)
                {
                    const char* base = f.first.c_str() + strlen("/Plugins/Synth/");
                    if (!filter(base) || n >= count) continue;
                    snprintf(names + n * maxNameLen, size_t(maxNameLen), "%s", base);
                    list[n] = names + n * maxNameLen;
                    n++;
                }
                count = n;
            }
            int read(const char* p, void* buf, int cap)
            {
                auto it = files.find(p);
                if (it == files.end()) return -1;
                const int n = int(it->second.size()) < cap ? int(it->second.size()) : cap;
                memcpy(buf, it->second.data(), size_t(n));
                return n;
            }
        } sm;
        auto plgOf = [&](uint32_t uid, uint32_t trailer) {
            std::string f = makePlg(trailer, trailer ? trail : std::string());
            plug_file_header h;
            memcpy(&h, f.data(), sizeof(h));
            h.abi_major = PLUG_ABI_MAJOR;
            h.cpu = PLUG_CPU_CM4F;
            h.kind = PLUG_KIND_SYNTH;
            h.uid = uid;
            memcpy(&f[0], &h, sizeof(h));
            return f;
        };
        sm.files["/Plugins/Synth/a.plg"] = plgOf(1, off);
        sm.files["/Plugins/Synth/b.plg"] = plgOf(2, 0);
        sm.files["/Plugins/Synth/c.plg"] = plgOf(3, 100);   // inside the header and manifest: no trailer
        sm.files["/Plugins/Synth/._a.plg"] = plgOf(4, off);
        IndexEntry idx[8];
        char names[kScanMax * 40];
        char* list[kScanMax];
        ScanStats st;
        const int n = scanIndex(sm, "/Plugins/Synth", idx, 8, names, list, st, PLUG_KIND_SYNTH);
        uint32_t want[4] = {0, off, 0, 0};
        bool ok = n == 3;
        for (int i = 0; i < n; i++) ok = ok && idx[i].uid >= 1 && idx[i].uid <= 3 && idx[i].logoOff == want[idx[i].uid];
        check(ok, "scan: trailer offset from manifest reserved[0], 0 inside the header, ._a.plg not listed");
    }

    // ---- RAM_G pool
    check(kLogoRamG + kLogoSlots * kLogoPixels == 148700 && 148700 <= 150000 && kLogoRamGEnd - kLogoRamG == 21220,
          "12 slots 128,780..148,700 of the 21,220 B window, below the controls cache");

    for (int i = 1; i < argc; i++) g_fail += checkFile(argv[i]);
    printf(g_fail ? "FAILED (%d)\n" : "PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
