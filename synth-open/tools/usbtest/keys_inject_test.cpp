// Host unit test (NOT firmware) of the USB test console's input queue in the firmware's own Keys class
// (platforms/mk66/3rdparty/Buttons/Keys.cpp, compiled here with host stand-ins for the Teensy headers).
// The queue is consumed at the top of Keys::update with the lines a real key event runs, so the hold repeat and
// the Shift state (isButtonPressed) must behave as with a finger.
#include "Keys.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
void pinMode(uint8_t, uint8_t) {}
void attachInterrupt(uint8_t, void (*)(void), int) {}
void detachInterrupt(uint8_t) {}

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Event { char kind; int button; uint32_t at; bool shiftDown; };
static std::vector<Event> g_events;
static Keys* g_keys;

static void onPush(uint8_t b) { g_events.push_back({'P', b, g_ms, g_keys->isButtonPressed(29) != 0}); }
static void onRelease(uint8_t b) { g_events.push_back({'R', b, g_ms, g_keys->isButtonPressed(29) != 0}); }
static void onHold(uint8_t b) { g_events.push_back({'H', b, g_ms, g_keys->isButtonPressed(29) != 0}); }

static Keys* fresh()
{
    delete g_keys;
    g_keys = new Keys;
    g_keys->setOnPush(onPush);
    g_keys->setOnRelease(onRelease);
    g_keys->setOnHold(onHold);
    g_events.clear();
    g_ms = 1000;
    return g_keys;
}

int main()
{
    // press, release: callbacks run at the next update(), not at inject()
    Keys* k = fresh();
    CHECK(k->inject(23, true));
    CHECK(g_events.empty() && !k->isButtonPressed(23) && k->injectPending());
    k->update();
    CHECK(g_events.size() == 1 && g_events[0].kind == 'P' && g_events[0].button == 23 && k->isButtonPressed(23) && !k->injectPending());
    g_ms += 70;
    CHECK(k->inject(23, false));
    k->update();
    CHECK(g_events.size() == 2 && g_events[1].kind == 'R' && !k->isButtonPressed(23));

    // hold auto repeat: first after 333 ms, then the shrinking steps of holdTimeStepDiff (100, 190, 235, 255, 270 less)
    k = fresh();
    k->inject(30, true);
    k->update();
    uint32_t start = g_ms;
    std::vector<uint32_t> holds;
    for (int i = 0; i < 4000 && holds.size() < 8; i++)
    {
        g_ms++;
        size_t before = g_events.size();
        k->update();
        if (g_events.size() > before && g_events.back().kind == 'H') holds.push_back(g_events.back().at - start), start = g_events.back().at;
    }
    const uint32_t want[] = {334, 234, 144, 99, 79, 64, 64, 64};   // holdTime - step, +1 for the strict > in update()
    CHECK(holds.size() == 8);
    for (size_t i = 0; i < holds.size() && i < 8; i++) CHECK(holds[i] == want[i]);
    CHECK(k->isButtonHold(30));
    k->inject(30, false);
    k->update();
    size_t n = g_events.size();
    g_ms += 1000;
    k->update();
    CHECK(g_events.size() == n);   // no repeat after the release

    // Shift combination: Shift is already down when the second key is pushed
    k = fresh();
    k->inject(29, true);
    k->inject(12, true);
    k->inject(12, false);
    k->inject(29, false);
    k->update();
    CHECK(g_events.size() == 4);
    CHECK(g_events[0].kind == 'P' && g_events[0].button == 29);
    CHECK(g_events[1].kind == 'P' && g_events[1].button == 12 && g_events[1].shiftDown);
    CHECK(g_events[2].kind == 'R' && g_events[2].button == 12 && g_events[2].shiftDown);
    CHECK(g_events[3].kind == 'R' && g_events[3].button == 29 && g_events[3].shiftDown);   // Keys clears the flag after onRelease, as for a real event
    CHECK(!k->isButtonPressed(29));

    // capacity 16, order kept, a full queue refuses and loses nothing
    k = fresh();
    int accepted = 0;
    for (int i = 0; i < 20; i++) accepted += k->inject(uint8_t(i), true) ? 1 : 0;
    CHECK(accepted == 16);
    k->update();
    CHECK(g_events.size() == 16);
    for (int i = 0; i < 16; i++) CHECK(g_events[size_t(i)].button == i);
    CHECK(k->inject(40, true));   // room again

    // out of range ids and a release of a key that is not down
    k = fresh();
    CHECK(!k->inject(BUTTON_MAX, true) && !k->inject(255, true));
    CHECK(k->inject(5, false));
    k->update();
    CHECK(g_events.size() == 1 && g_events[0].kind == 'R' && !k->isButtonPressed(5));

    // the queue survives wrap around of its 8 bit counters
    k = fresh();
    for (int round = 0; round < 40; round++)
    {
        for (int i = 0; i < 7; i++) CHECK(k->inject(uint8_t(i), true));
        k->update();
        for (int i = 0; i < 7; i++) CHECK(k->inject(uint8_t(i), false));
        k->update();
    }
    CHECK(g_events.size() == 40 * 14);

    printf("keys_inject_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
