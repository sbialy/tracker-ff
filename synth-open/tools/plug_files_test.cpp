// Host test of plug_files.h (PLUG_EXT_FILES, ABI 1.4): the sorted /Waves list (subfolders, hidden and AppleDouble files, long paths,
// the PLUG_FILES_MAX cap keeping the smallest paths whatever the directory order) and the .wav reader (PCM 8/16/24/32, float, stereo,
// WAVE_FORMAT_EXTENSIBLE, chunks before data, pieces, too long, not a wave). Builds a card folder in a temp dir.
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_files_test.cpp -o /tmp/plug_files_test && /tmp/plug_files_test
#include "plug_files.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
int g_fail;
#define CHECK(c)                                                                                                                   \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!(c))                                                                                                                  \
        {                                                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                                    \
            g_fail++;                                                                                                              \
        }                                                                                                                          \
    } while (0)

std::string g_root;

// The card: a folder. Directory entries come in reverse readdir order, so the list cannot rely on the order the card gives
struct PosixFs
{
    std::vector<std::string> names;
    std::vector<int> dirs;
    size_t at = 0;
    FILE* f = nullptr;
    bool dirOpen(const char* p)
    {
        names.clear();
        dirs.clear();
        DIR* d = opendir((g_root + p).c_str());
        if (!d) return false;
        while (dirent* e = readdir(d))
        {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            names.insert(names.begin(), e->d_name);
            dirs.insert(dirs.begin(), e->d_type == DT_DIR);
        }
        closedir(d);
        at = 0;
        return true;
    }
    bool dirNext(char* name, bool& isDir, bool& hidden)
    {
        if (at >= names.size()) return false;
        snprintf(name, plug::kWaveName, "%s", names[at].c_str());
        isDir = dirs[at];
        hidden = names[at].rfind("HID", 0) == 0;   // stands in for the FAT hidden attribute
        at++;
        return true;
    }
    void dirClose() {}
    bool open(const char* p)
    {
        f = fopen((g_root + p).c_str(), "rb");
        return f != nullptr;
    }
    int read(void* d, int n) { return int(fread(d, 1, size_t(n), f)); }
    bool seek(uint32_t pos) { return fseek(f, long(pos), SEEK_SET) == 0; }
    void close()
    {
        if (f) fclose(f);
        f = nullptr;
    }
};

void put32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

// A wave of `frames` frames whose first channel is value(i) as int16 (written in the given format), the second channel its negative
std::vector<uint8_t> wav(uint16_t tag, uint16_t ch, uint16_t bits, uint32_t frames, bool extensible = false, bool list = false)
{
    std::vector<uint8_t> d;
    for (uint32_t i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++)
        {
            const int16_t v = int16_t((int(i) * 97 % 65536) - 32768) * (c ? -1 : 1);
            if (tag == 3)
            {
                float x = v / 32767.0f;
                uint32_t u;
                memcpy(&u, &x, 4);
                put32(d, u);
            }
            else if (bits == 8)
                d.push_back(uint8_t((v >> 8) + 128));
            else if (bits == 16)
                put16(d, uint16_t(v));
            else if (bits == 24)
            {
                d.push_back(0x5A);
                put16(d, uint16_t(v));
            }
            else
            {
                put16(d, 0x1234);
                put16(d, uint16_t(v));
            }
        }
    std::vector<uint8_t> b;
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(b, 0);
    b.insert(b.end(), {'W', 'A', 'V', 'E'});
    if (list)
    {
        b.insert(b.end(), {'J', 'U', 'N', 'K'});
        put32(b, 7);
        for (int i = 0; i < 8; i++) b.push_back(0);   // odd size + pad byte
    }
    b.insert(b.end(), {'f', 'm', 't', ' '});
    put32(b, extensible ? 40 : 16);
    put16(b, extensible ? 0xFFFE : tag);
    put16(b, ch);
    put32(b, 44100);
    put32(b, 44100u * ch * (bits / 8));
    put16(b, uint16_t(ch * (bits / 8)));
    put16(b, bits);
    if (extensible)
    {
        put16(b, 22);
        put16(b, bits);
        put32(b, 0);
        put16(b, tag);
        for (int i = 0; i < 14; i++) b.push_back(0);
    }
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, uint32_t(d.size()));
    b.insert(b.end(), d.begin(), d.end());
    return b;
}

