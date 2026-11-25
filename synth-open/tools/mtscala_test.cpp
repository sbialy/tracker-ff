// Host test of the Scala parser (core/source/mtScala.h): good files and every reject of ref/microtuning-scope.md section 6. NOT firmware.
//   c++ -std=c++20 -O1 -Wall -Wextra -I core/source synth-open/tools/mtscala_test.cpp -o /tmp/mtscala_test && /tmp/mtscala_test
#include "mtScala.h"
#include "mtTuning.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_fail++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static scala::Result run(const std::string& t, uint32_t* deg)
{
    memset(deg, 0xAB, scala::kMaxNotes * sizeof(uint32_t));
    return scala::parse(t.data(), int(t.size()), deg);
}
static bool near(uint32_t a, double cents) { return std::abs(double(a) - cents * 1000.0) <= 1.0; }   // 1e-3 cent

int main()
{
    uint32_t d[scala::kMaxNotes];
    // 5-limit just major, ratios, a comment, a label after a value, CRLF
    {
        const char* t = "! just.scl\r\n!\r\n5-limit just major\r\n 7\r\n!\r\n 9/8\r\n 5/4  major third\r\n 4/3\r\n 3/2\r\n 5/3\r\n 15/8\r\n 2/1\r\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && r.n == 7 && r.periodMc == 1200000, "just major: err %d n %d period %u", r.err, r.n, r.periodMc);
        CHECK(d[0] == 0 && near(d[1], 203.91000) && near(d[2], 386.31371) && near(d[3], 498.04500) && near(d[4], 701.95500) && near(d[5], 884.35871) && near(d[6], 1088.26871),
              "just major cents %u %u %u %u %u %u", d[1], d[2], d[3], d[4], d[5], d[6]);
    }
    // cents with a dot, a whole number as a ratio, a mix, the last line a non octave period (Bohlen-Pierce 13 over 3/1 as cents)
    {
        std::string t = "BP 13ED3\n13\n";
        for (int i = 1; i <= 12; i++) { char b[32]; snprintf(b, sizeof(b), "%.3f\n", 1901.955 * i / 13); t += b; }
        t += "3/1\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && r.n == 13 && std::abs(double(r.periodMc) - 1901955.0) <= 1.0, "BP: err %d n %d period %u", r.err, r.n, r.periodMc);
        CHECK(near(d[1], 146.304) && near(d[12], 1755.651), "BP degrees %u %u", d[1], d[12]);
    }
    {   // a whole number is a ratio, a decimal point is cents, both in one file, the empty description line
        const char* t = "\n3\n 100.\n 3/2\n 2\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && r.n == 3 && near(d[1], 100.0) && near(d[2], 701.955) && r.periodMc == 1200000, "mixed: err %d n %d", r.err, r.n);
    }
    {   // N = 1: Carlos alpha, a single period line
        const char* t = "Carlos alpha\n1\n78.0\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && r.n == 1 && r.periodMc == 78000 && d[0] == 0, "carlos: err %d n %d period %u", r.err, r.n, r.periodMc);
    }
    {   // 128 notes: the largest file the table takes (every degree 0.5 cent, period 64 cents), lines after the values are ignored
        std::string t = "128\n128\n";
        for (int i = 1; i <= 128; i++) { char b[32]; snprintf(b, sizeof(b), "%d.%d\n", i / 2, (i % 2) * 5); t += b; }
        t += "this line is ignored\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && r.n == 128 && r.periodMc == 64000 && near(d[127], 63.5), "128 notes: err %d n %d period %u", r.err, r.n, r.periodMc);
    }
    {   // the 4th decimal rounds, a plus sign, a ratio of 7 digit parts
        const char* t = "x\n3\n+100.0005\n 1999999/1000000\n 2/1\n";
        const scala::Result r = run(t, d);
        CHECK(r.err == scala::kOk && d[1] == 100001 && near(d[2], 1200.0 * std::log2(1.999999)), "round: err %d d1 %u d2 %u", r.err, d[1], d[2]);
    }
    // ---- the rejects
    auto expect = [&](const char* name, const std::string& t, scala::Err e, int line = 0, int n = 0) {
        const scala::Result r = run(t, d);
        char msg[64];
        scala::errText(r, msg, sizeof(msg));
        CHECK(r.err == e && (line == 0 || r.line == line) && (n == 0 || r.n == n), "%s: err %d (want %d) line %d n %d msg '%s'", name, r.err, e, r.line, r.n, msg);
    };
    expect("empty file", "", scala::kNotScala);
    expect("only a comment", "! nothing\n", scala::kNotScala);
    expect("description only", "just a title\n", scala::kNotScala);
    expect("count is text", "title\nseven\n1/1\n", scala::kNotScala);
    expect("count 0", "title\n0\n", scala::kNotScala);
    expect("count with 10 digits", "title\n1000000000\n2/1\n", scala::kNotScala);
    expect("a NUL byte", std::string("title\n2\n3/2\n2/1\0\n", 17), scala::kNotScala);
    expect("a binary byte", "title\n2\n3/2\n2/1\x01\n", scala::kNotScala);
    expect("fewer lines than the count", "title\n3\n9/8\n2/1\n", scala::kNotScala);
    expect("129 notes", "title\n129\n2/1\n", scala::kTooMany, 0, 129);
    expect("1000 notes", "title\n1000\n2/1\n", scala::kTooMany, 0, 1000);
    expect("ratio zero denominator", "t\n2\n3/0\n2/1\n", scala::kBadValue, 3);
    expect("ratio zero numerator", "t\n2\n0/3\n2/1\n", scala::kBadValue, 3);
    expect("ratio 10 digits", "t\n2\n1000000000/3\n2/1\n", scala::kBadValue, 3);
    expect("negative ratio", "t\n2\n-3/2\n2/1\n", scala::kBadValue, 3);
    expect("ratio with a dot", "t\n2\n3.5/2\n2/1\n", scala::kBadValue, 3);
    expect("not a number", "t\n2\n3/2\nfoo\n", scala::kBadValue, 4);
    expect("an empty value line", "t\n2\n3/2\n\n2/1\n", scala::kBadValue, 4);
    expect("cents beyond 9600", "t\n2\n9600.001\n9700.0\n", scala::kBadValue, 3);
    expect("a bare dot", "t\n2\n.\n2/1\n", scala::kBadValue, 3);
    expect("two dots", "t\n2\n1.2.3\n2/1\n", scala::kBadValue, 3);
    expect("whole number zero", "t\n2\n0\n2/1\n", scala::kBadValue, 3);
    expect("line numbers count comments", "! a\n! b\nt\n!c\n2\n3/2\nx\n", scala::kBadValue, 7);
    expect("degrees descend", "t\n3\n3/2\n5/4\n2/1\n", scala::kNotAscending, 4);
    expect("a degree repeats", "t\n3\n3/2\n3/2\n2/1\n", scala::kNotAscending, 4);
    expect("the period is not above the last degree", "t\n2\n1250.0\n2/1\n", scala::kNotAscending, 4);
    expect("a degree at the unison", "t\n2\n1/1\n2/1\n", scala::kNotAscending, 3);
    expect("a degree below the unison", "t\n2\n-100.0\n2/1\n", scala::kNotAscending, 3);
    expect("period under 10 cents", "t\n1\n9.999\n", scala::kPeriodRange);
    expect("period over 4800 cents", "t\n1\n4800.001\n", scala::kPeriodRange);
    expect("period 4800 is fine", "t\n1\n4800.0\n", scala::kOk);
    expect("period 10 is fine", "t\n1\n10.0\n", scala::kOk);
    {   // > 8 KB
        std::string t = "t\n1\n2/1\n";
        while (t.size() <= 8192) t += "! padding padding padding padding padding padding padding\n";
        expect("larger than 8 KB", t, scala::kTooLong);
        t.resize(8192);
        expect("exactly 8 KB is read", t, scala::kOk);
    }
    {   // the messages
        scala::Result r = {scala::kTooMany, 200, 0, 0};
        char m[64];
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Scale has 200 notes, max 128"), "msg many '%s'", m);
        r = {scala::kBadValue, 0, 17, 0};
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Bad value on line 17"), "msg bad '%s'", m);
        r = {scala::kNotAscending, 0, 0, 0};
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Scale must ascend"), "msg asc '%s'", m);
        r = {scala::kPeriodRange, 0, 0, 0};
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Period out of range"), "msg period '%s'", m);
        r = {scala::kTooLong, 0, 0, 0};
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Scala file too long"), "msg long '%s'", m);
        r = {scala::kNotScala, 0, 0, 0};
        scala::errText(r, m, sizeof(m));
        CHECK(!strcmp(m, "Not a Scala file"), "msg not '%s'", m);
    }
    // ---- the tuning model over a parsed scale: key = degree, whole periods stack, the 1/1 key and Hz as the EDO
    {
        const char* t = "just\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n";
        const scala::Result r = run(t, tuning::degMc);
        tuning::reset();
        tuning::set(r.n, r.periodMc, 60, tuning::kDefHzE4);
        tuning::cfg.file = 1;
        CHECK(!tuning::isDefault() && tuning::tunedNames(), "a file tuning is not the default, 7 notes name degrees");
        CHECK(std::abs(tuning::rel(62) * 100.0 - 701.955 * 0 - 1200.0 * std::log2(5.0 / 4.0)) < 0.01, "key 62 = 5/4 over key 60 (%f)", tuning::rel(62) * 100.0);
        CHECK(std::abs(tuning::rel(67) * 100.0 - 1200.0) < 0.01, "7 keys up = the period (%f)", tuning::rel(67) * 100.0);
        CHECK(std::abs(tuning::rel(59) * 100.0 - (1200.0 * std::log2(15.0 / 8.0) - 1200.0)) < 0.01, "key 59 = 15/8 an octave down (%f)", tuning::rel(59) * 100.0);
        tuning::set(7, 1200000, 60, 2640000);
        CHECK(std::abs(tuning::rel(60) * 100.0 - 1200.0 * std::log2(264.0 / 261.6256)) < 0.01, "1/1 Hz 264 moves the 1/1 key (%f)", tuning::rel(60) * 100.0);
        tuning::reset();
        CHECK(tuning::isDefault() && tuning::rel(70) == 10.0f, "reset is the default and k - 60");
    }
    printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
