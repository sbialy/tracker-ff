

#include "mtAudioEngine.h"
#include "stereoPairs.h"
#include "mtSequencer.h"

#include "performanceFx.h"

#include "performanceEngine.h"

// INSTRUKCJA DODAWANIA FXa
// 1. Dodaj odpowiednią pozycje enuma w odpowiednim jego miejscu
// 2. Powiel blok tworzący obiekt dowolnego dzialającego juz fxa
// 3. W nowo utworzonym:
//	  - Pozmieniaj nazwy zmiennych/funkcji na odpowiednie
// 	  - Przypisz prawidlowe funkcje i parametry struktury
//    - przypisz wartosc enuma i nazwe do wyswietlania w interfejsie

// enum okreslajacy kolejnosic i ilosc efektow oraz niepowtarzalne id efektu
enum
{
    mtPerfFxNone,
    //------------------------- \/ ---
    mtPerfFxVolume,
    mtPerfPanning,
    mtPerfTune,
    mtPerfLowPass,
    mtPerfHighPass,
    mtPerfBandPass,
    mtPerfDelaySend,
    mtPerfReverbSend,
    mtPerfSamplePosition,
    mtPerfSampleEnd,
    mtPerfSamplePlayback,
    mtPerfVolumeLfoSpeed,
    mtPerfPanningLfoSpeed,
    mtPerfFinetuneLfoSpeed,
    mtPerfFilterfoSpeed,
    mtPerfGranWtfoSpeed,
    mtPerfStepStutter,
    mtPerfPatternPlayMode,
    mtPerfPatternLength,
    mtPerfBitDepth,
    mtPerfOverdrive,

#ifdef FEATURE_SYNTH
    mtPerfSynth1ReverbSend,
    mtPerfSynth2ReverbSend,
    mtPerfSynth3ReverbSend,

    mtPerfSynth1DelaySend,
    mtPerfSynth2DelaySend,
    mtPerfSynth3DelaySend,

    mtPerfSynth1Macro1,
    mtPerfSynth1Macro2,
    mtPerfSynth1Macro3,
    mtPerfSynth1Macro4,
    mtPerfSynth1Macro5,
    mtPerfSynth1Macro6,

    mtPerfSynth2Macro1,
    mtPerfSynth2Macro2,
    mtPerfSynth2Macro3,
    mtPerfSynth2Macro4,
    mtPerfSynth2Macro5,
    mtPerfSynth2Macro6,

    mtPerfSynth3Macro1,
    mtPerfSynth3Macro2,
    mtPerfSynth3Macro3,
    mtPerfSynth3Macro4,
    mtPerfSynth3Macro5,
    mtPerfSynth3Macro6,
#endif
    //--- dodwac efekty powyzej /\ ---
    performanceFxesCount
};

// A linked stereo pair is one voice: a Performance effect on its second track acts on the leader (stereoPairs.h)
static inline playerEngine& pairPlayer(uint8_t track)
{
    return instrumentPlayer[isSampleTrack(track) ? stereopairs::base(track) : track];
}

cPerformEngine performance(performanceFxesCount, performanceDataSource, performanceDataSourceCount);

//##################################################################################
//##################################################################################
//########## NONE ########################################################################
void noneSet([[maybe_unused]] uint8_t track, [[maybe_unused]] int16_t value)
{
}
void noneClear([[maybe_unused]] uint8_t track)
{
}
const fxSetup noneSetup = { -100, 100, nullptr };

cPerformFx nonePerfFx(mtPerfFxNone, "", noneSet, noneClear, &noneSetup);

//########## VOLUME ########################################################################
void volumeSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeVolumePerformanceMode(value);
    }
    sequencer.setPerformanceMidiVolume(track, value);
}
void volumeClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endVolumePerformanceMode();
    }
    sequencer.setPerformanceMidiVolume(track, 0);
}
const fxSetup volumeSetup = { -100, 100, nullptr };

cPerformFx volumePerfFx(mtPerfFxVolume, "Volume/Velocity", volumeSet, volumeClear, &volumeSetup);

//########## PANNING ########################################################################
void panningSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changePanningPerformanceMode(value);
    }
}
void panningClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endPanningPerformanceMode();
    }
}
const fxSetup panningSetup = { -100, 100, nullptr };

cPerformFx panningPerfFx(mtPerfPanning, "Sample panning", panningSet, panningClear, &panningSetup);

