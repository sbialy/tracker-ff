#ifndef _MTSEQUENCER_H_
#define _MTSEQUENCER_H_

#include "Platform.h"
#include "internalTickClock.h"
#include "mtChordDefs.h"
#include "mtMidi.h"
#include "mtStructs.h"
#include "scales.h"
#include "seqRandom.h"
#include "util_math.h"
#include <stdint.h>

namespace fx34
{
struct Set;
}

static void nullCallback(void){};

bool isSampleInstrument(int instrument);
bool isMidiInstrument(int instrument);
bool isSynthInstrument(int instrument);
int instrumentIdxToMidiChannel(int instrument);
int instrumentIdxToSynthIdx(int instrumentIdx);
bool isSampleTrack(int track);
bool isPerformanceTrack(int track);

static constexpr int kNoChannel = -1;

class Sequencer
{

  public:
    Sequencer();

    static constexpr int TRACKS = 8; // todo: use kAllTracks in futore or other in mtStructs
    typedef float seqtimesec_t;
    static constexpr float kUnknownExternalTempo = 0.0f;
    static constexpr seqtimesec_t kSecond = 1;
    static constexpr seqtimesec_t kMinute = 60 * kSecond;

    static constexpr int kStepFxSlots = 2;
    // FX slots the player plays a step with: the 2 of the pattern file and, with the 4 FX project switch (fx34.h), 2 more of the
    // per pattern sidecar. Rank 0 is the most significant, ranks 2 and 3 form a second pair (rank ^ 1 is the other slot of a pair)
    static constexpr int kSeqFxSlots = 4;

    enum enStep
    {
        STEP_NOTE_EMPTY = -1,
        STEP_NOTE_OFF_FADE = -2,
        STEP_NOTE_OFF_CUT = -3,
        STEP_NOTE_OFF_DEFAULT = -4,
        STEP_NOTE_DEFAULT = 60,
        STEP_NOTE_MAX = 127,
        STEP_NOTE_MIN = 0,

        STEP_VELO_MIN = 0,
        STEP_VELO_MAX = 127,
        STEP_VELO_DEFAULT = -1,
    };
    enum enFillStep
    {

        fillStepNote = 0,
        fillStepNoNote,
        fillStepFx,
        fillStepNoFx,
        fillStepRandom,
        fillStepByStep,
        fillStepEuclidean,
        fillStepEnd__
    };
    static constexpr int MAX_FILL_DENSITY = 128;

    enum enMisc
    {
        MINSTEP = 0,
        MAXSTEP = 127,
        DEFAULT_PATTERN_LENGTH = 32,

    };

    enum MuteState
    {
        kUnmuted,
        kMuted,
    };

    enum enPlaymode
    {
        PLAYMODE_MIN = 0,
        PLAYMODE_FORWARD = 0,
        PLAYMODE_BACKWARD,
        PLAYMODE_RANDOM,
        PLAYMODE_CUSTOM_1,
        PLAYMODE_CUSTOM_2,
        PLAYMODE_CUSTOM_3,
        PLAYMODE_CUSTOM_4,
        PLAYMODE_CUSTOM_5,
        PLAYMODE_CUSTOM_6,
        PLAYMODE_CUSTOM_7,
        PLAYMODE_CUSTOM_8,
        PLAYMODE_CUSTOM_9,
        PLAYMODE_CUSTOM_10,
        PLAYMODE_CUSTOM_11,
        PLAYMODE_CUSTOM_12,
        PLAYMODE_CUSTOM_13,
        PLAYMODE_CUSTOM_14,
        PLAYMODE_CUSTOM_15,
        PLAYMODE_CUSTOM_16,
        PLAYMODE_CUSTOM_17,
        PLAYMODE_CUSTOM_18,
        PLAYMODE_CUSTOM_19,
        PLAYMODE_CUSTOM_20,
        PLAYMODE_PINGPONG
    };

    enum enMidiInVoiceMode
    {
        midiInVoiceMode_sequencer,
        midiInVoiceMode_ignore,
        midiInVoiceMode_SamplePlayback,
        midiInVoiceMode_SampleEditor,
        midiInVoiceMode_SampleRecorder,
        midiInVoiceMode_SynthPlay, // the Synth play page: MIDI in plays one synth part, no song (synthPlayRouter.h)
        midiInVoiceMode_default = midiInVoiceMode_sequencer,
    };

    static const float MAX_TEMPO;
    static const float MIN_TEMPO;

    static struct strFxConsts
    {
        enum enFxType
        {
            // powiązane z listą tekstów w
            //			 #include <interfaceDefs.h>
            FX_TYPE_NONE,
            FX_TYPE_OFF,
            FX_TYPE_MICROMOVE,
            FX_TYPE_ROLL,
            FX_TYPE_CHANCE,
            FX_TYPE_RANDOM_NOTE,
            FX_TYPE_RANDOM_INSTRUMENT,
            FX_TYPE_RANDOM_VOLUME,
            FX_TYPE_SEND_CC_A,
            FX_TYPE_SEND_CC_B,
            FX_TYPE_SEND_CC_C,
            FX_TYPE_SEND_CC_D,
            FX_TYPE_SEND_CC_E,
            FX_TYPE_BREAK_PATTERN, // dziala jak koniec patternu
            FX_TYPE_MIDI_CHORD,
            FX_TYPE_TEMPO,
            FX_TYPE_RANDOM_VALUE,

            FX_TYPE_SWING,
            FX_TYPE_VOLUME,
            FX_TYPE_GLIDE,
            FX_TYPE_GATE_LENGTH,
            FX_TYPE_ARP,
            FX_TYPE_POSITION,
            FX_TYPE_VOLUME_LFO,
            FX_TYPE_PANNING_LFO,
            FX_TYPE_SAMPLE_SLICE,
            FX_TYPE_REVERSE_PLAYBACK,
            FX_TYPE_FILTER_LOWPASS,
            FX_TYPE_FILTER_HIGHPASS,
            FX_TYPE_FILTER_BANDPASS,
            FX_TYPE_DELAY_SEND,
            FX_TYPE_PANNING,
            FX_TYPE_REVERB_SEND,
            FX_TYPE_FINETUNE_LFO,
            FX_TYPE_MICROTUNING,
            FX_TYPE_FILTER_LFO,
            FX_TYPE_POSITION_LFO,
            FX_TYPE_SEND_CC_F,
            FX_TYPE_OVERDRIVE,
            FX_TYPE_BIT_DEPTH,
            FX_TYPE_TUNE,
            FX_TYPE_SLIDE_UP,
            FX_TYPE_SLIDE_DOWN,
            FX_TYPE_R30,
            FX_TYPE_AMP_ENV = FX_TYPE_R30,
            FX_TYPE_R31,
            FX_TYPE_FILTER_ENV = FX_TYPE_R31,

