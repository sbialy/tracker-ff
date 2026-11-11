#ifdef FEATURE_SYNTH

#include "synth_PolySynth.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "PolySynth/Instrument.h"
#include "PolySynth/Synth.h"
#include "Synth.h"
#include "config.h"
#include <span>
#ifdef FEATURE_SYNTH_OPEN
#include "open_load.h"
#include "synth_part_clip.h"
#endif
//#include "Audio.h"
//#include "dspinst.h'
//#include "util_math.h"
//#include <cr_section_macros.h>

AUDIO_CORE_RAM void PolySynth::update(void)
{
    if (isBlocked)
    {
        return;
    }

    static_assert(std::is_same<polysynth::sample_t, float>::value);

    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonRight;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderRight;

    static std::array<std::array<polysynth::sample_t, polysynth::kAudioBufferSize>, 2> tempBufSend;

    for (int partIdx = 0; partIdx < kSynthParts; partIdx++)
    {
        auto& part = trackerSynth->parts[partIdx];

        if (part.instrument->getVoiceCount() == 0)
        {
            if (!AudioStream::renderMode)
            {
                continue;
            }

            audio_block_t* blockZero = allocate();
            if (blockZero == NULL)
            {
                return;
            }
            std::fill(std::begin(blockZero->data), std::end(blockZero->data), 0);
            transmit(blockZero, partIdx);
            transmit(blockZero, kRightChannelOffset + partIdx);
            transmit(blockZero, kSend1Offset + partIdx);
            transmit(blockZero, kSend2Offset + partIdx);
            release(blockZero);
            continue;
        }

#ifdef FEATURE_SYNTH_OPEN
        // A part with voices allowed but nothing sounding (the default: 1 voice per part) used to render, soft clip and
        // send blocks of zeros through its amplifiers, analysers and mixers every block. Transmit nothing: a missing
        // block is silence downstream. Offline render keeps the full path (stems are written block by block).
        if (!AudioStream::renderMode && polysynth::open::partIdle(partIdx))
        {
            continue;
        }
#endif

        bool isStereo =
          part.patch != nullptr && part.patch->getMetadata().outputChannels ==
                                     polysynth::Patch::Metadata::OutputChannels::kStereo;

        bool hasSends = part.patch != nullptr && part.patch->getMetadata().rendersAuxSends;

        const bool reuseUnmodifiedMono = (!hasSends && !isStereo && gainDB[partIdx] == 0.0f);

        // Both pre FX send gains of the part are 0 (target and previous, the default): the mixers release the send blocks unread, so
        // the part does not build them (soft clip and int16 conversion). Not for a part with aux sends, not while the send block is also
        // the post fader signal (0 dB), and not in offline render (stems are written block by block).
        bool skipSendBlocks = false;
#ifdef AUDIO_NULL_IS_SILENCE
        skipSendBlocks = !hasSends && !reuseUnmodifiedMono && !AudioStream::renderMode && sendMixer[0] != nullptr &&
                         sendMixer[1] != nullptr &&
                         sendMixer[0]->isInputSilenced(firstSendInput + partIdx) &&
                         sendMixer[1]->isInputSilenced(firstSendInput + partIdx);
#endif

        audio_block_t* blockPostFaderLeft = allocate();
        audio_block_t* blockPostFaderRight = isStereo ? allocate() : NULL;
        audio_block_t* blockSendsCommonOrSend1 = skipSendBlocks ? NULL : allocate();
        audio_block_t* blockSend2 = hasSends ? allocate() : NULL;

        if (blockPostFaderLeft == NULL || (isStereo && blockPostFaderRight == NULL) ||
            (!skipSendBlocks && blockSendsCommonOrSend1 == NULL) || (hasSends && blockSend2 == NULL))
        {
            if (blockPostFaderLeft != NULL)
            {
                release(blockPostFaderLeft);
            }

            if (blockPostFaderRight != NULL)
            {
                release(blockPostFaderRight);
            }

            if (blockSendsCommonOrSend1 != NULL)
            {
                release(blockSendsCommonOrSend1);
            }

            if (blockSend2 != NULL)
            {
                release(blockSend2);
            }

            return;
        }

        std::array<polysynth::AudioBufferRef, 2> tempSendsBufArray = { tempBufSend[0],
                                                                       tempBufSend[1] };

        trackerSynth->synth->renderAudioBuffer(
          { tempBufSendsCommonLeft, tempBufSendsCommonRight },
          std::span<polysynth::AudioBufferRef>(tempSendsBufArray),
          partIdx);

#ifdef FEATURE_SYNTH_OPEN
        if (!hasSends && !isStereo)
        {
            // Mono part: soft clip and int16 in one pass straight from the render buffer (synth_part_clip.h), no float copy for the post
            // fader, no second pass, no memcpy of the int16 block. Same blocks as the general path below.
            static_assert(kSynthParts == 3, "the gain cache is initialised for 3 parts");
            static part_clip::GainCache<kSynthParts> gainCache = { { __builtin_nanf(""), __builtin_nanf(""), __builtin_nanf("") },
                                                                   { 0.0f, 0.0f, 0.0f } };
            const float postGainAmp =
              reuseUnmodifiedMono
                ? 1.0f
                : gainCache.get(partIdx, gainDB[partIdx], [](float db) { return polysynth::sdk::dbToAmplitude(db); });
            part_clip::monoPart<polysynth::kAudioBufferSize>(tempBufSendsCommonLeft.begin(),
                                                             postGainAmp,
                                                             reuseUnmodifiedMono,
                                                             skipSendBlocks,
                                                             skipSendBlocks ? nullptr : blockSendsCommonOrSend1->data,
                                                             blockPostFaderLeft->data);
            if (!skipSendBlocks)
            {
                transmit(blockSendsCommonOrSend1, kSend1Offset + partIdx);
                transmit(blockSendsCommonOrSend1, kSend2Offset + partIdx);
                release(blockSendsCommonOrSend1);
            }
            transmit(blockPostFaderLeft, partIdx);
            transmit(blockPostFaderLeft, kRightChannelOffset + partIdx);
            release(blockPostFaderLeft);
            continue;
        }
#endif

        if (!reuseUnmodifiedMono)
        {
            tempBufPostfaderLeft = tempBufSendsCommonLeft;
            if (isStereo)
            {
                tempBufPostfaderRight = tempBufSendsCommonRight;
            }
        }

        polysynth::AudioBufferRef tempBufRef = tempBufSendsCommonLeft;

        assert(hasSends ==
               isStereo); // Assumption to simplify handling. The only one stereo and with internal
                          // synths is PERC, the other are mono and without sends. Refactor the
                          // handling if needed for new synth.

        if (hasSends)
        {
            auto* srcSend1 = tempBufSend[0].begin();
            auto* srcSend2 = tempBufSend[1].begin();

            auto* destSend1 = blockSendsCommonOrSend1->data;
            auto* destSend2 = blockSend2->data;

            for (int i = 0; i < polysynth::kAudioBufferSize; i++)
            {
                *(destSend1++) = static_cast<int16_t>(*(srcSend1++) * INT16_MAX);
                *(destSend2++) = static_cast<int16_t>(*(srcSend2++) * INT16_MAX);
            }
        }
        else if (!skipSendBlocks)
        {

            if (isStereo)
            {
                for (int idx = 0; idx < polysynth::kAudioBufferSize; idx++)
                {
                    tempBufSendsCommonLeft[idx] =
                      (tempBufSendsCommonLeft[idx] + tempBufSendsCommonRight[idx]) / 2.0f;
                }
            }

            polysynth::sdk::saturateSoftClip(tempBufRef, 0.0f);

            auto* src = tempBufSendsCommonLeft.begin();
            auto* dest = blockSendsCommonOrSend1->data;

            for (int i = 0; i < polysynth::kAudioBufferSize; i++)
            {
                *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
            }
        }

        if (reuseUnmodifiedMono)
        {
            memcpy(blockPostFaderLeft->data,
                   blockSendsCommonOrSend1->data,
                   sizeof(blockPostFaderLeft->data));
        }

        if (!skipSendBlocks)
        {
            transmit(blockSendsCommonOrSend1, kSend1Offset + partIdx);
            transmit(hasSends ? blockSend2 : blockSendsCommonOrSend1, kSend2Offset + partIdx);
            release(blockSendsCommonOrSend1);
        }
        if (blockSend2 != NULL)
        {
            release(blockSend2);
        }

        if (!reuseUnmodifiedMono)
        {
            tempBufRef = tempBufPostfaderLeft;
            polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);

            if (isStereo)
            {
                tempBufRef = tempBufPostfaderRight;
                polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);
            }

            if (isStereo)
            {
                auto* srcL = tempBufPostfaderLeft.begin();
                auto* srcR = tempBufPostfaderRight.begin();

                auto* destL = blockPostFaderLeft->data;
                auto* destR = blockPostFaderRight->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(destL++) = static_cast<int16_t>(*(srcL++) * INT16_MAX);
                    *(destR++) = static_cast<int16_t>(*(srcR++) * INT16_MAX);
                }
            }
            else
            {
                auto* src = tempBufPostfaderLeft.begin();
                auto* dest = blockPostFaderLeft->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
                }
            }
        }

        transmit(blockPostFaderLeft, partIdx);
        transmit(isStereo ? blockPostFaderRight : blockPostFaderLeft,
                 kRightChannelOffset + partIdx);
        release(blockPostFaderLeft);
        if (blockPostFaderRight != NULL)
        {
            release(blockPostFaderRight);
        }
    }
}

void PolySynth::init(Synth* trackerSynth)
{
    PolySynth::trackerSynth = trackerSynth;
    isBlocked = false;
}

AUDIO_CORE_FLASH void PolySynth::setGain(int idx, float gainDB)
{
    assert(idx < kSynthParts);
    PolySynth::gainDB[idx] = gainDB;
}

#endif