//########## BIT DEPTH ######################################################################
void bitDepthSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeBitDepthPerformanceMode(value);
    }
}
void bitDepthClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endBitDepthPerformanceMode();
    }
}
const fxSetup bitDepthSetup = { -12, 12, nullptr };

cPerformFx bitDepthPerfFx(mtPerfBitDepth,
                          "Sample bit depth",
                          bitDepthSet,
                          bitDepthClear,
                          &bitDepthSetup);

//########## OVERDRIVE ######################################################################
void overdriveSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeOverdrivePerformanceMode(value);
    }
}
void overdriveClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endOverdrivePerformanceMode();
    }
}
const fxSetup overdriveSetup = { -100, 100, nullptr };

cPerformFx overdrivePerfFx(mtPerfOverdrive,
                           "Sample overdrive",
                           overdriveSet,
                           overdriveClear,
                           &overdriveSetup);

//########## TUNE ########################################################################
void tuneSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeTunePerformanceMode(value);
    }
    sequencer.setPerformanceMidiTune(track, value);
}
void tuneClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endTunePerformanceMode();
    }
    sequencer.setPerformanceMidiTune(track, 0);
}
const fxSetup tuneSetup = { -48, 48, nullptr };

cPerformFx tunePerfFx(mtPerfTune, "Tune", tuneSet, tuneClear, &tuneSetup);

//########## FILTER ########################################################################
void lowPassSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeFilterTypePerformanceMode(1);
        pairPlayer(track).changeCutoffPerformanceMode(value);
    }
}
void highPassSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeFilterTypePerformanceMode(2);
        pairPlayer(track).changeCutoffPerformanceMode(value);
    }
}
void bandPassSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeFilterTypePerformanceMode(3);
        pairPlayer(track).changeCutoffPerformanceMode(value);
    }
}
void filterClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endCutoffPerformanceMode();
        pairPlayer(track).endFilterTypePerformanceMode();
    }
}
const fxSetup lowPassSetup = { -100, 100, nullptr, 0, mtPerfHighPass, mtPerfBandPass };
const fxSetup highPassSetup = { -100, 100, nullptr, 0, mtPerfLowPass, mtPerfBandPass };
const fxSetup bandPassSetup = { -100, 100, nullptr, 0, mtPerfLowPass, mtPerfHighPass };

cPerformFx lowPassPerfFx(mtPerfLowPass, "Sample LP cutoff", lowPassSet, filterClear, &lowPassSetup);
cPerformFx highPassPerfFx(mtPerfHighPass,
                          "Sample HP cutoff",
                          highPassSet,
                          filterClear,
                          &highPassSetup);
cPerformFx bandPassPerfFx(mtPerfBandPass,
                          "Sample BP cutoff",
                          bandPassSet,
                          filterClear,
                          &bandPassSetup);

//########## DELAY ########################################################################
void delaySet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeDelaySendPerformanceMode(value);
    }
}
void delayClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endDelaySendPerformanceMode();
    }
}
const fxSetup delaySetup = { -100, 100, nullptr };

cPerformFx delayPerfFx(mtPerfDelaySend, "Sample delay send", delaySet, delayClear, &delaySetup);

//########## REVERB ########################################################################
void reverbSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeReverbSendPerformanceMode(value);
    }
}
void reverbClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endReverbSendPerformanceMode();
    }
}
const fxSetup reverbSetup = { -100, 100, nullptr };

cPerformFx reverbPerfFx(mtPerfReverbSend,
                        "Sample reverb snd",
                        reverbSet,
                        reverbClear,
                        &reverbSetup);

//########## SAMPLE POS ########################################################################
void samplePosSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changePositionPerformanceMode(value);
    }
}
void samplePosClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endPositionPerformanceMode();
    }
}
const fxSetup samplePosSetup = { -100, 100, nullptr };

cPerformFx samplePosPerfFx(mtPerfSamplePosition,
                           "Sample position",
                           samplePosSet,
                           samplePosClear,
                           &samplePosSetup);

//########## SAMPLE END ########################################################################
void sampleEndSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeEndPointPerformanceMode(value);
    }
}
void sampleEndClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endEndPointPerformanceMode();
    }
}
const fxSetup sampleEndSetup = { -100, 100, nullptr };

cPerformFx sampleEndPerfFx(mtPerfSampleEnd,
                           "Sample end",
                           sampleEndSet,
                           sampleEndClear,
                           &sampleEndSetup);

//########## SYNTH STUFF #############################################################