            // FX_TYPE_Rx to pozostałość po starych efektach i zostają tu żeby nic się nie rozpadło
            FX_TYPE_R1,
            FX_TYPE_GRANULAR_LENGTH = FX_TYPE_R1,
            FX_TYPE_R2,
            FX_TYPE_PATCH_CHANGE = FX_TYPE_R2, // SYNTH_OPEN: one-shot synth patch change (bank slot)
            FX_TYPE_R3,
            FX_TYPE_STRUM = FX_TYPE_R3, // tracker-ff: strum of the MIDI Chord in the other slot (1..63 up, 65..127 down, 2 ms a unit)
            FX_TYPE_R7,
            FX_TYPE_COND = FX_TYPE_R7, // tracker-ff: Chance FX, the Chance value gates only the other slot's FX, the note always plays
            FX_TYPE_R8,
            FX_TYPE_PATCH_MORPH = FX_TYPE_R8, // SYNTH_OPEN: blend of the open patch slot and a bank slot, 0..100 %
            FX_TYPE_R9,
            FX_TYPE_TAPE = FX_TYPE_R9, // tracker-ff: Tape, value 1..64 = the voices of the track slow to a stop (run-down of v quarters of a step), 65..128 = spin up from standstill (run-up of v - 64 quarters), see tapeFx.h
            FX_TYPE_R10, // RESERVED HOLE, never reuse: the former Tape Start (dev projects), migrated on load to Tape (id 50) with the value + 64 (tapeFx.h)
            FX_TYPE_R11, // rel140-snapfx: id 52, older firmware shows it as no FX
            FX_TYPE_SNAP = FX_TYPE_R11, // tracker-ff: latching scale snap of the track from this step (value 0 Off, 1 Nearest, 2 Down, 3 Up, 4 Track = back to the project setting)

        };

        enum enRollPeriod
        {
            ROLL_PERIOD_NONE = 0,
            ROLL_PERIOD_MIN = 0,

            ROLL_PERIOD_1_1,
            ROLL_PERIOD_1_2,
            ROLL_PERIOD_1_3,
            ROLL_PERIOD_1_4,
            ROLL_PERIOD_1_6,
            ROLL_PERIOD_1_8,
            ROLL_PERIOD_1_12,
            ROLL_PERIOD_1_16,
            ROLL_PERIOD_MAX = ROLL_PERIOD_1_16
        };
        enum enStutterPeriod
        {
            STUTTER_PERIOD_NONE = 0,
            STUTTER_PERIOD_MIN = 0,
            STUTTER_PERIOD_16_1,
            STUTTER_PERIOD_12_1,
            STUTTER_PERIOD_8_1,
            STUTTER_PERIOD_6_1,
            STUTTER_PERIOD_4_1,
            STUTTER_PERIOD_3_1,
            STUTTER_PERIOD_2_1,
            STUTTER_PERIOD_1_1,
            STUTTER_PERIOD_1_2,
            STUTTER_PERIOD_1_3,
            STUTTER_PERIOD_1_4,
            STUTTER_PERIOD_1_6,
            STUTTER_PERIOD_1_8,
            STUTTER_PERIOD_1_12,
            STUTTER_PERIOD_1_16,
            STUTTER_PERIOD_MAX = STUTTER_PERIOD_1_16
        };

        enum enRollDir
        {
            rollType_const = 0,
            rollType_volDown,
            rollType_volUp,
            rollType_noteDown,
            rollType_noteUp,
            rollType_noteRandom,
            rollNoteDir_max = rollType_noteRandom,
        };

        enum enArpConsts
        {
            arpPeriod_none = -1,
            arpPeriod_8 = 0,
            arpPeriod_6,
            arpPeriod_4,
            arpPeriod_3,
            arpPeriod_2,
            arpPeriod_1,
            arpPeriod_1_2,
            arpPeriod_1_3,
            arpPeriod_1_4,
            arpPeriod_1_6,
            arpPeriod_1_8,

            arpPeriodsCount = 11,

            arpOption_up = 0,
            arpOption_down,
            arpOption_random,
        };

        enum enChanceConsts
        {
            CHANCE_PROB_MAX = 100,
            CHANCE_GROUP_WITH_LAST = 101,
            CHANCE_NOT_LAST = 102,
            CHANCE_PLAY_SKIP_START = 103,
            CHANCE_CYCLE_STEPS = 6,
            CHANCE_CYCLE_COUNT = CHANCE_CYCLE_STEPS * CHANCE_CYCLE_STEPS,
            CHANCE_PLAY_SKIP_END = CHANCE_PLAY_SKIP_START + CHANCE_CYCLE_COUNT - 1,
            CHANCE_SKIP_PLAY_START = CHANCE_PLAY_SKIP_END + 1,
            CHANCE_SKIP_PLAY_END = CHANCE_SKIP_PLAY_START + CHANCE_CYCLE_COUNT - 1,
            // long pass conditions (tracker-ff), counted per track like the cycles above (passCount % 8 or % 16)
            CHANCE_PASS8_START = CHANCE_SKIP_PLAY_END + 1, // 175..182: pass k of 8 (k = 1..8, 8 is every 8th pass)
            CHANCE_PASS8_END = CHANCE_PASS8_START + 7,
            CHANCE_PASS16_START = CHANCE_PASS8_END + 1,    // 183..198: pass k of 16 (16 is every 16th pass)
            CHANCE_PASS16_END = CHANCE_PASS16_START + 15,
            CHANCE_MAX = CHANCE_PASS16_END,
        };

    } fx;

    enum enRollVolumeOption
    {
        rollVolumeOption_const = 0,
        rollVolumeOption_up,
        rollVolumeOption_down,
        rollVolumeOption_random,
    };

    struct strPattern
    {
        struct strFileHeader
        {
            char id_file[2];   // { 0 }; // typ pliku, np MT = plik projektu, TI = instrument
            uint16_t type;     // typ w liczbie
            char fwVersion[4]; // { 0 }; 		// wersja fw w ktorej zapisano plik
            char fileStructureVersion[4]; // { 0 };	// wersja struktury pliku
            uint16_t size;                // = 0;					// rozmiar
        } header;

        float tempoRezerwa; // = 100.0;
        float swingRez;     // = 50.0;

        uint8_t rezerwa1; // = 0;
        uint8_t rezerwa2; // = 0;
        uint8_t rezerwa3; // = 0;
        uint8_t rezerwa4; // = 0;

        struct strTrack
        {
            uint8_t length; // = DEFAULT_PATTERN_LENGTH - 1; // liczy od 0

            struct strStep
            {
                int8_t note;        // = STEP_NOTE_EMPTY;
                uint8_t instrument; // = 0;
                // FX
                struct strFx
                {
                    uint8_t type;  // = 0;
                    uint8_t value; // = 0;
                } fx[kStepFxSlots];
                int8_t volume;    // = STEP_VELO_DEFAULT (-1);
                int8_t micromove; // = -1;