void write(const std::string& rel, const std::vector<uint8_t>& b)
{
    FILE* f = fopen((g_root + rel).c_str(), "wb");
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
}

int16_t expect(uint32_t i)
{
    return int16_t((int(i) * 97 % 65536) - 32768);
}

int find(const plug::WaveList& w, const char* p)
{
    for (int i = 0; i < w.n; i++)
        if (!strcmp(w.at(i), p)) return i;
    return -1;
}
}   // namespace

int main()
{
    char tmpl[] = "/tmp/plug_files_test.XXXXXX";
    g_root = mkdtemp(tmpl);
    mkdir((g_root + "/Waves").c_str(), 0755);
    mkdir((g_root + "/Waves/AKWF_bw_saw").c_str(), 0755);
    mkdir((g_root + "/Waves/B").c_str(), 0755);
    mkdir((g_root + "/Waves/HIDDEN").c_str(), 0755);
    mkdir((g_root + "/Waves/B/deeper").c_str(), 0755);
    write("/Waves/zz.wav", wav(1, 1, 16, 600));
    write("/Waves/a16.WAV", wav(1, 1, 16, 600));
    write("/Waves/._a16.WAV", wav(1, 1, 16, 600));
    write("/Waves/notes.txt", {1, 2, 3});
    write("/Waves/HIDhidden.wav", wav(1, 1, 16, 600));
    write("/Waves/HIDDEN/in_hidden.wav", wav(1, 1, 16, 600));
    write("/Waves/AKWF_bw_saw/AKWF_saw_0002.wav", wav(1, 2, 16, 600));
    write("/Waves/AKWF_bw_saw/AKWF_saw_0001.wav", wav(1, 1, 16, 600));
    write("/Waves/B/u8.wav", wav(1, 1, 8, 256));
    write("/Waves/B/s24.wav", wav(1, 2, 24, 300));
    write("/Waves/B/s32.wav", wav(1, 1, 32, 300));
    write("/Waves/B/f32.wav", wav(3, 1, 32, 2048));
    write("/Waves/B/ext.wav", wav(1, 1, 16, 128, true, true));
    write("/Waves/B/long.wav", wav(1, 1, 16, 2049));
    write("/Waves/B/bad.wav", {'R', 'I', 'F', 'X', 0, 0, 0, 0, 'W', 'A', 'V', 'E'});
    write("/Waves/B/deeper/two_levels.wav", wav(1, 1, 16, 600));
    write("/Waves/B/a_name_long_enough_to_pass_the_path_limit_0001.wav", wav(1, 1, 16, 600));

    static plug::WaveList w;   // 57 KB, not on the stack
    PosixFs fs;
    plug::waveBuild(fs, w, "/Waves");
    for (int i = 0; i < w.n; i++) printf("  %d %s\n", i, w.at(i));
    const char* want[] = {"AKWF_bw_saw/AKWF_saw_0001.wav", "AKWF_bw_saw/AKWF_saw_0002.wav", "B/bad.wav", "B/ext.wav", "B/f32.wav",
                          "B/long.wav", "B/s24.wav", "B/s32.wav", "B/u8.wav", "a16.WAV", "zz.wav"};
    CHECK(w.built);
    CHECK(w.n == int(sizeof(want) / sizeof(want[0])));
    for (int i = 0; i < w.n && i < int(sizeof(want) / sizeof(want[0])); i++) CHECK(!strcmp(w.at(i), want[i]));
    CHECK(w.skipped == 1);   // the long path, nothing else (hidden, "._", .txt and two levels down are simply not waves of the list)

    // reads go through the real waveRead (the Fs puts the temp root in front of every path)
    const std::string base = "/Waves";
    int16_t buf[2100];
    auto rd = [&](const char* p, uint32_t first, uint32_t max) {
        memset(buf, 0x55, sizeof(buf));
        return plug::waveRead(fs, w, uint32_t(find(w, p)), first, buf, max, base.c_str());
    };
    CHECK(rd("a16.WAV", 0, 2048) == 600);
    bool ok = true;
    for (uint32_t i = 0; i < 600; i++) ok = ok && buf[i] == expect(i);
    CHECK(ok);
    CHECK(buf[600] == 0x5555);   // nothing written past the file
    CHECK(rd("AKWF_bw_saw/AKWF_saw_0002.wav", 0, 2048) == 600);   // stereo: the first channel
    ok = true;
    for (uint32_t i = 0; i < 600; i++) ok = ok && buf[i] == expect(i);
    CHECK(ok);
    CHECK(rd("a16.WAV", 590, 256) == 600);   // a piece: 10 frames from 590
    ok = true;
    for (uint32_t i = 0; i < 10; i++) ok = ok && buf[i] == expect(590 + i);
    CHECK(ok && buf[10] == 0x5555);
    CHECK(rd("a16.WAV", 100, 5) == 600 && buf[0] == expect(100) && buf[4] == expect(104) && buf[5] == 0x5555);
    CHECK(rd("a16.WAV", 600, 16) == 600 && buf[0] == 0x5555);   // past the end: the size only
    CHECK(rd("B/u8.wav", 0, 2048) == 256);
    ok = true;
    for (uint32_t i = 0; i < 256; i++) ok = ok && buf[i] == int16_t((expect(i) >> 8) * 256);
    CHECK(ok);
    CHECK(rd("B/s24.wav", 0, 2048) == 300);
    ok = true;
    for (uint32_t i = 0; i < 300; i++) ok = ok && buf[i] == expect(i);
    CHECK(ok);
    CHECK(rd("B/s32.wav", 0, 2048) == 300);
    ok = true;
    for (uint32_t i = 0; i < 300; i++) ok = ok && buf[i] == expect(i);
    CHECK(ok);
    CHECK(rd("B/f32.wav", 0, 2048) == 2048);
    ok = true;
    for (uint32_t i = 0; i < 2048; i++) ok = ok && abs(buf[i] - expect(i)) <= 1;
    CHECK(ok);
    CHECK(rd("B/ext.wav", 0, 2048) == 128 && buf[5] == expect(5));   // extensible, a JUNK chunk of odd size first
    CHECK(rd("B/long.wav", 0, 2048) == PLUG_FILES_E_LONG);
    CHECK(rd("B/bad.wav", 0, 2048) == PLUG_FILES_E_FORMAT);
    CHECK(plug::waveRead(fs, w, uint32_t(w.n), 0, buf, 16, base.c_str()) == PLUG_FILES_E_NONE);
    unlink((g_root + "/Waves/zz.wav").c_str());
    CHECK(rd("zz.wav", 0, 16) == PLUG_FILES_E_CARD);   // listed, then gone from the card

    // ---- by path (ABI 1.4 tail): wavePathOk, waveReadPath, waveNext
    for (const char* ok1 : {"a16.WAV", "x.wav", "AKWF_bw_saw/AKWF_saw_0001.wav", "B/u8.wav", "a b/c d.wav", "B/deeper/two_levels.wav"})
        CHECK(plug::wavePathOk(ok1));
    for (const char* bad : {"", ".wav", "a.txt", "/a.wav", "a.wav/", "a//b.wav", "../a.wav", "B/../a.wav", "./a.wav", "B/./a.wav", "._a.wav",
                            "B/._a.wav", "B\\a.wav", "C:a.wav", "a*.wav", "a?.wav", "a|b.wav", "a\"b.wav", "B/.wav", "a.wa", "a.wavx"})
        CHECK(!plug::wavePathOk(bad));
    CHECK(!plug::wavePathOk(nullptr));
    CHECK(!plug::wavePathOk("a\tb.wav") && !plug::wavePathOk("a\x7f.wav") && !plug::wavePathOk("\xc3\xa4.wav"));
    {
        std::string longest(43, 'a');                      // 43 + ".wav" = 47 characters: the longest that fits PLUG_FILES_PATH
        CHECK(plug::wavePathOk((longest + ".wav").c_str()));
        CHECK(!plug::wavePathOk((longest + "a.wav").c_str()));
    }
    auto rp = [&](const char* p, uint32_t first, uint32_t max) {
        memset(buf, 0x55, sizeof(buf));
        return plug::waveReadPath(fs, p, first, buf, max, base.c_str());
    };
    CHECK(rp("a16.WAV", 0, 2048) == 600 && buf[0] == expect(0) && buf[599] == expect(599) && buf[600] == 0x5555);
    CHECK(rp("a16.WAV", 100, 5) == 600 && buf[0] == expect(100) && buf[5] == 0x5555);
    CHECK(rp("B/deeper/two_levels.wav", 0, 2048) == 600);   // a path reaches below the list's one level
    CHECK(rp("B/a_name_long_enough_to_pass_the_path_limit_0001.wav", 0, 16) == PLUG_FILES_E_NONE);   // too long for a path (48+), and the list skipped it too
    CHECK(rp("zz.wav", 0, 16) == PLUG_FILES_E_NONE);          // gone from the card: not a card fault, the same answer as a bad path
    CHECK(rp("nothere.wav", 0, 16) == PLUG_FILES_E_NONE);
    CHECK(rp("../outside.wav", 0, 16) == PLUG_FILES_E_NONE);
    // a path with a folder that the card does not have reaches the same file name directly in /Waves (the flat curated folder of the release zip)
    CHECK(rp("AKWF_bw_sin/a16.WAV", 0, 2048) == 600 && buf[0] == expect(0) && buf[599] == expect(599));
    CHECK(rp("AKWF_bw_sin/nothere.wav", 0, 16) == PLUG_FILES_E_NONE);
    CHECK(rp("B/a16.WAV", 0, 2048) == 600);                  // the folder B exists but does not hold a16.WAV: the flat one answers
    CHECK(rp("B/u8.wav", 0, 2048) == 256);                   // the exact path still wins
    CHECK(plug::waveFlatName("A/b/c.wav") && !strcmp(plug::waveFlatName("A/b/c.wav"), "c.wav") && !plug::waveFlatName("c.wav") && !plug::waveFlatName("A/.c.wav"));
    // list membership as the editor label asks it
    CHECK(plug::waveListHas(w, "a16.WAV") && plug::waveListHas(w, "B/u8.wav") && plug::waveListHas(w, "AKWF_bw_sin/a16.WAV"));
    CHECK(!plug::waveListHas(w, "nothere.wav") && !plug::waveListHas(w, "AKWF_bw_sin/nothere.wav") && !plug::waveListHas(w, "B/nothere.wav"));
    CHECK(rp("B/long.wav", 0, 16) == PLUG_FILES_E_LONG && rp("B/bad.wav", 0, 16) == PLUG_FILES_E_FORMAT);
    // the same bytes by index and by path, for every listed file that reads
    {
        bool same = true;
        int16_t b2[2100];
        for (int i = 0; i < w.n; i++)
        {
            const int a = plug::waveRead(fs, w, uint32_t(i), 0, buf, 2048, base.c_str());
            const int b = plug::waveReadPath(fs, w.at(i), 0, b2, 2048, base.c_str());
            const bool gone = a == PLUG_FILES_E_CARD && b == PLUG_FILES_E_NONE;   // listed zz.wav, deleted above
            same = same && (gone || (a == b && (a <= 0 || !memcmp(buf, b2, size_t(a) * 2))));
        }
        CHECK(same);
    }
    // waveNext on this card (zz.wav is gone): the order of the list, root files and folders mixed, then the end
    {
        plug::waveBuild(fs, w, "/Waves");
        char nm[plug::kWaveName], out[PLUG_FILES_PATH];
        bool chain = w.n > 3;
        for (int i = 0; i + 1 < w.n; i++)
            chain = chain && plug::waveNext(fs, nm, w.at(i), out, sizeof(out), base.c_str()) == int(strlen(w.at(i + 1))) && !strcmp(out, w.at(i + 1));
        CHECK(chain);
        CHECK(plug::waveNext(fs, nm, w.at(w.n - 1), out, sizeof(out), base.c_str()) == PLUG_FILES_E_NONE);   // after the last
        CHECK(plug::waveNext(fs, nm, "AKWF_bw_saw/AKWF_saw_0001.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "AKWF_bw_saw/AKWF_saw_0002.wav"));
        // a path that is not on the card: the first path above it
        CHECK(plug::waveNext(fs, nm, "AKWF_bw_saw/AKWF_saw_0001a.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "AKWF_bw_saw/AKWF_saw_0002.wav"));
        CHECK(plug::waveNext(fs, nm, "A.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "AKWF_bw_saw/AKWF_saw_0001.wav"));   // 'A.' < 'AK'
        CHECK(plug::waveNext(fs, nm, "AKWF_bw_saw/zzz.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "B/bad.wav"));        // past the folder
        CHECK(plug::waveNext(fs, nm, "B/u8.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "a16.WAV"));                      // folder end, root file next
        CHECK(plug::waveNext(fs, nm, "zzzz.wav", out, sizeof(out), base.c_str()) == PLUG_FILES_E_NONE);
        CHECK(plug::waveNext(fs, nm, "../x.wav", out, sizeof(out), base.c_str()) == PLUG_FILES_E_NONE);                                // not valid
        CHECK(plug::waveNext(fs, nm, "a16.WAV", out, 4, base.c_str()) == PLUG_FILES_E_NONE);                                            // nothing follows (zz.wav is gone)
        write("/Waves/zz.wav", wav(1, 1, 16, 600));
        char small[4];
        CHECK(plug::waveNext(fs, nm, "a16.WAV", small, sizeof(small), base.c_str()) == 6 && !strcmp(small, "zz."));                    // cut to fit, the length is the whole
        // empty and wave-less folders between the folders do not stop the walk
        mkdir((g_root + "/Waves/AKWF_empty").c_str(), 0755);
        mkdir((g_root + "/Waves/AKWF_text").c_str(), 0755);
        write("/Waves/AKWF_text/readme.txt", {1});
        CHECK(plug::waveNext(fs, nm, "AKWF_bw_saw/AKWF_saw_0002.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "B/bad.wav"));
        write("/Waves/AKWF_zzz_last.wav", wav(1, 1, 16, 10));   // a root file between folder keys: "AKWF_zzz_last.wav" < "B/"
        CHECK(plug::waveNext(fs, nm, "AKWF_bw_saw/AKWF_saw_0002.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "AKWF_zzz_last.wav"));
        CHECK(plug::waveNext(fs, nm, "AKWF_zzz_last.wav", out, sizeof(out), base.c_str()) > 0 && !strcmp(out, "B/bad.wav"));
        unlink((g_root + "/Waves/AKWF_zzz_last.wav").c_str());
    }

    // the cap: 1100 files in a shuffled directory order, the list keeps the 1024 smallest names, sorted
    mkdir((g_root + "/Big").c_str(), 0755);
    std::vector<int> ids;
    for (int i = 0; i < 1100; i++) ids.push_back((i * 7919) % 1100);
    for (int id : ids)
    {
        char p[64];
        snprintf(p, sizeof(p), "/Big/w%04d.wav", id);
        write(p, {0});
    }
    plug::waveBuild(fs, w, "/Big");
    CHECK(w.n == int(PLUG_FILES_MAX));
    CHECK(w.skipped == 1100 - PLUG_FILES_MAX);
    ok = true;
    for (int i = 0; i < w.n; i++)
    {
        char p[16];
        snprintf(p, sizeof(p), "w%04d.wav", i);
        ok = ok && !strcmp(w.at(i), p);
    }
    CHECK(ok);
    // no /Waves at all: an empty list
    plug::waveBuild(fs, w, "/Nothing");
    CHECK(w.built && w.n == 0);

    std::string rm = "rm -rf '" + g_root + "'";
    if (system(rm.c_str()) != 0) printf("cleanup failed\n");
    printf("%s (%d failures)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