#ifdef FEATURE_SYNTH
const fxSetup synthFxSetup = { .min = -100, .max = 100, .isGlobalFx = true };

cPerformFx synth1ReverbSend(
  mtPerfSynth1ReverbSend,
  "S1 reverb send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 0;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 0;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

cPerformFx synth2ReverbSend(
  mtPerfSynth2ReverbSend,
  "S2 reverb send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 1;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 1;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

cPerformFx synth3ReverbSend(
  mtPerfSynth3ReverbSend,
  "S3 reverb send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 2;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 2;
      const auto paramIdx = audioEngine::SynthParam::kReverbSend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

// delay
cPerformFx synth1DelaySend(
  mtPerfSynth1DelaySend,
  "S1 delay send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 0;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 0;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

cPerformFx synth2DelaySend(
  mtPerfSynth2DelaySend,
  "S2 delay send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 1;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 1;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

cPerformFx synth3DelaySend(
  mtPerfSynth3DelaySend,
  "S3 delay send",
  [](uint8_t, int16_t value)
  {
      const int synthIdx = 2;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;

      float paramValue = map<float>(value,
                                    synthFxSetup.min,
                                    synthFxSetup.max,
                                    -audioEngine::synthParamsBoundaries[paramIdx].max,
                                    audioEngine::synthParamsBoundaries[paramIdx].max);
      engine.setSynthParam(
        paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
  },
  [](uint8_t)
  {
      const int synthIdx = 2;
      const auto paramIdx = audioEngine::SynthParam::kDelaySend;
      engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
  },
  &synthFxSetup);

template<int synthIdx, int macroIdx>
void setSynthMacro(uint8_t, int16_t value)
{
    const auto paramIdx = audioEngine::SynthParam(audioEngine::SynthParam::kMacro1 + macroIdx);
    float paramValue = map<float>(value,
                                  synthFxSetup.min,
                                  synthFxSetup.max,
                                  -audioEngine::synthParamsBoundaries[paramIdx].max,
                                  audioEngine::synthParamsBoundaries[paramIdx].max);
    engine.setSynthParam(
      paramIdx, synthIdx, paramValue, audioEngine::SynthValuesSource::kPerformance);
};
template<int synthIdx, int macroIdx>
void resetSynthMacro(uint8_t)
{
    const auto paramIdx = audioEngine::SynthParam(audioEngine::SynthParam::kMacro1 + macroIdx);
    engine.cleanSynthParam(paramIdx, synthIdx, audioEngine::SynthValuesSource::kPerformance);
};

cPerformFx synth1Macro1(mtPerfSynth1Macro1,
                        "S1 macro 1",
                        setSynthMacro<0, 0>,
                        resetSynthMacro<0, 0>,
                        &synthFxSetup);
cPerformFx synth1Macro2(mtPerfSynth1Macro2,
                        "S1 macro 2",
                        setSynthMacro<0, 1>,
                        resetSynthMacro<0, 1>,
                        &synthFxSetup);
cPerformFx synth1Macro3(mtPerfSynth1Macro3,
                        "S1 macro 3",
                        setSynthMacro<0, 2>,
                        resetSynthMacro<0, 2>,
                        &synthFxSetup);
cPerformFx synth1Macro4(mtPerfSynth1Macro4,
                        "S1 macro 4",
                        setSynthMacro<0, 3>,
                        resetSynthMacro<0, 3>,
                        &synthFxSetup);
cPerformFx synth1Macro5(mtPerfSynth1Macro5,
                        "S1 macro 5",
                        setSynthMacro<0, 4>,
                        resetSynthMacro<0, 4>,
                        &synthFxSetup);
cPerformFx synth1Macro6(mtPerfSynth1Macro6,
                        "S1 macro 6",
                        setSynthMacro<0, 5>,
                        resetSynthMacro<0, 5>,
                        &synthFxSetup);

cPerformFx synth2Macro1(mtPerfSynth2Macro1,
                        "S2 macro 1",
                        setSynthMacro<1, 0>,
                        resetSynthMacro<1, 0>,
                        &synthFxSetup);
cPerformFx synth2Macro2(mtPerfSynth2Macro2,
                        "S2 macro 2",
                        setSynthMacro<1, 1>,
                        resetSynthMacro<1, 1>,
                        &synthFxSetup);
cPerformFx synth2Macro3(mtPerfSynth2Macro3,
                        "S2 macro 3",
                        setSynthMacro<1, 2>,
                        resetSynthMacro<1, 2>,
                        &synthFxSetup);
cPerformFx synth2Macro4(mtPerfSynth2Macro4,
                        "S2 macro 4",
                        setSynthMacro<1, 3>,
                        resetSynthMacro<1, 3>,
                        &synthFxSetup);
cPerformFx synth2Macro5(mtPerfSynth2Macro5,
                        "S2 macro 5",
                        setSynthMacro<1, 4>,
                        resetSynthMacro<1, 4>,
                        &synthFxSetup);
cPerformFx synth2Macro6(mtPerfSynth2Macro6,
                        "S2 macro 6",
                        setSynthMacro<1, 5>,
                        resetSynthMacro<1, 5>,
                        &synthFxSetup);

cPerformFx synth3Macro1(mtPerfSynth3Macro1,
                        "S3 macro 1",
                        setSynthMacro<2, 0>,
                        resetSynthMacro<2, 0>,
                        &synthFxSetup);
cPerformFx synth3Macro2(mtPerfSynth3Macro2,
                        "S3 macro 2",
                        setSynthMacro<2, 1>,
                        resetSynthMacro<2, 1>,
                        &synthFxSetup);
cPerformFx synth3Macro3(mtPerfSynth3Macro3,
                        "S3 macro 3",
                        setSynthMacro<2, 2>,
                        resetSynthMacro<2, 2>,
                        &synthFxSetup);
cPerformFx synth3Macro4(mtPerfSynth3Macro4,
                        "S3 macro 4",
                        setSynthMacro<2, 3>,
                        resetSynthMacro<2, 3>,
                        &synthFxSetup);
cPerformFx synth3Macro5(mtPerfSynth3Macro5,
                        "S3 macro 5",
                        setSynthMacro<2, 4>,
                        resetSynthMacro<2, 4>,
                        &synthFxSetup);
cPerformFx synth3Macro6(mtPerfSynth3Macro6,
                        "S3 macro 6",
                        setSynthMacro<2, 5>,
                        resetSynthMacro<2, 5>,
                        &synthFxSetup);

#endif

//########## SAMPLE PLAYBACK
//########################################################################
void samplePlaybackSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeSamplePlaybackPerformanceMode(value);
    }
}
void samplePlaybackClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endSamplePlaybackPerformanceMode();
    }
}
const char samplePlaybackPerfLabels[2][4] = { ">>>", "<<<" };
const fxSetup samplePlaybackSetup = { 0, 1, &samplePlaybackPerfLabels[0][0], 4 };

cPerformFx samplePlaybackPerfFx(mtPerfSamplePlayback,
                                "Sample playback",
                                samplePlaybackSet,
                                samplePlaybackClear,
                                &samplePlaybackSetup);

//########## VOLUME LFO SPEED
//########################################################################
void volumeLfoSpeedSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeAmpLfoRatePerformanceMode(value);
    }
}
void volumeLfoSpeedClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endAmpLfoRatePerformanceMode();
    }
}
const fxSetup volumeLfoSpeedSetup = { -CONSTRAIN_SPEED_AMP_ENVELOPE,
                                      CONSTRAIN_SPEED_AMP_ENVELOPE,
                                      nullptr };