                bool isNoteEmpty()
                {
                    return note == STEP_NOTE_EMPTY || note < STEP_NOTE_OFF_DEFAULT;
                }
                bool isNoteOff()
                {
                    return note == STEP_NOTE_OFF_FADE || note == STEP_NOTE_OFF_DEFAULT ||
                           note == STEP_NOTE_OFF_CUT;
                }
                bool hasAnyFx()
                {
                    return fx[0].type != 0 || fx[1].type != 0 || volume >= 0 || micromove >= 0;
                }

            } step[MAXSTEP + 1];

        } track[kAllTracks];

        uint32_t crc;
    };

    struct strSelection
    {
        uint8_t firstStep;  // = 0;
        uint8_t firstTrack; // = 0;
        uint8_t lastStep;   // = 0;
        uint8_t lastTrack;  // = 0;
    };

    // 16 tracks
    static_assert(sizeof(strPattern) == k16TracksPatternSize);

    struct strPlayer
    {
        bool songMode = 0;
        bool performanceMode = 0;
        // Performance put tracks of other patterns into the playing pattern (bit per track): it is not the pattern on screen
        // any more, so it must never be saved under that pattern's number (cleared when a whole pattern is loaded)
        uint16_t perfMixedTracks = 0;
        bool selectionMode = 0;
        bool selectionModeLooped = 0;
        strSelection loopSelection;
        uint8_t selectionModeLastStep = 0;

        uint16_t sequencialSwitch_pattern = 0;
        bool sequencialSwitch_isArmed = 0;
        bool sequencialSwitch_loaded = 0;
        // Set by stop() in the audio callback, carried out by update().
        bool reloadPatternAfterStop = 0;
        int activePatternIdx = -1;

        bool isPlay = 0;
        bool isREC = 0;
        bool startRecOnExtClk = 0;
        bool isStop = 1;

        uint16_t uStep = 0;
        uint16_t globalPos = 0;

        bool breakPattern = 0; // stops pattern after current step
        bool endOfPattern = false;

        uint8_t midiInVoiceMode = midiInVoiceMode_sequencer;

        // Per-track MIDI in: last instrument that sounded on each sample track
        // (-1 = none yet) and the note currently held there by MIDI in.
        int8_t trackLastInstrument[kSampleTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 };
        int8_t trackMidiInNote[kSampleTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 };

        struct strPerformance
        {
            int16_t patternLength = -1;
            int16_t trackLength[kAllTracks];
        } performance;

        struct strTempoFx
        {
            bool isActive = 0;
            bool stopAfterCurrentStep = 0;

            float tempo = 0.0;

            void set(int16_t val)
            {
                isActive = 1;
                tempo = val;
                tempo = clamp(tempo, MIN_TEMPO_FX, MAX_TEMPO);
            }
            void reset()
            {
                isActive = 0;
                stopAfterCurrentStep = 0;
            }

        } tempoFx;

        struct strSwing
        {
            bool isActive = 0;
            bool stepToogle = 0;
            float value = 50.0f;

            void toogle() { stepToogle = !stepToogle; }
            float getTempoCorrection()
            {
                return stepToogle ? (100.0f - value) + 50.0f : value + 50.0f;
            }
            void setSwing(int16_t val)
            {
                isActive = 1;
                value = (float)clamp<int16_t>(val, 25, 75);
            }
            void resetSwing()
            {
                value = 50.0;
                isActive = 0;
                stepToogle = 0;
            }

        } swing;

        struct strPlayerTrack
        {
            struct strSendStep
            {
                int8_t note = STEP_NOTE_EMPTY;

                uint8_t instrument = 0;

                // FX
                struct strFx
                {
                    uint8_t type = 0;
                    uint8_t value = 0;

                } fx[kSeqFxSlots];

                int8_t volume = -1; // jeśli <0 to nie wysyłamy
                int8_t micromove = -1;
            } stepSent, stepToSend, stepContRoll;

            bool isActive = 1;
            bool isRecordingArmed = 1;

            bool stepOpen = 0;
            bool noteOpen = 0;
            uint8_t recOpen = 0;
            uint8_t recNoteOnStepIdx = 0;

            int16_t noteSource = -1;

            uint16_t uStep = 0;              // aktualny microstep
            int16_t actual_pos = 0;          // aktualna pozycja w stepach
            int16_t actual_pos_absolute = 0; // aktualna pozycja na potrzeby custom playMode
            int16_t custom_fx = 0;
            int16_t custom_fx_value = 0;

            uint16_t stepTimer = 0;  // tu liczymy microstepy w górę
            uint16_t stepLength = 0; // z tym porównujemy timer
            uint16_t noteTimer = 0;
            int16_t noteLength = -1; // nieskonczenie dluga jesli -1

            uint8_t cancelStep = 0;
            uint8_t snapFx = 0xFF; // the Snap FX latch of the track (a scale snap mode 0..3), 0xFF = none (sits in the padding before passCount)
            uint32_t passCount = 0;
            bool lastCondFired = true;

            struct strRollFx
            {
                uint8_t isOn = 0;
                uint8_t period = 0; // 1/1, 1/2, 1/4 ...
                uint8_t val = 0;
                uint8_t type = 0;
                uint8_t fxSlot = 0;
                uint8_t volumeOption = rollVolumeOption_const;
                int8_t startingVolume = -1;
                uint16_t length = 1; // obliczona długość na potrzeby vol up i down

                void reset()
                {
                    isOn = 0;
                    period = fx.ROLL_PERIOD_NONE;
                }
            } roll;

            struct strArpFx
            {
                uint8_t isOn = 0;
                int8_t period = -1;
                uint8_t option = 0;

                uint16_t timer = 0;
                uint16_t length = 0;

                uint8_t chord = 0;
                uint8_t baseNote = 36;

                // kontynuacja od 1,
                // zakladamy ze 0 poszlo jako pierwsze
                uint8_t nextPosInBitmask = 0;

                void start()
                {
                    isOn = 1;
                    timer = 0;

                    switch (option)
                    {
                        case fx.arpOption_down: nextPosInBitmask = 11; break;

                        default: nextPosInBitmask = 0; break;
                    }
                }
                void reset()
                {
                    isOn = 0;
                    period = -1;
                    option = 0;

                    timer = 0;
                }
            } arp;

            struct strTuneMidiFx
            {
                bool isActive = 0;
                int8_t tune = 0;

            } tuneMidi;

            struct strPerformanceMidiVolumeFx
            {
                static constexpr int minValue = -100;
                static constexpr int maxValue = 100;
                int value = 0;

            } performanceMidiVolumeFx;

            int8_t performanceStepRepeater = 0;
            int8_t performancePlayMode = 0;
            int performanceSourcePattern = -1;

            struct strMicroMoveFx
            {
                uint8_t isActive = 0;
                uint8_t isArmed = 0;
                uint16_t offsetValue = 0;
                uint16_t timer = 0;

