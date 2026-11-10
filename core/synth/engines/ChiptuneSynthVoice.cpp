#include "config.h"
#ifdef FEATURE_SYNTH

#include "ChiptuneSynthVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <numbers>

namespace polysynth
{

namespace
{

enum class ChipParamId : int
{
    kModel = 0,
    kTune,
    kFine,
    kShape,
    kPwmRate,
    kPwmDepth,
    kSubMix,
    kArpChord,
    kArpSpeed,
    kPitchSweep,
    kSweepTime,
    kVibRate,
    kVibDepth,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kBitcrush,
    kDecimate,
    kPlayMode,
    kGlideTime,
    kVolume,
};

enum class ChipModel : int
{
    kPulse2A03 = 0,
    kSidSawTri = 1,
    kNoiseLfsr = 2,
    kFmBell = 3,
    kSuperSquare = 4,
    kZapLaser = 5,
    kArpChord = 6,
    kChipDrum = 7,
};

} // namespace

namespace
{

static constexpr std::array<const char*, 8> kModelNames = {
    "2A03 Pulse",   "SID Saw/Tri", "Noise / LFSR", "FM Bell",
    "Super Square", "Zap / Laser", "Arp Chord",    "Chip Drum",
};

static constexpr std::array<const char*, 8> kArpNames = {
    "Off", "Octave", "Fifth", "Major", "Minor", "Sus4", "Maj7", "Min7",
};

// Semitone offsets for 4-step chiptune chord arpeggiator
static constexpr int8_t kArpChords[8][4] = {
    { 0, 0, 0, 0 },   // Off
    { 0, 12, 0, 12 }, // Octave
    { 0, 7, 12, 7 },  // Fifth
    { 0, 4, 7, 12 },  // Major
    { 0, 3, 7, 12 },  // Minor
    { 0, 5, 7, 12 },  // Sus4
    { 0, 4, 7, 11 },  // Maj7
    { 0, 3, 7, 10 },  // Min7
};

inline float polyBlep(float t, float dt)
{
    if (dt <= 0.0f)
    {
        return 0.0f;
    }
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

#ifdef FEATURE_SYNTH_OPEN
// MK66 (Cortex-M4F): no VRINT, so __builtin_floorf and __builtin_roundf in the SDK's fastPow2 and fastSine are newlib
// calls (floorf, roundf), as is std::round. Same polynomials as sdk::fastPow2 and sdk::fastSine, with round and floor
// as integer code that gives the same value for |x| < 2^31 (every argument here), so the voice renders bit identical to
// the SDK version at about two thirds of the cycles (synth-open/tools/phz_cycles/run_bench.sh fw CHIP).
// floor(x), exact for |x| < 2^31 (the sign of a zero result is not kept: fastPow2 and the phase wrap never see it)
[[gnu::always_inline]] inline float floorOf(float x)
{
    int i = static_cast<int>(x);
    i -= (static_cast<float>(i) > x) ? 1 : 0;
    return static_cast<float>(i);
}

// round half away from zero, the same value and sign (also of a zero) as roundf for |x| < 2^31
[[gnu::always_inline]] inline float roundOf(float x)
{
    const float t = static_cast<float>(static_cast<int>(x));
    const float d = x - t; // exact
    return __builtin_copysignf(t + ((d >= 0.5f) ? 1.0f : 0.0f) - ((d <= -0.5f) ? 1.0f : 0.0f), x);
}

// sdk::fastSine with roundOf
inline float fastSine(float phase)
{
    float t = phase - roundOf(phase);
    if (t > 0.25f)
    {
        t = 0.5f - t;
    }
    else if (t < -0.25f)
    {
        t = -0.5f - t;
    }
    const float t2 = t * t;
    return t * (6.283164f + t2 * (-41.337143f + t2 * (81.340782f + t2 * -70.993568f)));
}

// sdk::fastPow2 with floorOf
inline float fastPow2(float x)
{
    x = std::clamp(x, -126.0f, 127.0f);
    const float whole = floorOf(x);
    const float f = x - whole;
    const float mantissa =
      1.0f + f * (0.69315249f +
                  f * (0.24015281f + f * (0.05583591f + f * (0.0089734f + f * 0.00188529f))));
    const union
    {
        uint32_t bits;
        float value;
    } scale = { static_cast<uint32_t>(static_cast<int32_t>(whole) + 127) << 23 };
    return mantissa * scale.value;
}
#else
using sdk::fastPow2;
using sdk::fastSine;
inline float floorOf(float x) { return std::floor(x); }
inline float roundOf(float x) { return std::round(x); }
#endif

// The same as std::fmod(x, 1.0f) for x in [0, 2), which is all the pulse oscillators give it.
inline float wrapPhase(float x)
{
    return (x >= 1.0f) ? x - 1.0f : x;
}

inline float nesSteppedTriangle(float phase01)
{
    const float tri = (phase01 < 0.5f) ? (phase01 * 4.0f - 1.0f) : (3.0f - phase01 * 4.0f);
    // 4-bit quantized 16-step triangle like the NES 2A03
    return roundOf(tri * 7.5f) * (1.0f / 7.5f);
}

inline uint16_t stepLfsr(uint16_t lfsr, bool metallicShortMode)
{
    const uint16_t tap = metallicShortMode ? ((lfsr >> 6) & 1u) : ((lfsr >> 1) & 1u);
    const uint16_t bit = ((lfsr >> 0) ^ tap) & 1u;
    return static_cast<uint16_t>((lfsr >> 1) | (bit << 14));
}

class ChiptuneSynthVoice : public sdk::CustomSynthVoice<ChiptuneSynthVoice, ChipParamId>
{
  public:
    ChiptuneSynthVoice() = default;

    bool isActive() final { return envStage != EnvStage::kIdle; }

#ifdef FEATURE_SYNTH_OPEN
    // Open shim hooks (see ChiptuneSynthVoice.h): same effect as the param handlers in createMetadata
    void openSetParam(int paramId, float val)
    {
        switch (static_cast<ChipParamId>(paramId))
        {
            case ChipParamId::kModel:
                model = static_cast<ChipModel>(
                  std::clamp<int>(static_cast<int>(std::lround(val)), 0, kModelNames.size() - 1));
                break;
            case ChipParamId::kTune: tuneSemitones = std::round(val); break;
            case ChipParamId::kFine: fineCents = val; break;
            case ChipParamId::kShape: shape = val; break;
            case ChipParamId::kPwmRate: pwmRateHz = val; break;
            case ChipParamId::kPwmDepth: pwmDepth = val; break;
            case ChipParamId::kSubMix: subMix = val; break;
            case ChipParamId::kArpChord:
                arpChordIdx =
                  std::clamp<int>(static_cast<int>(std::lround(val)), 0, kArpNames.size() - 1);
                break;
            case ChipParamId::kArpSpeed: arpSpeedHz = val; break;
            case ChipParamId::kPitchSweep: pitchSweepSemitones = val; break;
            case ChipParamId::kSweepTime: sweepTimeSec = val; break;
            case ChipParamId::kVibRate: vibRateHz = val; break;
            case ChipParamId::kVibDepth: vibDepth = val; break;
            case ChipParamId::kAmpAttack: ampAttackSec = val; break;
            case ChipParamId::kAmpDecay: ampDecaySec = val; break;
            case ChipParamId::kAmpSustain: ampSustainLevel = val; break;
            case ChipParamId::kAmpRelease: ampReleaseSec = val; break;
            case ChipParamId::kFilterCutoff: filterCutoffHz = val; break;
            case ChipParamId::kFilterResonance: filterResonance = val; break;
            case ChipParamId::kFilterEnvAmount: filterEnvAmount = val; break;
            case ChipParamId::kBitcrush: bitcrushBits = val; break;
            case ChipParamId::kDecimate: decimateRate = val; break;
            case ChipParamId::kPlayMode:
                playMode = static_cast<PlayMode>(std::clamp<int>(std::lround(val), 0, 2));
                break;
            case ChipParamId::kGlideTime: glideTimeSec = val; break;
            case ChipParamId::kVolume: break; // applied by the shim
        }
    }
    void openSetBend(float semitones) { pitchBendSemitones = semitones; }
#endif

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/,
                  note_t note,
                  float /*velocity*/,
                  bool isLegatoStep) final
    {
        const float targetNote = static_cast<float>(note);
        if (!hasPlayedAnyNote || glideTimeSec <= 0.001f ||
            (playMode == PlayMode::kLegato && !isLegatoStep))
        {
            currentGlideNote = targetNote;
        }
        targetGlideNote = targetNote;
        hasPlayedAnyNote = true;

        if (!isLegatoStep || envStage == EnvStage::kIdle || envStage == EnvStage::kRelease)
        {
            envStage = EnvStage::kAttack;
            noteAgeSamples = 0;
            sweepOffsetSemitones = 0.0f;
            arpStepCounter = 0;
            arpSampleCounter = 0;
            lfsrState = 0x7FFFu;
        }
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        if (envStage != EnvStage::kIdle)
        {
            envStage = EnvStage::kRelease;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/) final
    {
        auto out = outputBuffer[0];
        if (envStage == EnvStage::kIdle)
        {
            std::fill(out.begin(), out.end(), 0.0f);
            return;
        }

        const float sr = sdk::kSampleRate;
        const float invSr = 1.0f / sr;

        // Envelope increments per sample
        const float attackInc =
          (ampAttackSec > 0.001f) ? (1.0f / (ampAttackSec * sr)) : 1.0f;
        const float decayDec =
          (ampDecaySec > 0.002f) ? ((1.0f - ampSustainLevel) / (ampDecaySec * sr)) : 1.0f;
        const float releaseDec =
          (ampReleaseSec > 0.002f) ? (1.0f / (ampReleaseSec * sr)) : 1.0f;

        // Portamento / glide coefficient
        const float glideStep =
          (glideTimeSec > 0.001f) ? (1.0f / (glideTimeSec * sr)) : 1.0f;

        // Arpeggio step length in samples
        const int effArpIdx =
          (model == ChipModel::kArpChord && arpChordIdx == 0) ? 3 : arpChordIdx;
        const uint32_t arpPeriodSamples =
          std::max<uint32_t>(64u, static_cast<uint32_t>(sr / std::max(5.0f, arpSpeedHz)));

        // Pitch sweep decay coefficient
        const float sweepSamples = std::max(64.0f, sweepTimeSec * sr);
        const float invSweepSamples = 1.0f / sweepSamples;
        const float zapSweepScale = 12.0f + 24.0f * shape;

        // Bitcrush quantization levels
        const bool crushActive = (bitcrushBits < 15.5f);
        const float crushScale = crushActive ? fastPow2(bitcrushBits - 1.0f) : 32768.0f;
        const float invCrushScale = 1.0f / crushScale;
        const int decimateFactor = std::clamp(static_cast<int>(decimateRate + 0.5f), 1, 32);

        // Chamberlin SVF resonance / damping
        const float qDamper = std::clamp(1.0f - 0.85f * filterResonance, 0.12f, 1.0f);

        // Per-block invariant oscillator / LFO / filter constants
        const float baseNoteOffset = tuneSemitones + (fineCents * 0.01f) + pitchBendSemitones;
        const float pwmPhaseInc = pwmRateHz * invSr;
        const float vibPhaseInc = vibRateHz * invSr;
        const float vibDepthSemi = vibDepth * 1.5f;
        const bool hasVibrato = (vibDepth > 0.0f);
        const float baseDuty = 0.125f + 0.75f * shape;
        const float pwmDutyScale = 0.38f * pwmDepth;
        const float lfsrRateMult = 2.0f + 14.0f * (1.0f - pwmDepth);
        const bool lfsrShortMode = (shape >= 0.5f);
        static constexpr std::array<float, 8> kFmRatios = {
            1.0f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f, 9.0f
        };
        const float fmRatio = kFmRatios[std::clamp(static_cast<int>(shape * 7.99f), 0, 7)];
        const float fmModScale = 0.4f + 3.5f * (0.35f + 0.65f * pwmDepth);
        const float superSquareSpread = 0.002f + 0.018f * shape;
        const float superSquareMult2 = 1.0f + superSquareSpread;
        const float superSquareMult3 = 1.0f - superSquareSpread;

        // ChipDrum exponential pitch envelope state (e^(-n / (0.08 * sr)))
        static constexpr float kDrumDecayStep = 0.99971659f;
        float drumDecay = (model == ChipModel::kChipDrum)
                            ? fastPow2(-1.4426950f * static_cast<float>(noteAgeSamples) *
                                       (1.0f / (0.08f * sr)))
                            : 0.0f;
        const float drumSweepScale = 3.5f * (1.0f - shape * 0.7f);
        const bool drumMetallicMode = (shape > 0.75f);
        const float drumSustainedNoiseMix = std::clamp(shape * 1.25f, 0.0f, 1.0f);

        const bool hasSubMix = (subMix > 0.001f);
        const float subMainGain = 1.0f - 0.35f * subMix;
        const float subOscGain = 0.75f * subMix;

        const float filterEnvScale = filterEnvAmount * 4.0f;
        const bool hasFilterEnv = (std::abs(filterEnvAmount) > 0.0001f);
        float cachedFilterEnvLevel = -1.0f;
        float cachedEnvCutoff = std::clamp(filterCutoffHz, 40.0f, 18000.0f);
        bool cachedFilterActive = (cachedEnvCutoff < 17500.0f || filterResonance > 0.02f);
        float cachedSvfF =
          (!hasFilterEnv && cachedFilterActive)
            ? std::min(0.85f, 2.0f * fastSine(0.5f * cachedEnvCutoff * invSr))
            : 0.0f;

#ifdef FEATURE_SYNTH_OPEN
        float freqNote = 0.0f;
        float freqHz = 0.0f;
        bool freqValid = false;
#endif

        for (size_t i = 0; i < out.size(); ++i)
        {
            // 1. Update ADSR envelope
            switch (envStage)
            {
                case EnvStage::kAttack:
                    envLevel += attackInc;
                    if (envLevel >= 1.0f)
                    {
                        envLevel = 1.0f;
                        envStage = EnvStage::kDecay;
                    }
                    break;
                case EnvStage::kDecay:
                    envLevel -= decayDec;
                    if (envLevel <= ampSustainLevel)
                    {
                        envLevel = ampSustainLevel;
                        envStage = (ampSustainLevel <= 0.0001f) ? EnvStage::kIdle
                                                                : EnvStage::kSustain;
                    }
                    break;
                case EnvStage::kSustain:
                    envLevel = ampSustainLevel;
                    break;
                case EnvStage::kRelease:
                    envLevel -= releaseDec;
                    if (envLevel <= 0.0001f)
                    {
                        envLevel = 0.0f;
                        envStage = EnvStage::kIdle;
                    }
                    break;
                case EnvStage::kIdle:
                    out[i] = 0.0f;
                    continue;
            }

            // 2. Update glide
            if (std::abs(currentGlideNote - targetGlideNote) > 0.001f)
            {
                const float diff = targetGlideNote - currentGlideNote;
                const float step = std::min(std::abs(diff), glideStep * 24.0f);
                currentGlideNote += (diff > 0.0f) ? step : -step;
            }
            else
            {
                currentGlideNote = targetGlideNote;
            }

            // 3. Update arpeggiator
            int8_t arpSemi = 0;
            if (effArpIdx > 0 && effArpIdx < 8)
            {
                if (++arpSampleCounter >= arpPeriodSamples)
                {
                    arpSampleCounter = 0;
                    arpStepCounter = (arpStepCounter + 1u) & 3u;
                }
                arpSemi = kArpChords[effArpIdx][arpStepCounter & 3u];
            }

            // 4. Update LFOs (PWM & Vibrato)
            pwmPhase += pwmPhaseInc;
            if (pwmPhase >= 1.0f)
            {
                pwmPhase -= 1.0f;
            }
            const float pwmLfo =
              (pwmPhase < 0.5f) ? (pwmPhase * 4.0f - 1.0f) : (3.0f - pwmPhase * 4.0f);

            vibPhase += vibPhaseInc;
            if (vibPhase >= 1.0f)
            {
                vibPhase -= 1.0f;
            }
            const float vibSemitones = hasVibrato ? (fastSine(vibPhase) * vibDepthSemi) : 0.0f;

            // 5. Pitch sweep envelope
            const float sweepProgress =
              std::min(1.0f, static_cast<float>(noteAgeSamples) * invSweepSamples);
            float activeSweepSemi = pitchSweepSemitones * sweepProgress;
            if (model == ChipModel::kZapLaser)
            {
                // Zap/Laser has built-in rapid downward pitch drop modulated by Shape
                const float zapEnv = 1.0f - sweepProgress;
                activeSweepSemi += zapSweepScale * (zapEnv * zapEnv * zapEnv - 1.0f);
            }

            const float totalNote = currentGlideNote + baseNoteOffset +
                                    static_cast<float>(arpSemi) + vibSemitones + activeSweepSemi;
            // sdk::noteToFreq(0, totalNote), as note 0 is MIDI note 0
#ifdef FEATURE_SYNTH_OPEN
            // only when the note moves (a held note without vibrato, sweep or glide keeps it): same value, less work
            if (!freqValid || totalNote != freqNote)
            {
                freqNote = totalNote;
                freqHz = std::clamp(8.1757989f * fastPow2(totalNote * (1.0f / 12.0f)), 16.0f, 18000.0f);
                freqValid = true;
            }
            const float freq = freqHz;
#else
            const float freq =
              std::clamp(8.1757989f * fastPow2(totalNote * (1.0f / 12.0f)), 16.0f, 18000.0f);
#endif
            const float dt = freq * invSr;

            // Duty cycle with PWM modulation
            const float duty = std::clamp(baseDuty + pwmDutyScale * pwmLfo, 0.06f, 0.94f);

            // 6. Synthesize selected voice model
            float sig = 0.0f;
            phase += dt;
            bool wrapped = false;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                wrapped = true;
            }

            switch (model)
            {
                case ChipModel::kPulse2A03:
                case ChipModel::kArpChord:
                case ChipModel::kZapLaser:
                {
                    float pulse = (phase < duty) ? 1.0f : -1.0f;
                    pulse += polyBlep(phase, dt);
                    pulse -= polyBlep(wrapPhase(phase - duty + 1.0f), dt);
                    sig = pulse * 0.75f;
                    break;
                }

                case ChipModel::kSidSawTri:
                {
                    float saw = 2.0f * phase - 1.0f;
                    saw -= polyBlep(phase, dt);
                    const float tri =
                      (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
                    // Ring-mod & waveform blend controlled by duty/shape
                    const float ring = saw * ((subPhase < 0.5f) ? 1.0f : -1.0f);
                    sig = ((1.0f - duty) * tri + duty * (0.65f * saw + 0.35f * ring)) * 0.8f;
                    break;
                }

                case ChipModel::kNoiseLfsr:
                {
                    // Clock LFSR at pitch-tracked rate
                    lfsrPhase += dt * lfsrRateMult;
                    while (lfsrPhase >= 1.0f)
                    {
                        lfsrPhase -= 1.0f;
                        lfsrState = stepLfsr(lfsrState, lfsrShortMode);
                    }
                    sig = ((lfsrState & 1u) != 0u ? 0.8f : -0.8f);
                    break;
                }

                case ChipModel::kFmBell:
                {
                    // 2-op OPL chiptune FM: integer/bell ratios selected by Shape
                    const float modDt = dt * fmRatio;
                    osc2Phase += modDt;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= floorOf(osc2Phase);
                    }
                    const float modIndex = fmModScale * envLevel;
                    const float modSig = fastSine(osc2Phase + 0.15f * pwmLfo);
                    // Quantize FM carrier to 6-bit OPL DAC character
                    const float rawCarrier = fastSine(phase + modIndex * modSig * 0.5f);
                    sig = roundOf(rawCarrier * 31.0f) * (0.85f / 31.0f);
                    break;
                }

                case ChipModel::kSuperSquare:
                {
                    // 3 detuned pulse oscillators
                    const float dt2 = dt * superSquareMult2;
                    const float dt3 = dt * superSquareMult3;
                    osc2Phase += dt2;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= 1.0f;
                    }
                    osc3Phase += dt3;
                    if (osc3Phase >= 1.0f)
                    {
                        osc3Phase -= 1.0f;
                    }
                    float p1 = (phase < duty) ? 1.0f : -1.0f;
                    p1 += polyBlep(phase, dt) -
                          polyBlep(wrapPhase(phase - duty + 1.0f), dt);
                    float p2 = (osc2Phase < duty) ? 1.0f : -1.0f;
                    p2 += polyBlep(osc2Phase, dt2) -
                          polyBlep(wrapPhase(osc2Phase - duty + 1.0f), dt2);
                    float p3 = (osc3Phase < duty) ? 1.0f : -1.0f;
                    p3 += polyBlep(osc3Phase, dt3) -
                          polyBlep(wrapPhase(osc3Phase - duty + 1.0f), dt3);
                    sig = (p1 + 0.8f * p2 + 0.8f * p3) * 0.34f;
                    break;
                }

                case ChipModel::kChipDrum:
                {
                    // Morphs from Kick (0..0.33) -> Snare (0.33..0.67) -> Metallic Hat (0.67..1.0)
                    const float drumFreq =
                      freq * (1.0f + drumSweepScale * drumDecay * drumDecay);
                    drumDecay *= kDrumDecayStep;
                    osc2Phase += drumFreq * invSr;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= floorOf(osc2Phase);
                    }
                    if (wrapped || (noteAgeSamples & 1u) == 0u)
                    {
                        lfsrState = stepLfsr(lfsrState, drumMetallicMode);
                    }
                    const float body = nesSteppedTriangle(osc2Phase);
                    const float noise = ((lfsrState & 1u) != 0u) ? 0.85f : -0.85f;
                    const float noiseMix = (noteAgeSamples < 180u) ? 0.7f : drumSustainedNoiseMix;
                    sig = (1.0f - noiseMix) * body + noiseMix * noise;
                    break;
                }
            }

            // 7. Add 4-bit stepped NES Sub-Oscillator (-1 octave)
            subPhase += dt * 0.5f;
            if (subPhase >= 1.0f)
            {
                subPhase -= 1.0f;
            }
            if (hasSubMix)
            {
                sig = sig * subMainGain + nesSteppedTriangle(subPhase) * subOscGain;
            }

            // 8. Sample-rate decimation & bitcrusher
            if (++decimateCounter >= decimateFactor)
            {
                decimateCounter = 0;
                if (crushActive)
                {
                    heldSample = roundOf(sig * crushScale) * invCrushScale;
                }
                else
                {
                    heldSample = sig;
                }
            }
            float postCrush = heldSample;

            // 9. State-Variable Lowpass Filter with envelope modulation
            if (hasFilterEnv && envLevel != cachedFilterEnvLevel)
            {
                cachedFilterEnvLevel = envLevel;
                cachedEnvCutoff = std::clamp(
                  filterCutoffHz * fastPow2(filterEnvScale * envLevel), 40.0f, 18000.0f);
                cachedFilterActive = (cachedEnvCutoff < 17500.0f || filterResonance > 0.02f);
                if (cachedFilterActive)
                {
                    cachedSvfF = std::min(0.85f, 2.0f * fastSine(0.5f * cachedEnvCutoff * invSr));
                }
            }
            if (cachedFilterActive)
            {
                svfLow += cachedSvfF * svfBand;
                const float high = postCrush - svfLow - qDamper * svfBand;
                svfBand += cachedSvfF * high;
                svfBand = std::clamp(svfBand, -2.0f, 2.0f);
                svfLow = std::clamp(svfLow, -2.0f, 2.0f);
                postCrush = svfLow;
            }

            out[i] = postCrush * envLevel * volumeMult;
            ++noteAgeSamples;
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
    {
        using namespace polysynth::sdk;

        metadata.params.resize(enumutil::EnumMax<ChipParamId>::value + 1);
        metadata.params.shrink_to_fit();

        if (paramHandlers.empty())
        {
            paramHandlers.resize(enumutil::EnumMax<ChipParamId>::value + 1);
        }

        ParamHelper<ChiptuneSynthVoice> helper(metadata, paramHandlers);

        helper.addParam(
          *ChipParamId::kModel,
          {
            .displayName = "Model",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = static_cast<Patch::paramvalue_short_t>(kModelNames.size() - 1),
            .isEnum = true,
            .midiCC = *MidiCCs::kOscParam0,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                const int idx =
                  std::clamp<int>(static_cast<int>(std::lround(value)), 0, kModelNames.size() - 1);
                return std::string(kModelNames[idx]);
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          {
              voice.model = static_cast<ChipModel>(
                std::clamp<int>(static_cast<int>(std::lround(*val)), 0, kModelNames.size() - 1));
          });

        helper.addParam(
          *ChipParamId::kTune,
          {
            .displayName = "Tune",
            .defaultValue = 0.0f,
            .minValue = -24,
            .maxValue = 24,
            .midiCC = *MidiCCs::kOscParam0 + 1,
            .valueFormatter = createSemitoneFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.tuneSemitones = std::round(*val); });

        helper.addParam(
          *ChipParamId::kFine,
          {
            .displayName = "Fine",
            .defaultValue = 0.0f,
            .minValue = -100,
            .maxValue = 100,
            .midiCC = *MidiCCs::kOscParam0 + 2,
            .valueFormatter = createCentsFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.fineCents = *val; });

        helper.addParam(
          *ChipParamId::kShape,
          {
            .displayName = "Duty / Shape",
            .defaultValue = 0.35f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 3,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.shape = *val; });

        helper.addParam(
          *ChipParamId::kPwmRate,
          {
            .displayName = "PWM Rate",
            .defaultValue = 2.5f,
            .minValue = 0.1f,
            .maxValue = 40.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kOscParam0 + 4,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.pwmRateHz = *val; });

        helper.addParam(
          *ChipParamId::kPwmDepth,
          {
            .displayName = "PWM Depth",
            .defaultValue = 0.2f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 5,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.pwmDepth = *val; });

        helper.addParam(
          *ChipParamId::kSubMix,
          {
            .displayName = "Sub Osc",
            .defaultValue = 0.25f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 6,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.subMix = *val; });

        helper.addParam(
          *ChipParamId::kArpChord,
          {
            .displayName = "Chord Arp",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = static_cast<Patch::paramvalue_short_t>(kArpNames.size() - 1),
            .isEnum = true,
            .midiCC = *MidiCCs::kOscParam0 + 7,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                const int idx =
                  std::clamp<int>(static_cast<int>(std::lround(value)), 0, kArpNames.size() - 1);
                return std::string(kArpNames[idx]);
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          {
              voice.arpChordIdx =
                std::clamp<int>(static_cast<int>(std::lround(*val)), 0, kArpNames.size() - 1);
          });

        helper.addParam(
          *ChipParamId::kArpSpeed,
          {
            .displayName = "Arp Speed",
            .defaultValue = 50.0f,
            .minValue = 5.0f,
            .maxValue = 75.0f,
            .midiCC = *MidiCCs::kOscParam0 + 8,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.arpSpeedHz = *val; });

        helper.addParam(
          *ChipParamId::kPitchSweep,
          {
            .displayName = "Pitch Sweep",
            .defaultValue = 0.0f,
            .minValue = -36.0f,
            .maxValue = 36.0f,
            .midiCC = *MidiCCs::kOscParam0 + 9,
            .valueFormatter = createSemitoneFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.pitchSweepSemitones = *val; });

        helper.addParam(
          *ChipParamId::kSweepTime,
          {
            .displayName = "Sweep Time",
            .defaultValue = 0.25f,
            .minValue = 0.02f,
            .maxValue = 2.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kOscParam0 + 10,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.sweepTimeSec = *val; });

        helper.addParam(
          *ChipParamId::kVibRate,
          {
            .displayName = "Vibrato Rate",
            .defaultValue = 6.0f,
            .minValue = 0.5f,
            .maxValue = 25.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kLfo1Frequency,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.vibRateHz = *val; });

        helper.addParam(
          *ChipParamId::kVibDepth,
          {
            .displayName = "Vibrato Depth",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kOscParam0 + 11,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.vibDepth = *val; });

        helper.addParam(
          *ChipParamId::kAmpAttack,
          {
            .displayName = "Attack",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvAttack,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampAttackSec = *val; });

        helper.addParam(
          *ChipParamId::kAmpDecay,
          {
            .displayName = "Decay",
            .defaultValue = 0.35f,
            .minValue = 0.01f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvDecay,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampDecaySec = *val; });

        helper.addParam(
          *ChipParamId::kAmpSustain,
          {
            .displayName = "Sustain",
            .defaultValue = 0.4f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kAmpEnvSustain,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampSustainLevel = *val; });

        helper.addParam(
          *ChipParamId::kAmpRelease,
          {
            .displayName = "Release",
            .defaultValue = 0.15f,
            .minValue = 0.01f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvRelease,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampReleaseSec = *val; });

        helper.addParam(
          *ChipParamId::kFilterCutoff,
          {
            .displayName = "Cutoff",
            .defaultValue = 12000.0f,
            .minValue = 80.0f,
            .maxValue = 18000.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
            .midiCC = *MidiCCs::kFilterCutoff,
            .valueFormatter = createFrequencyFormatter(18000.0f),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterCutoffHz = *val; });

        helper.addParam(
          *ChipParamId::kFilterResonance,
          {
            .displayName = "Resonance",
            .defaultValue = 0.15f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kFilterResonance,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterResonance = *val; });

        helper.addParam(
          *ChipParamId::kFilterEnvAmount,
          {
            .displayName = "Env Amt",
            .defaultValue = 0.2f,
            .minValue = -1.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kFilterEnvAmount,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterEnvAmount = *val; });

        helper.addParam(
          *ChipParamId::kBitcrush,
          {
            .displayName = "Bitcrush",
            .defaultValue = 8.0f,
            .minValue = 2.0f,
            .maxValue = 16.0f,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f} bit", value); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.bitcrushBits = *val; });

        helper.addParam(
          *ChipParamId::kDecimate,
          {
            .displayName = "Decimate",
            .defaultValue = 1.0f,
            .minValue = 1.0f,
            .maxValue = 32.0f,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f}x", std::round(value)); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.decimateRate = *val; });

        helper.addParam(
          *ChipParamId::kPlayMode,
          {
            .displayName = "Glide Mode",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = 2,
            .isEnum = true,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                constexpr std::array kModes = { "Always", "Overlap", "Legato" };
                return std::string(kModes.at(std::clamp<int>(std::lround(value), 0, 2)));
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.playMode = static_cast<PlayMode>(std::clamp<int>(std::lround(*val), 0, 2)); });

        helper.addParam(
          *ChipParamId::kGlideTime,
          {
            .displayName = "Glide Time",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.glideTimeSec = *val; });

        helper.addParam(
          *ChipParamId::kVolume,
          {
            .displayName = "Volume",
            .defaultValue = 1.0f,
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.volumeMult = *val; });

        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              {
                .name = "Oscillator",
                .shortName = "Osc",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kModel },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kTune },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFine },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kShape },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmRate },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmDepth },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSubMix },
                },
              },
              {
                .name = "Modulation",
                .shortName = "Mod",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpChord },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpSpeed },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPitchSweep },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSweepTime },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibRate },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibDepth },
                },
              },
              {
                .name = "Amplifier",
                .shortName = "Amp",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpAttack },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpDecay },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpSustain },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpRelease },
                },
              },
              {
                .name = "Filter",
                .shortName = "Filter",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterCutoff },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterResonance },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterEnvAmount },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kBitcrush },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kDecimate },
                },
              },
              {
                .name = "Voice",
                .shortName = "Voice",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPlayMode },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kGlideTime },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVolume },
                },
              },
            },
        };

        metadata.layout23 = metadata.layoutFlat;

        metadata.initDefaultMacros = [](auto& macros)
        {
            macros[0] = Patch::Metadata::Macro{
                .displayName = "Timbre / PWM",
                .modifiers = {
                  { *ChipParamId::kShape, 0.7f },
                  { *ChipParamId::kPwmDepth, 0.5f },
                },
                .bipolar = true,
            };
            macros[1] = Patch::Metadata::Macro{
                .displayName = "Arp / Chord",
                .modifiers = {
                  { *ChipParamId::kArpChord, 1.0f },
                },
                .bipolar = false,
            };
            macros[2] = Patch::Metadata::Macro{
                .displayName = "Sweep / Vibrato",
                .modifiers = {
                  { *ChipParamId::kVibDepth, 0.6f },
                  { *ChipParamId::kPitchSweep, 0.5f },
                },
                .bipolar = true,
            };
            macros[3] = Patch::Metadata::Macro{
                .displayName = "Env Decay / Rel",
                .modifiers = {
                  { *ChipParamId::kAmpDecay, 0.8f },
                  { *ChipParamId::kAmpRelease, 0.6f },
                },
                .bipolar = true,
            };
            macros[4] = Patch::Metadata::Macro{
                .displayName = "Crush / Lo-Fi",
                .modifiers = {
                  { *ChipParamId::kBitcrush, -0.6f },
                  { *ChipParamId::kDecimate, 0.4f },
                },
                .bipolar = false,
            };
            macros[5] = Patch::Metadata::Macro{
                .displayName = "Filter Cutoff",
                .modifiers = {
                  { *ChipParamId::kFilterCutoff, 1.0f },
                  { *ChipParamId::kFilterResonance, 0.35f },
                },
                .bipolar = true,
            };

            macros[*Patch::SystemMacroId::kVelocity].modifiers = {
                { *ChipParamId::kVolume, 1.0f },
            };
        };

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kChiptuneSynth);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
    }

  private:
    enum class EnvStage : uint8_t
    {
        kIdle = 0,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
    };

    ChipModel model = ChipModel::kPulse2A03;
    float tuneSemitones = 0.0f;
    float fineCents = 0.0f;
    float shape = 0.35f;
    float pwmRateHz = 2.5f;
    float pwmDepth = 0.2f;
    float subMix = 0.25f;

    int arpChordIdx = 0;
    float arpSpeedHz = 50.0f;
    float pitchSweepSemitones = 0.0f;
    float sweepTimeSec = 0.25f;
    float vibRateHz = 6.0f;
    float vibDepth = 0.0f;

    float ampAttackSec = 0.0f;
    float ampDecaySec = 0.35f;
    float ampSustainLevel = 0.4f;
    float ampReleaseSec = 0.15f;

    float filterCutoffHz = 12000.0f;
    float filterResonance = 0.15f;
    float filterEnvAmount = 0.2f;
    float bitcrushBits = 8.0f;
    float decimateRate = 1.0f;

    float glideTimeSec = 0.0f;
    float volumeMult = 1.0f;

    // Real-time voice state
    EnvStage envStage = EnvStage::kIdle;
    float envLevel = 0.0f;
    bool hasPlayedAnyNote = false;
    float currentGlideNote = 60.0f;
    float targetGlideNote = 60.0f;

    float phase = 0.0f;
    float osc2Phase = 0.0f;
    float osc3Phase = 0.0f;
    float subPhase = 0.0f;
    float pwmPhase = 0.0f;
    float vibPhase = 0.0f;
    float lfsrPhase = 0.0f;
    uint16_t lfsrState = 0x7FFFu;

    uint32_t noteAgeSamples = 0;
    uint32_t arpSampleCounter = 0;
    uint32_t arpStepCounter = 0;
    float sweepOffsetSemitones = 0.0f;

    int decimateCounter = 0;
    float heldSample = 0.0f;
    float svfLow = 0.0f;
    float svfBand = 0.0f;
};

} // namespace