cPerformFx volumeLfoSpeedPerfFx(mtPerfVolumeLfoSpeed,
                                "Volume LFO speed",
                                volumeLfoSpeedSet,
                                volumeLfoSpeedClear,
                                &volumeLfoSpeedSetup);

//########## PANNING LFO SPEED
//########################################################################
void panningLfoSpeedSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changePanningLfoRatePerformanceMode(value);
    }
}
void panningLfoSpeedClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endPanningLfoRatePerformanceMode();
    }
}
const fxSetup panningLfoSpeedSetup = { -CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                       CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                       nullptr };

cPerformFx panningLfoSpeedPerfFx(mtPerfPanningLfoSpeed,
                                 "Panning LFO speed",
                                 panningLfoSpeedSet,
                                 panningLfoSpeedClear,
                                 &panningLfoSpeedSetup);

//########## PITCH LFO SPEED
//########################################################################
void finetuneLfoSpeedSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeFinetuneLfoRatePerformanceMode(value);
    }
}
void finetuneLfoSpeedClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endFinetuneLfoRatePerformanceMode();
    }
}
const fxSetup finetuneLfoSpeedSetup = { -CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                        CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                        nullptr };

cPerformFx finetuneLfoSpeedPerfFx(mtPerfFinetuneLfoSpeed,
                                  "Finetune LFO spd",
                                  finetuneLfoSpeedSet,
                                  finetuneLfoSpeedClear,
                                  &finetuneLfoSpeedSetup);