                strSendStep stepToMove;

                void set(uint16_t val)
                {
                    offsetValue = val;
                    offsetValue = clamp<uint16_t>(val, 0, 2 * 48);
                    isActive = 1;
                    isArmed = 0;
                    timer = 0;
                }
                void reset()
                {
                    offsetValue = 0;
                    isActive = 0;
                    isArmed = 0;
                    timer = 0;
                }
                void arm(strSendStep stepToBuffer)
                {
                    if (isActive && !isArmed)
                    {
                        stepToMove = stepToBuffer;
                        isArmed = 1;
                    }
                }
                bool isMicromoveReady()
                {
                    if (isArmed && timer == offsetValue)
                        return true;
                    else
                        return false;
                }
            } micromove;

        } track[kAllTracks];

        void (*onPatternOrSelectionEnd)(void) = nullCallback;
        void (*onSongEnd)(void) = nullCallback;
        void (*onGlobalStep)(void) = nullCallback;

        int extRecMetronomeStep = 0;
        uint8_t preRecMetronomeActive = 0;
        void (*preRollMetronomeEndCalback)(void) = nullptr;
    };

    enum enSelectionElements
    {
        ELEMENTS_ALL_NO_PREFERENCES,
        ELEMENTS_ALL_WITH_PREFERENCES,
        ELEMENTS_NOTES,
        ELEMENTS_INSTRUMENTS,
        ELEMENTS_FX1,
        ELEMENTS_FX2,
        ELEMENTS_MICROMOVE,
        ELEMENTS_VOLUME,
        // the extra FX of the "4 FX" project switch (fx34.h). Ranks, not column names: rank 2 is FX4, rank 3 is FX3
        ELEMENTS_FX_RANK2,
        ELEMENTS_FX_RANK3,
    };
    enum enAltMidiChannel
    {
        GRID_OUTSIDE_PATTERN = 20,
        GRID_INSIDE_PATTERN = 21,
        GRID_INSIDE_PATTERN_INSTRUMENT = 22,
    };

    struct strSelection selection, copySelection, pasteSelection;

    void init();
    void update();
    void setCrcCalculator(CrcCalculator* crcCalculator);
    void initAfterReadConfig();
    void tick(float intervalSeconds);

    bool isPlay(void);

    // Seeded random takes (seqRandom.h): 0 = Off (every take differs, as before), 1..255 = the same decisions on every playback.
    // The project keeps it in its plugin sidecar (plug_host.cpp), the Config > Project Settings rows change it. A change counts from the
    // next Play.
    uint8_t randomSeed = 0;           // the Config > Project Settings "Random seed" row edits it in place
    uint8_t rerollRandomSeed(void);   // a new seed 1..255 other than the current one, returned
    bool isRec(void);

    uint8_t isRecArmedOnTrack(uint8_t track);
    void toogleRecArmedOnTrack(uint8_t track);
    int8_t getSingleArmedTrack();

    uint8_t isStop(void);
    bool isPlayingSelection(void);
    bool isPlayingSelectionLooped(void);
    bool isLoopSelectionSingleRow(void);
    uint8_t isRenderMode(void);

    float getCurrentTempo();
    float getExternalTempo() { return externalTempo; }
    uint32_t getSeqTimer();

    strPlayer const* ptrPlayer = &player;

    strPattern* getPatternToSaveToFile() { return currentPattern; }

    strPattern* getActualPattern() { return currentPattern; }
    strPattern* getBuffPattern() { return bufferPattern; }

    bool isPerformanceImageMixed() const { return player.perfMixedTracks != 0; }

    bool isPerformanceTrackChange(uint8_t track)
    {
        return player.track[track].performanceSourcePattern >= 0;
    }

    void saveToFileDone() {}

    void enterPerformanceMode();

    enum enSeqState
    {
        SEQ_STATE_STOP = 0,
        SEQ_STATE_PLAY_PATTERN,
        SEQ_STATE_PLAY_SONG,
        SEQ_STATE_PLAY_PERFORMANCE,
        SEQ_STATE_PLAY_SELECTION,
    };

    enSeqState getSeqState()
    {
        if (player.isStop)
            return SEQ_STATE_STOP;
        if (player.performanceMode)
            return SEQ_STATE_PLAY_PERFORMANCE;
        if (player.isPlay && player.selectionMode)
            return SEQ_STATE_PLAY_SELECTION;
        if (player.isPlay && !player.songMode)
            return SEQ_STATE_PLAY_PATTERN;
        if (player.isPlay && player.songMode)
            return SEQ_STATE_PLAY_SONG;

        return SEQ_STATE_STOP;
    }

    uint8_t getPatternLengthWithSel()
    {
        if (player.performanceMode)
        {
            if (player.performance.patternLength > -1)
            {
                return player.performance.patternLength;
            }
            else
            {
                return player.performance.trackLength[0];
            }
        }
        else if (player.selectionMode)
        {
            return player.selectionModeLastStep;
        }

        return currentPattern->track[0].length;
    }

    uint8_t isSongMode() { return player.songMode == 1; }

    bool isPerformanceMode() { return player.performanceMode; }

    void exitPerformanceMode()
    {
        player.performanceMode = 0;
        player.performance.patternLength = -1;
    }

    bool isPerformancePatternChanged()
    {
        if (!player.performanceMode)
            return false;

        for (uint8_t track = 0; track < kPerformanceTracks; track++)
        {
            if (mtProject.perfTracksPatterns[track] != mtProject.values.actualPattern)
            {
                return true;
            }
        }

        return false;
    }

    strPattern* getPatternToLoadFromFile() { return bufferPattern; }
    void clearAllPatternSlots();

    strPattern* getPatternToUI() { return currentPattern; }

    bool copyBufferToBufferPattern(uint8_t* srcBuff);
    bool copyCurrentPatternToBuff(uint8_t* srcBuff);

    void switchRamPatternsNow();
    // source: the pattern the buffer track came from, -1: the track's pending source (performanceSourcePattern)
    void switchPerformanceTrackNow(uint8_t trackToSwitch, int source = -1);
    void setTrackToLoadOnSwitch(uint8_t track, uint8_t sourcePattern);
    void setTrackToLoadNow(uint8_t track, uint8_t sourcePattern);
    void setPerformanceTrackLength(uint8_t track, uint8_t length);

    void setSynthMute(MuteState muteState);

    void sequencialSwitch_changeNextPattern(int8_t delta);
    void sequencialSwitch_SetReady();
    void sequencialSwitch_SetReadyNow();
    void sequencialSwitch_Reset();
    uint16_t sequencialSwitch_GetNext();

