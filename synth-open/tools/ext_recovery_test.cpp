// The recovery console of the image (synth-open/ext_recovery.h) and the status line of a refused module (ext_loader.h Diag).
//   ext_recovery_test                  the rules, no files
//   ext_recovery_test module.plg CRC RUN   also boots the real module file with a wrong image id and a right one
#include "ext_loader.h"
#include "ext_recovery.h"

#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>

static int g_fail;
static void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

namespace
{
std::string reply(const char* line, const char* status = "refused: built for another firmware image", const char* detail = "",
                  bool* restart = nullptr)
{
    char out[320];
    bool r = false;
    const int n = ext::recoveryReply(line, "abc1234", status, detail, out, sizeof(out), restart ? restart : &r);
    return std::string(out, size_t(n));
}

struct Fs
{
    std::map<std::string, std::string> files;
    std::string cur;
    size_t pos = 0;
    bool exists(const char* p) { return files.count(p) != 0; }
    bool remove(const char* p) { return files.erase(p) != 0; }
    bool writeText(const char* p, const char* t, int n) { files[p] = std::string(t, size_t(n)); return true; }
    bool open(const char* p) { if (!files.count(p)) return false; cur = p; pos = 0; return true; }
    bool read(uint8_t* d, uint32_t n) { auto& f = files[cur]; if (pos + n > f.size()) return false; memcpy(d, f.data() + pos, n); pos += n; return true; }
    void close() {}
};

void lines()
{
    bool restart = false;
    check(reply("ping") == "@tracker-dev original abc1234 proto=1 recovery module: refused: built for another firmware image\n",
          "ping: the console's first line (the console ping parses product, build, proto), then the module status");
    check(reply("  ping extra") .rfind("@tracker-dev original abc1234 proto=1", 0) == 0, "ping with leading spaces and arguments");
    check(reply("module", "refused: built for another firmware image", " | this image: crc da27a007") ==
              "@ok module refused: built for another firmware image | this image: crc da27a007\n",
          "module: status and the loader's detail");
    check(reply("help").rfind("@ok ", 0) == 0, "help is an @ok line");
    check(reply("") .empty() && reply("   ").empty(), "a blank line gets no reply");
    check(reply("cpu 3") == "@err recovery console, no module runs (refused: built for another firmware image): cpu\n",
          "any other command: one @err line naming the command, never silence");
    check(reply("reset", "none: no module file", "", &restart) == "@ok reset pending\n" && restart, "reset: reply and restart request");
    restart = false;
    reply("ping", "x", "", &restart);
    check(!restart, "ping does not restart");
    std::string longCmd(200, 'x');
    const std::string r = reply(longCmd.c_str());
    check(r.size() < 200 && r.back() == '\n' && r.find('\n') == r.size() - 1, "a very long command name is cut, the reply is still one line");
    char tiny[24];
    bool rr = false;
    const int n = ext::recoveryReply("ping", "abc1234", "a long status text that does not fit", "", tiny, sizeof(tiny), &rr);
    check(n == int(sizeof(tiny)) - 1 && tiny[n - 1] == '\n' && tiny[n] == 0, "a reply that does not fit the buffer is cut at its end, '\\n' kept");

    ext::RecoveryLine l;
    std::vector<std::string> got;
    for (char c : std::string("ping\r\nmod") + "ule\n\nreset\n" + std::string(100, 'a') + "\nping\n")
        if (l.feed(c)) got.push_back(l.buf);
    check(got.size() == 6 && got[0] == "ping" && got[1] == "module" && got[2].empty() && got[3] == "reset",
          "line assembly: CRLF, a command split over two feeds, an empty line");
    check(got.size() == 6 && got[4].size() == sizeof(l.buf) - 1 && got[5] == "ping",
          "an over long line is cut to the buffer, ends at its newline, the next line is intact");
}

void statusLine(const char* path, uint32_t crc, uint32_t run)
{
    FILE* f = fopen(path, "rb");
    if (!f) { check(false, "open the module"); return; }
    std::string b;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.append(buf, n);
    fclose(f);
    auto go = [&](uint32_t dev, int* entered) {
        Fs fs;
        fs.files[ext::kPath] = b;
        std::vector<uint8_t> slot(EXT_SLOT_BYTES, 0);
        const ext::Slot s{slot.data(), EXT_LOAD_BYTES, uintptr_t(run)};
        const ext::Status st = ext::boot(
            fs, true, s, [&] { return dev; }, [&](const plug_file_header&) { ++*entered; return true; },
            [](ext::Status st) { return st == ext::Status::kOtherImage ? " | this image: crc 1234abcd" : ""; });
        return std::make_pair(st, fs.files[ext::kStatus]);
    };
    int e = 0;
    auto bad = go(crc ^ 0x00400000u, &e);
    check(bad.first == ext::Status::kOtherImage && e == 0, "wrong image id: refused, entry not called");
    check(bad.second.find("refused: built for another firmware image (module image ") == 0 && bad.second.find(" | this image: crc 1234abcd\n") != std::string::npos &&
              bad.second.back() == '\n',
          "the status line of kOtherImage carries the diag text");
    e = 0;
    auto good = go(crc, &e);
    check(good.first == ext::Status::kLoaded && e == 1 && good.second.find("loaded (module image ") == 0 && good.second.find('|') == std::string::npos,
          "right image id: loaded, no diag text on the line");
    // the diag callable is asked for every status, an empty answer leaves the line as before
    Fs none;
    std::vector<uint8_t> slot(EXT_SLOT_BYTES, 0);
    const ext::Slot s{slot.data(), EXT_LOAD_BYTES, uintptr_t(run)};
    const ext::Status st = ext::boot(none, true, s, [] { return 0u; }, [](const plug_file_header&) { return true; });
    check(st == ext::Status::kNone && none.files[ext::kStatus].rfind("none: no module file (module image 00000000 at 00000000)\n", 0) == 0,
          "default Diag (no argument): the line is the old one");
}
}   // namespace

int main(int argc, char** argv)
{
    lines();
    if (argc == 4) statusLine(argv[1], uint32_t(strtoul(argv[2], nullptr, 0)), uint32_t(strtoul(argv[3], nullptr, 0)));
    printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
