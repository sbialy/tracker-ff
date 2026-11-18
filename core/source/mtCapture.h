#pragma once
// Capture (Ableton Move style, tracker-ff): the ring of the notes played in the green mode of the pattern editor. Sequencer::captureTap
// fills it (pads in the main loop, MIDI in the audio callback), Sequencer::captureCommit puts the last pass of the pattern from it into the copy buffer
// (mtSequencer_capture.cpp). The ring lives in SDRAM (__NOINIT, no constructor), its head and count in SRAM.
#include <stdint.h>

constexpr uint16_t kCaptureRing = 512;                 // events, 12 B each (6,144 B of SDRAM)
constexpr uint16_t kCaptureReadMax = kCaptureRing - 8; // the reader stays this far behind the head: a push during a commit never tears an event it reads

constexpr uint8_t kCapOn = 0x80;        // CaptureEvent::vel bit 7: note on (clear: note off)
constexpr uint8_t kCapFlagPads = 1;     // CaptureEvent::flags: the event came from the pads (else MIDI in)
constexpr uint8_t kCapFlagRunning = 2;  // the transport ran at the event

struct CaptureEvent
{
    uint32_t tUs;       // Sequencer::seqClockUs, only differences are used
    uint8_t note;       // 0..127
    uint8_t vel;        // bit 7 kCapOn, 0..127 velocity (127 when the record options do not take the velocity, as the stock record)
    uint8_t pos;        // player.globalPos at the event
    uint8_t uStep;      // player.uStep at the event, 1..48 (0 before the first tick)
    uint8_t instrument; // mtProject.values.lastUsedInstrument
    uint8_t pattern;    // mtProject.values.actualPattern
    uint8_t flags;
    uint8_t spare;
};
static_assert(sizeof(CaptureEvent) == 12, "the capture ring is 512 x 12 B");

// Config > Features > Capture (features_builtin.cpp): off by default. Off: the tap does nothing and the button 3 of the pattern editor is the stock one.
extern bool captureFeatureOn;
// Config > Features > Capture > Idle time: the ring is emptied when no note arrived for this many seconds (the idle gap of Ableton Move's Capture)
constexpr uint8_t kCaptureIdleDefault = 20;
constexpr uint8_t kCaptureIdleMin = 5;
constexpr uint8_t kCaptureIdleMax = 60;
extern uint8_t captureIdleSec;

// The always visible indicator in the title bar (Sequencer::captureIndicator, drawn by TitleBar::setCaptureState)
enum CaptureIndicator : uint8_t
{
    kCapIndHidden = 0, // the feature is off
    kCapIndEmpty = 1,  // on, nothing in the ring: an empty (hollow) square
    kCapIndStale = 2,  // on, notes in the ring that a Capture would not take now (older than one pass, transport stopped, other pattern): dim filled
    kCapIndReady = 3,  // on, a Capture would take notes now: filled in the green of the theme
};

// The ring, for the host test (ui_shot "capring")
uint16_t captureRingCount();
bool captureRingGet(uint16_t back, CaptureEvent* out); // back 0 = the newest
void captureRingClear();