#ifdef FEATURE_SYNTH_OPEN
namespace open
{
#ifdef __arm__
static_assert(sizeof(ChiptuneSynthVoice) == kChipVoiceBytes && alignof(ChiptuneSynthVoice) <= 4,
              "kChipVoiceBytes (ChiptuneSynthVoice.h) is the voice object's size");
#endif
InstrumentVoice* chipVoiceConstruct(void* storage)
{
#ifndef __arm__
    // host tests (64 bit pointers): the object is bigger than the MK66's kChipVoiceBytes, so it cannot go into the plugin's storage
    if (sizeof(ChiptuneSynthVoice) > kChipVoiceBytes)
    {
        return new ChiptuneSynthVoice();
    }
#endif
    return new (storage) ChiptuneSynthVoice();
}
std::unique_ptr<InstrumentVoice> chipVoiceCreate() { return std::make_unique<ChiptuneSynthVoice>(); }
void chipVoiceReset(InstrumentVoice& voice)
{
    static_cast<ChiptuneSynthVoice&>(voice) = ChiptuneSynthVoice();
}
void chipVoiceSetParam(InstrumentVoice& voice, int paramId, float value)
{
    static_cast<ChiptuneSynthVoice&>(voice).openSetParam(paramId, value);
}
void chipVoiceSetBend(InstrumentVoice& voice, float semitones)
{
    static_cast<ChiptuneSynthVoice&>(voice).openSetBend(semitones);
}
} // namespace open
#endif

std::unique_ptr<InstrumentType> createChiptuneSynth()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "CHIP",
                      .createMetadata = &ChiptuneSynthVoice::createMetadata,
                      .createVoice = [](int /*voiceIdx*/) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<ChiptuneSynthVoice>(); } });
}

} // namespace polysynth

#endif