//########## FILTER LFO SPEED
//########################################################################
void filterLfoSpeedSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changeCutoffLfoRatePerformanceMode(value);
    }
}
void filterLfoSpeedClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endCutoffLfoRatePerformanceMode();
    }
}
const fxSetup filterLfoSpeedSetup = { -CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                      CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                      nullptr };

cPerformFx filterLfoSpeedPerfFx(mtPerfFilterfoSpeed,
                                "Filter LFO speed",
                                filterLfoSpeedSet,
                                filterLfoSpeedClear,
                                &filterLfoSpeedSetup);

//########## GRAN/WT LFO SPEED
//########################################################################
void granWtLfoSpeedSet(uint8_t track, int16_t value)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).changePositionLfoRatePerformanceMode(value);
    }
}
void granWtLfoSpeedClear(uint8_t track)
{
    if (isSampleTrack(track))
    {
        pairPlayer(track).endPositionLfoRatePerformanceMode();
    }
}
const fxSetup granWtLfoSpeedSetup = { -CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                      CONSTRAIN_SPEED_OTHER_ENVELOPE,
                                      nullptr };

cPerformFx granWtLfoSpeedPerfFx(mtPerfGranWtfoSpeed,
                                "Gran/Wt LFO speed",
                                granWtLfoSpeedSet,
                                granWtLfoSpeedClear,
                                &granWtLfoSpeedSetup);

//########## STEP REPEATER ########################################################################
void stepRepeaterSet(uint8_t track, int16_t value)
{
    sequencer.setStepRepeater(track, value);
}
void stepRepeaterClear(uint8_t track)
{
    sequencer.setStepRepeater(track, 0);
}
const char stutterPerfLabels[16][5] = {
    "Off", "16",  "12",  "8",   "6",   "4",   "3",    "2",
    "1",   "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16",
};
const fxSetup stepRepeaterSetup = { Sequencer::strFxConsts::STUTTER_PERIOD_MIN,
                                    Sequencer::strFxConsts::STUTTER_PERIOD_MAX,
                                    &stutterPerfLabels[0][0],
                                    5 };

cPerformFx stepRepeaterPerfFx(mtPerfStepStutter,
                              "Step repeater",
                              stepRepeaterSet,
                              stepRepeaterClear,
                              &stepRepeaterSetup);

//########## PATTERN PLAY MODE
//########################################################################
void patternPlayModeSet(uint8_t track, int16_t value)
{
    sequencer.setPerformancePlayMode(track, value);
}
void patternPlayModeClear(uint8_t track)
{
    sequencer.setPerformancePlayMode(track, 0);
    sequencer.alignToGlobalPos(track);
}
const char patternPlayModePerfLabels[23][5] = {
    "Fwd", "Back", "Rnd", "C1",  "C2",  "C3",  "C4",  "C5",  "C6",  "C7",  "C8",  "C9",
    "C10", "C11",  "C12", "C13", "C14", "C15", "C16", "C17", "C18", "C19", "C20",
};
const fxSetup patternPlayModeSetup = { 0, 22, &patternPlayModePerfLabels[0][0], 5 };

cPerformFx patternPlayModePerfFx(mtPerfPatternPlayMode,
                                 "Pattern play mode",
                                 patternPlayModeSet,
                                 patternPlayModeClear,
                                 &patternPlayModeSetup);

//########## PATTERN LENGTH ########################################################################
void patternLengthSet([[maybe_unused]] uint8_t track, int16_t value)
{
    sequencer.setPerformancePatternLengthFromFxVal(value);
}
void patternLengthClear([[maybe_unused]] uint8_t track)
{
    sequencer.setPerformancePatternLengthFromFxVal(0);
}
const char patternLengthPerfLabels[9][4] = { "---", "1", "2", "4", "8", "16", "32", "64", "128" };
const fxSetup patternLengthSetup = { 0, 8, &patternLengthPerfLabels[0][0], 4, 0, 0, true };

cPerformFx patternLengthPerfFx(mtPerfPatternLength,
                               "Pattern length",
                               patternLengthSet,
                               patternLengthClear,
                               &patternLengthSetup);

//##########  ########################################################################

//##########  ########################################################################
