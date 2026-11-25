// mtAudioEngine.cpp, included once after the includes: the side send mixers of stereo sends and the feature hooks of the audio engine. The hooks are statement macros (as in devhooks.h): they expand to the very tokens the inline blocks had, so audio path code stays inlined and the image byte identical. Without the features they are empty
#ifdef FW_STEREO_SENDS
// The side (L - R) / 2 send mixers of the delay and the reverb (stereo_sends.h), set in audioEngine::audioEngine next to the mid ones
using SendMixer = AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>;
static SendMixer* sideDelayMixer;
static SendMixer* sidePolyverbMixer;
static inline bool stereoSendsOn()
{
    return stereopairs::g_mask != 0;
}
#endif

#ifdef FW_STEREO_SENDS
// audioEngine::init: the side (L - R) / 2 send mixers next to the mid ones
#define FF_AE_SIDE_MIXERS_INIT()                                                                    \
    sidePolyverbMixer = mixerAndFxs.getSideMixer(MixerAndFxs<kVoices>::FxType::kReverb);            \
    sideDelayMixer = mixerAndFxs.getSideMixer(MixerAndFxs<kVoices>::FxType::kDelay);
// a voice's filter output into the side mixers too (gain 0 until a pair is linked, the mixer then releases it unread)
#define FF_AE_SIDE_CONNECT_VOICE(voice, idx)                                                         \
    connections.push_back(new AudioConnection(                                                       \
      (voice)->getFilter(), 0, mixerAndFxs->getSideMixer(MixerAndFxs<kVoices>::FxType::kReverb), idx)); \
    connections.push_back(new AudioConnection(                                                       \
      (voice)->getFilter(), 0, mixerAndFxs->getSideMixer(MixerAndFxs<kVoices>::FxType::kDelay), idx));
// the line input into the side mixers of both sends
#define FF_AE_SIDE_CONNECT_INPUT(input)                                                              \
    for (int fx = 0; fx < MixerAndFxs<kVoices>::FxType::kFxCount; fx++)                              \
    {                                                                                                \
        auto* side = mixerAndFxs->getSideMixer((MixerAndFxs<kVoices>::FxType)fx);                    \
        connections.push_back(new AudioConnection(                                                   \
          (input)->getGate(AudioInput::InputChannels::kLeft), 0, side, kLeftInputMixerChannel));     \
        connections.push_back(new AudioConnection(                                                   \
          (input)->getGate(AudioInput::InputChannels::kRight), 0, side, kRightInputMixerChannel));   \
    }
// the line in send of one effect (sideMixer) while a pair is linked: side = (L - R) / 2
#define FF_AE_SIDE_LINEIN(sideMixer, g)                   \
    (sideMixer)->gain(8, stereoSendsOn() ? (g) : 0.0f);   \
    (sideMixer)->gain(9, stereoSendsOn() ? -(g) : 0.0f);
// disableLineInImpact: the line in side sends off
#define FF_AE_SIDE_LINEIN_MUTE()            \
    sideDelayMixer->gain(8, AMP_MUTED);     \
    sideDelayMixer->gain(9, AMP_MUTED);     \
    sidePolyverbMixer->gain(8, AMP_MUTED);  \
    sidePolyverbMixer->gain(9, AMP_MUTED);
// a pair is linked or unlinked: the side sends of both channels at 0
#define FF_AE_SIDE_PAIR_ZERO(ch, follower)                  \
    sideDelayMixer->gain(ch, 0.0f);                         \
    sideDelayMixer->gain((follower)->nChannel, 0.0f);       \
    sidePolyverbMixer->gain(ch, 0.0f);                      \
    sidePolyverbMixer->gain((follower)->nChannel, 0.0f);
#else
#define FF_AE_SIDE_MIXERS_INIT()
#define FF_AE_SIDE_CONNECT_VOICE(voice, idx)
#define FF_AE_SIDE_CONNECT_INPUT(input)
#define FF_AE_SIDE_LINEIN(sideMixer, g)
#define FF_AE_SIDE_LINEIN_MUTE()
#define FF_AE_SIDE_PAIR_ZERO(ch, follower)
#endif

#ifdef FEATURE_SEND_DUCK
#define FF_AE_SEND_DUCK_CONNECT() connectSendDuck()
// off until a project load says otherwise (takes the stream out of the update list)
#define FF_AE_SEND_DUCK_OFF() sendDuck.set(DuckParams{ 0, 0, 0, 0 })
#else
#define FF_AE_SEND_DUCK_CONNECT() ((void)0)
#define FF_AE_SEND_DUCK_OFF() ((void)0)
#endif

#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
// the synth whose patch bank the Patch and Morph FX use (mtAudioEngine_patchfx.inc)
#define FF_AE_PATCH_BANK_SYNTH(s) patchBankSynth = (s)
#else
#define FF_AE_PATCH_BANK_SYNTH(s) ((void)0)
#endif

#ifdef FEATURE_PART_AUTO
// updateSynthParams, audio interrupt: the kPart automation of the part first (mtAudioEngine_partauto.inc)
#define FF_AE_PART_AUTO(synthIdx) updateSynthAuto(synthIdx)
#else
#define FF_AE_PART_AUTO(synthIdx) ((void)0)
#endif