    void playPattern();
    void playPattern(uint8_t fromPos);
    void playSelection(bool looped = false);
    float getPlaySelectionProgress(void);
    void playSong(void);
#ifdef FEATURE_PARTS
    void switchToPartImage();             // parts.cpp: the buffer pattern (a Part image) becomes the playing pattern
    void playPartImage(bool songMode);    // play the Part image (songMode 0: loop it)
#endif
    void playSongFromPosition(uint8_t songPos);
    void recStart(void);
    void showPreRollPopup();
    bool startRecOnExtClk_Active(void);
    void startRecOnExtClk_Reset(void);
    void recEnable(bool enable);
    void stop(void);
    void stopManualNotes(void);
    void continueSong(uint8_t songPos);
    void loopCurrentPattern(uint8_t songPos);
    int16_t findSlotWithPattern();
    uint8_t isMetronomeActive();
    bool isMetronomeApplicableForActualRecordingSource(void);
    uint8_t isPreRollConfigOn();
    uint8_t getMetronomeNumerator();
    uint8_t getMetronomeDenominator();
    void setMidiInVoiceMode(enMidiInVoiceMode mode);
    uint8_t getMidiInVoiceMode();

    float getStepPeriodMs(void);
    float getTimerPeriod_us(void);
    void attachToTimer(void);
    void detachFromTimer(void);

    // SELECTION
    void insert(strSelection* selection);
    void insertReversed(strSelection* selection);
    bool backspace();

    void copySelectionToBuffer(strSelection* from, strSelection* to);
    void pasteSelectionFromBuffer(strSelection* from, strSelection* to, uint8_t elements);
    void copyToBuffer();
    void pasteFromBuffer(uint8_t);
    void copyStep(strPattern::strTrack::strStep* srcStep,
                  strPattern::strTrack::strStep* destStep,
                  bool toMidi,
                  int elements = ELEMENTS_ALL_NO_PREFERENCES);
    bool isSelectionCorrect(strSelection* selection);
    bool isSingleSelection(strSelection* selection);

    void setSelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    void setCopySelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    void setPasteSelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    uint8_t getCopySelectionHeight();
    int8_t getSelectionFirstNote();

    void clearSingleTrack(strPattern::strTrack* track);
    void clearPattern(strPattern* patt);
    void setPatternHeader(strPattern* patt);
    void clearStep(strPattern::strTrack::strStep*);
    void clearStep(strPattern::strTrack::strStep* step, uint8_t);
    void clearStepStored(strPattern::strTrack::strStep* step, uint8_t elements); // the step struct only, not the extra FX of fx34.h
    void clearSelected(strSelection* sel, uint8_t elements);
    void clearSelected();
    void clearSelected(uint8_t);
    void clearCopyPasteBuffer();

    void changeSelectionFxValue(uint8_t index, int16_t value);
    void changeSelectionFxType(uint8_t index, int16_t value);
    void changeSelectionNote(int16_t value);
    void changeSelectionNote(int16_t value, bool octaveShift);
    void changeSelectionInstrument(int16_t value);
    uint8_t changeInstrumentInSpecificOrder(int8_t actualValue, int16_t delta, bool midiTypeOnly);
    uint8_t getInstrumentVelo(uint8_t);
    uint8_t getInstrumentVolume(uint8_t);
    uint8_t getInstrumentMidiOffset(uint8_t instrument);

    void setSelectionFxType(uint8_t, int16_t);
    void setSelectionFxValueByPad(uint8_t index, int16_t value);
    void changeSelectionMicromove(int16_t value);
    void setSelectionMicromoveByPad(int16_t pad);
    void changeSelectionVolume(int16_t value);
    void setSelectionVolumeByPad(int16_t pad);
    void setSelectionInstrument(int16_t value);
    void setSelectionNote(int16_t value);

    void setStepRepeater(uint8_t track, int8_t stepRepeat);
    void setPerformancePlayMode(uint8_t track, int8_t playmode);

