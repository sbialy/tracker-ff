// Host test of the Features registry and its file parser (core/source/features.cpp, ref/features-design.md), the real code
// against an in-memory card:
//   c++ -std=c++20 -O1 -Wall -DFEATURE_EXT_HOOKS -DFEATURES_TEST -I core/source -I core/include synth-open/tools/features_test.cpp \
//       core/source/features.cpp -o /tmp/features_test
// Also built with -fsanitize=address,undefined by run_tests.sh. Exit 0 when every check passes.
#include "features.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
int s_fail, s_checks;
void check(bool ok, const char* what)
{
    s_checks++;
    if (!ok) s_fail++;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
}

// ---- the card
std::map<std::string, std::string> s_files;
uint32_t s_ms;
int s_writes;
bool s_writeFails;
int cardRead(const char* p, char* buf, int cap)
{
    auto it = s_files.find(p);
    if (it == s_files.end()) return 0;
    const int n = int(it->second.size()) < cap ? int(it->second.size()) : cap;
    memcpy(buf, it->second.data(), size_t(n));
    return n;
}
bool cardWrite(const char* p, const char* buf, int n)
{
    if (s_writeFails) return false;
    s_writes++;
    s_files[p] = std::string(buf, size_t(n));
    return true;
}
bool cardExists(const char* p) { return s_files.count(p) != 0; }
void cardMkdir(const char* p) { s_files[p] = ""; }
void cardRemove(const char* p) { s_files.erase(p); }
uint32_t cardMillis() { return s_ms; }
const features::Io kIo = { cardRead, cardWrite, cardExists, cardMkdir, cardRemove, cardMillis };

// ---- descriptors
const char* const kAB[] = { "A", "B", "C" };
int s_apply, s_lastOn = -1, s_boot;
uint8_t s_running;
void apply(uint8_t on, const uint8_t*, uint8_t) { s_apply++; s_lastOn = on; }
void bootFn(uint8_t, const uint8_t*) { s_boot++; }
uint8_t running() { return s_running; }
uint8_t s_st[64];
int s_stN;
int saveState(uint8_t* b, int cap) { const int n = s_stN < cap ? s_stN : cap; memcpy(b, s_st, size_t(n)); return n; }
void loadState(const uint8_t* b, int n) { s_stN = n; memcpy(s_st, b, size_t(n)); }

const feat_param kParams[] = {
    { "mode", "Mode", FEAT_P_ENUM, 0, 0, 1, 0, 3, kAB, 0, nullptr },
    { "level", "Level", FEAT_P_INT, 10, 20, 15, 0, 0, nullptr, 0, nullptr },
    { "boot", "Boot", FEAT_P_ENUM, 0, 0, 0, 0, 2, kAB, 1, nullptr },
};
feat_desc make(const char* id)
{
    return { sizeof(feat_desc), FEAT_ABI, 1, id, "Test", "help", 0, 3, kParams, 16, bootFn, apply, nullptr, saveState, loadState,
             nullptr };
}

void fresh()
{
    features::clear();
    s_files.clear();
    s_ms = 1000;
    s_writes = 0;
    s_writeFails = false;
    s_apply = 0;
    s_lastOn = -1;
    s_boot = 0;
    s_stN = 0;
}

// one load of a file into the descriptor "t", returns its index
feat_desc s_desc;
int loadWith(const std::string& file)
{
    fresh();
    s_desc = make("t");
    const int i = features::add(&s_desc);
    s_files["/Features/t.cfg"] = file;
    features::load(&kIo);
    return i;
}
}   // namespace

int main()
{
    // ---- add(): refused descriptors are never listed
    fresh();
    feat_desc d = make("ok");
    check(features::add(nullptr) < 0, "add: null refused");
    d.size = 8;
    check(features::add(&d) < 0, "add: size smaller than the image's feat_desc refused");
    d = make("ok");
    d.abi = 2;
    check(features::add(&d) < 0, "add: another ABI refused");
    const char* badIds[] = { "", "ninechars", "Upper", "a/b", "a.b", "a b" };
    for (const char* id : badIds)
    {
        d = make(id);
        char what[64];
        snprintf(what, sizeof what, "add: id \"%s\" refused", id);
        check(features::add(&d) < 0, what);
    }
    d = make("ok");
    d.paramCount = 9;
    check(features::add(&d) < 0, "add: more than 8 params refused");
    feat_param bad[1] = { { "k", "K", FEAT_P_ENUM, 0, 0, 3, 0, 3, kAB, 0, nullptr } };
    d = make("ok");
    d.params = bad;
    d.paramCount = 1;
    check(features::add(&d) < 0, "add: enum default out of range refused");
    const char* const withNull[] = { "A", nullptr };
    bad[0] = { "k", "K", FEAT_P_ENUM, 0, 0, 0, 0, 2, withNull, 0, nullptr };
    check(features::add(&d) < 0, "add: enum with a null text refused");
    bad[0] = { "k", "K", FEAT_P_INT, 5, 4, 5, 0, 0, nullptr, 0, nullptr };
    check(features::add(&d) < 0, "add: int min > max refused");
    bad[0] = { "k", "K", 7, 0, 1, 0, 0, 0, nullptr, 0, nullptr };
    check(features::add(&d) < 0, "add: unknown param type refused");
    check(features::count() == 0, "add: nothing listed after the refusals");
    static feat_desc many[9];
    static char ids[9][4];
    int added = 0;
    for (int k = 0; k < 9; k++)
    {
        snprintf(ids[k], sizeof ids[k], "f%d", k);
        many[k] = make(ids[k]);
        if (features::add(&many[k]) >= 0) added++;
    }
    check(added == features::kMax, "add: the registry stops at kMax");
    feat_desc dup = make("f0");
    fresh();
    features::add(&many[0]);
    check(features::add(&dup) < 0, "add: duplicate id refused");

    // ---- no file: defaults, nothing written, no folder made
    int i = loadWith("");
    s_files.clear();
    fresh();
    s_desc = make("t");
    i = features::add(&s_desc);
    features::load(&kIo);
    check(!features::on(i) && features::value(i, 0) == 1 && features::value(i, 1) == 15, "no file: defaults");
    check(s_boot == 1 && s_apply == 0, "no file: boot() once, no apply()");
    s_ms += 5000;
    features::tick();
    check(s_writes == 0 && !cardExists("/Features"), "no file: a start that changes nothing writes nothing");

    // ---- round trip with the debounce
    features::set(i, true);
    *features::valueRef(i, 0) = 2;
    *features::valueRef(i, 1) = 20;
    features::changed();
    check(s_apply >= 3 && s_lastOn == 1, "edit: apply() for the switch and each param");
    s_ms += 500;
    features::tick();
    check(s_writes == 0, "edit: no write inside the 1 s debounce");
    s_ms += 600;
    features::tick();
    check(s_writes == 1 && cardExists("/Features"), "edit: one write after the debounce, folder made");
    const std::string saved = s_files["/Features/t.cfg"];
    check(saved.rfind("feature 1\nid t\nversion 1\non 1\nmode 2\nlevel 20\nboot 0\n", 0) == 0, "edit: file text");
    i = loadWith(saved);
    check(features::on(i) && features::value(i, 0) == 2 && features::value(i, 1) == 20, "round trip: values back");

    // ---- flush is synchronous (the power off path)
    features::set(i, false);
    features::flush();
    check(s_files["/Features/t.cfg"].find("on 0\n") != std::string::npos, "flush: written at once, no debounce");
    s_writeFails = true;
    features::set(i, true);
    features::flush();
    s_writeFails = false;
    features::flush();
    check(s_files["/Features/t.cfg"].find("on 1\n") != std::string::npos, "flush: a failed write stays dirty and is retried");

    // ---- hostile files: every one starts with defaults where it is broken, nothing crashes (ASan/UBSan in run_tests.sh)
    struct Case { const char* what; std::string text; int on, mode, level; };
    std::string big(4000, '\0');
    srand(7);
    for (char& c : big) c = char(rand() & 0xFF);
    std::string longLine = "feature 1\non 1\nmode " + std::string(380, '1') + "\n";
    std::string repeated = "feature 1\n";
    for (int k = 0; k < 90; k++) repeated += "on 1\n";
    const std::vector<Case> cases = {
        { "4000 random bytes", big, 0, 1, 15 },
        { "format 2", "feature 2\non 1\nmode 2\n", 0, 1, 15 },
        { "no header", "on 1\nmode 2\n", 0, 1, 15 },
        { "out of range is the default, not a clamp", "feature 1\non 1\nmode 3\nlevel 21\n", 1, 1, 15 },
        { "below the int range", "feature 1\nlevel 9\n", 0, 1, 15 },
        { "negative", "feature 1\nmode -1\nlevel -15\n", 0, 1, 15 },
        { "20 digits", "feature 1\nmode 99999999999999999999\n", 0, 1, 15 },
        { "trailing garbage", "feature 1\non 1x\nmode 2x\nlevel 12 \n", 0, 1, 15 },
        { "on 2", "feature 1\non 2\n", 0, 1, 15 },
        { "no newline at the end", "feature 1\non 1\nmode 0", 1, 0, 15 },
        { "empty", "", 0, 1, 15 },
        { "lines without values", "feature 1\non\nmode\n\n\nlevel\n", 0, 1, 15 },
        { "90 repeated on lines", repeated, 1, 1, 15 },
        { "unknown keys skipped", "feature 1\nfuture 9\non 1\nlevel 11\n", 1, 1, 11 },
        { "CRLF (a Windows editor)", "feature 1\r\non 1\r\nmode 2\r\nlevel 13\r\n", 1, 2, 13 },
        { "383 B line", longLine, 1, 1, 15 },
        { "a NUL in the middle", std::string("feature 1\non 1\0mode 2\n", 22), 1, 1, 15 },
    };
    for (const Case& c : cases)
    {
        i = loadWith(c.text);
        char what[96];
        snprintf(what, sizeof what, "hostile file: %s", c.what);
        check(int(features::on(i)) == c.on && features::value(i, 0) == c.mode && features::value(i, 1) == c.level, what);
    }

    // ---- opaque state: hex, capped by stateMax (16) and kMaxState
    i = loadWith("feature 1\nstate 0102030405060708090a0b0c0d0e0f101112131415\n");
    check(s_stN == 16 && s_st[0] == 1 && s_st[15] == 16, "state: capped at stateMax");
    i = loadWith("feature 1\nstate " + std::string(6000, 'a') + "\n");
    check(s_stN == 16 && s_st[3] == 0xaa, "state: 6000 hex digits in a 384 B file, capped");
    i = loadWith("feature 1\nstate 01zz\n");
    check(s_stN == 1, "state: stops at the first non hex pair");
    s_stN = 3;
    s_st[0] = 0xde; s_st[1] = 0xad; s_st[2] = 0x01;
    features::set(i, true);
    features::flush();
    check(s_files["/Features/t.cfg"].find("state dead01\n") != std::string::npos, "state: saved as hex");

    // ---- restart flagged param and switch
    i = loadWith("feature 1\non 1\nboot 0\n");
    check(!features::restartPending(i) && strcmp(features::rowValue(i), "On") == 0, "restart: nothing pending at start");
    *features::valueRef(i, 2) = 1;
    features::changed();
    check(features::restartPending(i) && strcmp(features::rowValue(i), "Restart") == 0, "restart: a restart param change reads Restart");
    *features::valueRef(i, 2) = 0;
    features::changed();
    check(!features::restartPending(i), "restart: back to the start value, nothing pending");

    // ---- boot flag (USB audio): the session's state comes from running(), the file wins and is re-synced
    fresh();
    feat_desc bf = { sizeof(feat_desc), FEAT_ABI, 1, "bf", "Boot flag", "h", FEAT_F_RESTART | FEAT_F_BOOTFLAG, 0, nullptr, 0,
                     nullptr, apply, nullptr, nullptr, nullptr, running };
    i = features::add(&bf);
    s_running = 1;   // the EEPROM said on, the card file says off (Reset config, or the file was deleted)
    features::load(&kIo);
    check(s_apply == 1 && s_lastOn == 0, "boot flag: file off, session on: apply(0) re-syncs the flag at start");
    check(strcmp(features::rowValue(i), "Restart") == 0, "boot flag: the row reads Restart until the next start");
    s_ms += 5000;
    features::tick();
    check(s_writes == 0, "boot flag: the re-sync writes no file");
    fresh();
    i = features::add(&bf);
    s_running = 0;
    s_files["/Features/bf.cfg"] = "feature 1\non 1\n";
    features::load(&kIo);
    check(s_apply == 1 && s_lastOn == 1 && strcmp(features::rowValue(i), "Restart") == 0, "boot flag: file on, session off: apply(1), Restart");
    fresh();
    i = features::add(&bf);
    s_running = 1;
    s_files["/Features/bf.cfg"] = "feature 1\non 1\n";
    features::load(&kIo);
    check(s_apply == 0 && strcmp(features::rowValue(i), "On") == 0, "boot flag: file and session agree: no apply, On");

    // ---- hidden unless on (Developer mode without /.develop, D8)
    fresh();
    feat_desc h = make("h");
    i = features::add(&h);
    features::hideUnlessOn(i);
    features::load(&kIo);
    check(!features::listed(i), "hidden: off and hidden, not listed");
    fresh();
    i = features::add(&h);
    features::hideUnlessOn(i);
    s_files["/Features/h.cfg"] = "feature 1\non 1\n";
    features::load(&kIo);
    check(features::listed(i), "hidden: on, listed so it can be switched off");

    // ---- Reset config (D5)
    i = loadWith("feature 1\non 1\nmode 2\n");
    s_files["/Features/t.cfg.bak"] = "x";
    s_files["/Features/t.cfg.tmp"] = "x";
    s_apply = 0;
    features::resetAll();
    check(!features::on(i) && features::value(i, 0) == 1 && s_lastOn == 0 && s_apply >= 1, "reset: defaults applied");
    check(!cardExists("/Features/t.cfg") && !cardExists("/Features/t.cfg.bak") && !cardExists("/Features/t.cfg.tmp"),
          "reset: .cfg, .bak and .tmp removed");
    s_ms += 5000;
    features::tick();
    check(!cardExists("/Features/t.cfg"), "reset: no file written back after the reset");

    // ---- Restart now flushes first
    static int s_rn;
    static bool s_rnSaw;
    i = loadWith("");
    features::setRestartNow([] { s_rn++; s_rnSaw = cardExists("/Features/t.cfg"); });
    features::set(i, true);
    features::restartNow();
    check(s_rn == 1 && s_rnSaw, "restart now: the file is written before the power off starts");

    printf("%d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