    void fillRandomNotes(int16_t, int16_t, int16_t, int16_t, int16_t);
    void fillLinearNotes(int16_t, int16_t, int16_t, int16_t, int16_t);
    // Fill Type Role: a drum, bass or lead idea over the selection (mtPatternGen.h), role is patternGen::Role, density 1..MAX_FILL_DENSITY
    void fillRoleNotes(int16_t role, int16_t scale, int16_t density);
    void fillRandomInstruments(int16_t step, int16_t from, int16_t to, int16_t density);
    void fillLinearInstruments(int16_t step, int16_t from, int16_t to, int16_t density);
    void fillLinearFx(int16_t index,
                      int16_t fillStep,
                      int16_t fxType,
                      int16_t fromVal,
                      int16_t toVal,
                      int16_t density);
    void fillRandomFx(int16_t index,
                      int16_t fillStep,
                      int16_t fxType,
                      int16_t fromVal,
                      int16_t toVal,
                      int16_t density);
    void fillLinearMicromove(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    void fillRandomMicromove(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    // Quantize: every selected step with a micromove moves toward the grid by strength percent (100 = on the grid, micromove cleared)
    void quantizeMicromove(int16_t fillStep, int16_t strength, int16_t density);
    // The per track groove, one signed value per track, -kGrooveMax..kGrooveMax, 0 = off (the default, plays as before). Positive
    // delays the odd steps (swing) by that percent of a step, negative pulls the recorded micromove toward the grid (-kGrooveMax
    // = on the grid). Playback only: the pattern keeps its micromoves. Not in the project struct: project.mtff (track.N.groove), written only when a
    // track has one (WorkspaceProjectSave, plug_host.cpp grooveSave)
    static constexpr int kGrooveMax = 50;
    int8_t getTrackGroove(uint8_t track) const { return track < kAllTracks ? trackGroove[track] : 0; }
    void setTrackGroove(uint8_t track, int v);
    void clearTrackGrooves() { memset(trackGroove, 0, sizeof(trackGroove)); }
    bool anyTrackGroove() const;
    void fillLinearVolume(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    void fillRandomVolume(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);

    bool isEuclideanStep(uint8_t offset, int16_t density);

    bool isStepToFillNote(strPattern::strTrack::strStep* step,
                          uint8_t offset,
                          int16_t fillStep,
                          int16_t density);
    bool isStepToFillFx(strPattern::strTrack::strStep* step,
                        uint8_t offset,
                        uint8_t fxIndex,
                        int16_t fillStep,
                        int16_t density);
    bool isStepToFillProp(strPattern::strTrack::strStep* step,
                          uint8_t offset,
                          bool isPropSet,
                          int16_t fillStep,
                          int16_t density);

    uint8_t generateRandomFx();
    void invertSelectedSteps(uint8_t);
    bool duplicatePattern();
    // D6 (4 FX): move the V (volume) and m (micro-move) FX of the steps that play a note into the volume and micro-move fields of the
    // step. Manual only. It is not bit exact: the groove (project.mtff) acts on the micro-move field only, and a step whose V or m FX
    // is the target of a Random value or Chance FX next to it is left alone. Steps without a note keep their FX (their V is a held
    // sequencer modulation, not a note velocity). Returns the number moved, `skipped` counts the ones left alone.
    uint16_t moveVmFxToFields(uint16_t* skipped);
    bool expandPattern();
    bool shrinkPattern();

    void setOnPatternOrSelectionEnd(void (*action)(void))
    {
        player.onPatternOrSelectionEnd = action;
    }
    void setOnSongEnd(void (*action)(void)) { player.onSongEnd = action; }
    void setOnGlobalStep(void (*action)(void)) { player.onGlobalStep = action; }

    void setPerformanceMidiTune(uint8_t tr, int8_t value);
    void setPerformanceMidiVolume(int track, int value);

    void setPerformancePatternLength(int length);
    void setPerformancePatternLengthFromFxVal(int8_t val);

    bool isSetInternalClock() { return (mtConfig.midi.clkIn == clockIn_Internal); }
    bool isFirstPatternStep(void);
    bool isLastPatternStep(void);

    void handleNoteOn(byte channel, byte pitch, int8_t volume, int16_t source);
    void handleNoteOff(byte channel, byte pitch, byte volume, int16_t source);

    // Chord capture (tracker-ff, mtSequencer_chord.cpp): a chord held on the pads or a MIDI keyboard in the pattern editor becomes the
    // cursor step's note (the lowest held note) and a MIDI Chord FX value (mtChord.h chordMatch). Pads: only with the cursor in an FX
    // column whose cell is MIDI Chord (or empty while MIDI Chord is the last used FX). MIDI: a note that comes while the first one
    // of a step entry is still held joins it, in any column. Only on MIDI and synth instrument steps, sample steps keep stock.
    enum enChordPad : uint8_t
    {
        chordPad_notMine = 0, // not a capture: the stock pad action
        chordPad_held,        // taken, the cursor stays
        chordPad_done,        // taken, the last held pad came up: the cursor moves on
    };
    enChordPad chordCapturePad(uint8_t fxIndex, uint8_t note, bool press);
    void chordCaptureMidiOn(uint8_t note, bool firstWritten);
    void chordCaptureMidiOff(uint8_t note);

    // Capture (Ableton Move style, mtSequencer_capture.cpp, Config > Features > Capture, off by default): the notes played in the green
    // mode of the pattern editor go into a ring (mtCapture.h), the under-screen button 3 puts the last pass of the pattern into the copy
    // buffer (the stock Copy/Paste one) with the stock live record rules, the pattern is not touched. captureTap is the one funnel:
    // handleNoteOn / handleNoteOff call it for pads and MIDI in.
    void captureTap(uint8_t channel, uint8_t note, uint8_t velocity, bool on, int16_t source);
    void captureCommit();   // main loop, a popup tells what was captured or why not
    uint8_t captureIndicator(); // main loop, every 60 ms: the idle time empties the ring, returns the CaptureIndicator state for the title bar
    void captureClear();        // long press of button 3: the ring is emptied, popup "Capture cleared"
    bool captureWindow(int& track, uint32_t& passUs); // the start track and the length of one pass in us (false: no usable track)

    // Strum FX (tracker-ff, FX_TYPE_STRUM): the value as signed units, + up (lowest note first), - down, 0 off (values 0 and 64).
    // A unit is kStrumUnitUs between two successive chord notes. The notes start on audio block boundaries (2,902 us on MK66),
    // the due time of each note is exact, so the error never adds up over a chord.
    static constexpr uint32_t kStrumUnitUs = 2000;
    static int8_t strumUnits(uint8_t v)
    {
        return (v > 0 && v < 64) ? int8_t(v) : (v > 64 && v <= 127) ? int8_t(64 - int(v)) : int8_t(0);
    }

    // Per-track MIDI in: play/stop a note directly on a track's voice using the
    // instrument that last sounded on that track.
    void handleTrackMidiNoteOn(uint8_t track, uint8_t note, uint8_t velocity);
    void handleTrackMidiNoteOff(uint8_t track);
    void updateTrackLengths(strPattern* pattern);

    // Per-track pattern length (polymeter, tracker-ff). Stock keeps every track at the length of track 1 (track[0].length, the
    // pattern length) and the player reads only that one. A pattern marks per-track lengths with kPolymeterMark in its reserved
    // header byte rezerwa1 (always 0 in stock files, clearPattern), so an unmarked pattern plays exactly as before even when an old
    // file holds stale lengths in tracks 9-16 (the stock length edit syncs only tracks 2-8). In a marked pattern
    //   - track[0].length stays the pattern length: song advance, pattern end, and what 1.3.0 plays and edits
    //   - track 1 keeps its own length in rezerwa2 (last step + 1, 0 = the pattern length)
    //   - tracks 2-16 keep their own length in track[t].length
    //   - rezerwa3 holds the pattern length at the last polymeter edit (last step + 2). A firmware without polymeter that changes
    //     the pattern length (1.3.0 syncs tracks 2-8 only) leaves it stale, the mark then no longer counts and the pattern plays as stock
    // Every track length is clamped to the pattern length. The mark is cleared again once every track is back at the pattern length.
    static constexpr uint8_t kPolymeterMark = 'P';
    static bool isPolymeter(const strPattern* p)
    {
        return p->rezerwa1 == kPolymeterMark && p->rezerwa3 == uint8_t(p->track[0].length + 2);
    }
    static void stampPolymeter(strPattern* p) { p->rezerwa3 = uint8_t(p->track[0].length + 2); }
    // the last step index (length - 1) track t plays in pattern p
    static uint8_t getTrackLastStep(const strPattern* p, int t)
    {
        const uint8_t last = p->track[0].length;
        if (!isPolymeter(p)) return last;
        const uint8_t own = t == 0 ? (p->rezerwa2 ? uint8_t(p->rezerwa2 - 1) : last) : p->track[t].length;
        return own < last ? own : last;
    }
    // set the length of track t (last step index), marks or unmarks the pattern. Returns true if anything changed
    bool setTrackLastStep(strPattern* p, int t, int last);
    // set the pattern length (last step index): tracks at the old pattern length follow it, shorter tracks keep theirs (clamped)
    void setPatternLastStep(strPattern* p, int last);
    // clamp every track to the pattern length, unmark the pattern when no track differs any more
    void normalizePolymeter(strPattern* p);
    // raw write of a track's own length (no clamp, no mark), callers normalize after
    static void setTrackOwnLastStep(strPattern* p, int t, int last);

    uint8_t noMoreRecOpen();

    // Snap FX (FX_TYPE_SNAP): the value is a scale snap mode (scales.h ScaleSnapMode), 0 Off .. 3 Up, or 4 Track: the latch is cleared and the
    // track plays its project setting again (mtff track.N.snap) from that step. A value past 4 reads as Track
    static constexpr uint8_t kSnapFxTrack = 4;
    static constexpr uint8_t kSnapFxMax = kSnapFxTrack;
    static constexpr uint8_t kSnapFxNone = 0xFF;
    int16_t getFxMax(uint8_t fxID, bool forRandom = false);
    // the two FX with the Chance value domain: Chance drops the whole step, Chance FX (FX_TYPE_COND) only the FX of the other slot
    static bool isChanceFamily(uint8_t fxType)
    {
        return fxType == strFxConsts::FX_TYPE_CHANCE || fxType == strFxConsts::FX_TYPE_COND;
    }
    int16_t getFxMin(uint8_t fxID, bool forRandom = false);
    int16_t getFxDefault(uint8_t fxID);
    int16_t getFxValueToView(uint8_t fxID, uint8_t track, uint8_t step);
    int16_t getFxValueCorrection(uint8_t type, uint8_t value);
    char getRollTypeChar(uint8_t val);
    uint8_t calcStepLength(uint8_t track, uint8_t step);
    int16_t rollValueToPeriod(int16_t value);
    void makeFxValLabelForSeqStep(char* ptr, int len, uint8_t fxID, uint8_t track, uint8_t step);
    enum enFxValLabelAlign
    {
        fxValLabelAlign_right = 0,
        fxValLabelAlign_center,
    };
    void makeFxValLabelWithAlgin(char* ptr,
                                 int len,
                                 uint8_t fxType,
                                 uint8_t value,
                                 enFxValLabelAlign align);

    uint8_t getActualPos() { return player.track[0].actual_pos; }
    uint8_t getPatternLength() { return getActualPattern()->track[0].length + 1; }

    void alignToGlobalPos(uint8_t);

    void setExternalClockComp(uint8_t configValue);

    void handleMidiInClock();
    void handleMidiInStart(bool continuePlay = false, bool songMode = false);
    void handleMidiInStop();
    void handleMidiInSongPosition(uint16_t beats);

    void setPreRollMetronomeEndCallback(void (*preRollMetronomeEndCalback)(void))
    {
        player.preRollMetronomeEndCalback = preRollMetronomeEndCalback;
    };

  private:
    struct strChordCapture
    {
        uint8_t notes[8];
        uint8_t count;   // held notes of the open group, 0 = no group
        uint8_t track;
        uint8_t step;
        uint8_t fromPads; // the group is the pads' (else MIDI's)
        int8_t fxIndex;   // pads: the FX slot under the cursor
        int8_t origNote;  // the step before the group (No chord puts it back)
        uint8_t origInstr;
        uint8_t origFx[2][2];
    } chordCap = {};
    void chordCaptureSnapshot(bool restore);
    uint32_t chordParaTracks = 0; // tracks whose MIDI Chord note-on went out as one paraphonic note (sendMidiChordOn / Off)
#ifdef FEATURE_SYNTH_OPEN
    // Note ids (polysynth::open::noteTag): every synth note the sequencer sends gets the next number (1..65535, 0 = none). noteId[track] is the
    // id of the track's latest note-on, the note-off that ends it carries the same id, so it releases the voice that note started and no other
    // voice of that pitch. The live paths (pad, MIDI in, record) send no id: they clear the track's id where they take the track over
    uint16_t noteSerial = 0;
    uint16_t noteId[kAllTracks] = {};
    uint16_t nextNoteId() { return noteSerial = (noteSerial == 0xFFFF) ? uint16_t(1) : uint16_t(noteSerial + 1); }
#endif

    // Strum FX: the delayed note-ons of strummed chords (MIDI out and Poly synth parts). Filled and served in the audio callback
    // (sendMidiChordOn, tick), a note-off of the track drops its pending notes, Stop drops all. Full: the note starts at once.
    struct strStrumNote
    {
        uint32_t dueUs;    // seqClockUs when it starts
        uint8_t track;
        uint8_t note;
        uint8_t velocity;
        uint8_t channel;   // 1..16, bit 7: synth part. 0: a free slot
#ifdef FEATURE_SYNTH_OPEN
        uint16_t id;       // the note id of the chord step (noteId[track] when it was queued), 0 for a MIDI out note
#endif
    };
    static constexpr uint8_t kStrumSlots = 12;
    strStrumNote strumQ[kStrumSlots] = {};
    uint8_t strumPending = 0;   // used slots
    uint32_t seqClockUs = 0;    // audio time, advanced by every tick (strum due times, the record join window)
    int8_t stepStrum(const strPlayer::strPlayerTrack::strSendStep* step);
    // the slot of the MIDI Chord FX that plays (not one with an Arp in the other slot of its pair), -1 for none
    static int8_t playingChordSlot(const strPlayer::strPlayerTrack::strSendStep* step);
    void strumQueue(uint8_t track, uint8_t note, uint8_t velocity, uint8_t channel, bool synth, uint32_t delayUs);
    void strumService();
    void strumCancel(int track);   // -1: every track

    // Record join: while recording on ONE armed track, a note that starts within
    // kRecJoinWindowUs of the group's previous note (kRecJoinSpanUs of its first), while the first note is still held, joins the
    // step as a MIDI Chord FX. The window slides with every joined note: the USB MIDI path of the unit spread notes sent 13 ms
    // apart over 40 to 70 ms (device test, J1), a 60 ms window from the first note lost them
    static constexpr uint32_t kRecJoinWindowUs = 100000;
    static constexpr uint32_t kRecJoinSpanUs = 300000;
    struct strRecJoin
    {
        uint8_t notes[8];
        uint8_t count;    // 0: no group
        uint8_t track;
        uint8_t step;
        uint8_t first;    // the group's first note (the one the stock path wrote and previews)
        uint32_t t0;      // seqClockUs of the first note
        uint32_t tLast;   // seqClockUs of the latest note that joined (or of the first)
        bool strumOwn;    // the Strum FX in the step is this group's roll (mtRoll.h), so the group may update or clear it
    } recJoin = {};
    void recJoinStart(uint8_t track, uint8_t step, uint8_t note);
    bool recJoinTry(uint8_t track, uint8_t note, uint8_t velocity);
    bool recJoinOff(uint8_t track, uint8_t note);
    // several tracks armed: the track under the cursor when it is armed records alone, as if it were the only armed one (-1: cursor track not armed or Instr pads, the stock spread)
    int8_t recJoinCursorTrack(bool padInstrumentEdit);
    void recHeldAdd(int16_t source, uint8_t note);
    bool recHeldRemove(int16_t source, uint8_t note);   // true: the note was in the list
    bool recGroupOff(uint8_t track, bool padInstrumentEdit);   // the end of a recorded group on a track, true: the pattern changed
    bool chordCaptureWrite();
    bool chordCaptureContext(int instrument);
    static int8_t chordSlot(strPattern::strTrack::strStep& st, int8_t prefer);   // the FX slot a chord goes to, -1: both busy
    static constexpr int kTicksInStep = 576;
    static constexpr size_t kExternalTempoCounterSize = 24 * 2;
    static constexpr int kExternalClockPpqn = 24;
    static constexpr int kStepsPerBeat = 4;

    static constexpr int kuStepsInStep = 48;
    static constexpr int kNanoStepsInUStep = 12;

    // efekty to bajty 0-255; żeby zmieścić >250 BPM liczone są co dwa.
    // np. wartość 5 to 10 BPM, wartość 200 to 400 BPM. Wartość 4 jest specjalna,
    // to TSTP.
    static const float TEMPO_FX_STP;
    static const float MIN_TEMPO_FX;
    static const float MAX_TEMPO_FX;

    void play(void);
    void play(uint8_t fromPos);

    void tick_uStep(void);

    void applyRowCustomFx(uint8_t x, uint8_t orderTemp);

    uint8_t isCustomPlayMode(uint8_t row);

    uint32_t timerTicks = 0;

    void switchRowStep(uint8_t row);
    void reset_actual_pos(void);
    void reset_actual_pos(uint8_t row, uint8_t delta = 0);
    void resetChanceState(void);
    bool evaluateChanceFx(uint8_t row, uint8_t value);

    void play_microStep(int row);
    // the FX3 / FX4 set of the playing pattern for this step of all tracks, nullptr unless the 4 FX switch is on and the set has entries
    const fx34::Set* playExtra = nullptr;
    void followChordUpdate(); // tracker-ff Follow chord (mtFollowChord.h): the chord of each chord track at its step start
    void inc_uStep(uint8_t row);
    void inc_uStepGlobal();

    void loadNextPattern(uint8_t patternNumber);

    uint8_t isTrackEngineMuted(uint8_t track);

    void stopAllOpenNotes(void);
    void stopOpenNoteOnTrack(int track, bool leaveStepOpen = false);

    void cancelFxes(int8_t track);
    void cancelFxes();
    void killFxOnSlot(uint8_t row, uint8_t slot);
    uint8_t getRollType(uint8_t value);
    uint8_t getRollVelo(uint8_t, const strPlayer::strPlayerTrack::strSendStep* const sendStep);

    uint8_t rollValToVolumeOption(uint8_t);
    int8_t rollValToPeriod(int8_t rollVal);
    inline uint16_t rollValToMicroSteps(int8_t rollType);
    inline uint16_t stutterValToPeriod(int8_t rollType);

    uint16_t arpPeriodToMicrosteps(int8_t period);

    void midiSendCC(uint8_t channel, uint8_t control, uint8_t value, uint8_t midiOut);

    void sendNoteOn(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);
#ifdef FEATURE_PLUG_HOST
    // Scale snap (scales.h): moves the note of the step in place, so the note off that follows names the note that was sent
    void snapStepNote(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step, uint8_t rollMode = kSnapOff);
    // Snap FX latch of a track: back to the project setting / the latch a forward start at step `pos` would hold (the last Snap FX above it)
    void snapFxReset(uint8_t track) { player.track[track].snapFx = kSnapFxNone; }
    void snapFxEnter(uint8_t track, int pos);
#endif
    void sendNoteOff(uint8_t track,
                     strPlayer::strPlayerTrack::strSendStep* step,
                     int8_t offOption = STEP_NOTE_OFF_DEFAULT);

    void sendMidiChordOn(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);
    void sendMidiChordOff(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);

    uint16_t getChordBitmask(uint8_t chord);
    uint16_t getChordBitmaskForArp(uint8_t chord);

    void send_clock(uint8_t);
    void stopAllMidiOpenNotes(void);

    int8_t velocityToVolume(int8_t velo);
    int8_t volumeToVelocity(int8_t vol);

    uint8_t recMicrotiming();
    uint8_t recVelocity();
    uint8_t getDestRecordingStep(uint8_t track, bool quantize, bool noteOff = false);

    uint8_t getNextArpNote(strPlayer::strPlayerTrack* playerRow);

    void updateExternalClockTempo(seqtimesec_t deltaTime);

    void updatePatternManagment();

    // The audio callback must not use the SD card, so stop() needs to know
    // whether it may reload the pattern itself.
    enum class StopContext
    {
        mainLoop,
        audioCallback,
    };
    void stop(StopContext context);
    void reloadPatternAfterStop();
    void stopForMissingNextPattern();

    strPattern* currentPattern;
    strPattern* bufferPattern;

    strPlayer player;

    seqrand::Rng rng;         // every random decision of the playing sequencer

    int externalClockPulses = 0;
    int externalClocksToPlay = 0;

    InternalTickClock internalTickClock;

    seqtimesec_t timeWithoutClockPulse = 0;
    int16_t externalClockCompensation_clockSignals = 0;
    int16_t clockSignalsToCompensate = 0;

    seqtimesec_t externalClockEvents[kExternalTempoCounterSize]{};
    uint8_t externalClockEventsCount = 0;
    bool externalClockIntervalValid = false;
    seqtimesec_t externalClockEventTime = 0;

    uint8_t lastStoppedStepPos = 0;
    uint16_t pendingSongPositionBeats = 0;
    bool hasPendingSongPosition = false;

    float externalTempo = DEFAULT_TEMPO;
    float lastKnownExternalTempo = DEFAULT_TEMPO;

    uint8_t renderMode = 0;

    MuteState synthsMuteState = MuteState::kUnmuted;

    CrcCalculator* crcCalculator;
    struct HeldNoteSource
    {
        int16_t source;
        uint8_t note;
    };
    static constexpr uint8_t kMaxLiveRecHeldNotes = 32;
    HeldNoteSource liveRecHeldNotes[kMaxLiveRecHeldNotes];
    uint8_t liveRecHeldNotesCount = 0;

    int8_t trackGroove[kAllTracks] = {};   // see setTrackGroove

    // Replace record (Config > Recording mode "Replace"): the playhead clears the steps of the armed tracks as it passes. A step the take
    // itself just wrote is kept (recKeepStep = step + 1, 0 = none, two slots: the note and its note off can both land ahead): a quantized note of the second half of a step lands on the NEXT step
    uint8_t recKeepStep[kAllTracks][2] = {};
    bool recReplaceOn() const;
    // the track the replace take erases: the only armed track, with several armed the track under the cursor when it is armed (the
    // track the live record writes to). Every track is armed by default, so the armed set alone would wipe the whole pattern
    bool recReplaceTrack(uint8_t track);
    void recReplaceEraseStep(uint8_t track, uint8_t step);
    bool recReplaceKept(uint8_t track, uint8_t step) const
    {
        return recKeepStep[track][0] == uint8_t(step + 1) || recKeepStep[track][1] == uint8_t(step + 1);
    }
    void recReplaceKeep(uint8_t track, uint8_t step)
    {
        recKeepStep[track][1] = recKeepStep[track][0];
        recKeepStep[track][0] = uint8_t(step + 1);
    }
};

extern Sequencer sequencer;

void cbkSequencialSwitch_SetReady(void);

#endif
