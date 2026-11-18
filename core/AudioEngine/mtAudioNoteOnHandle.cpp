#include "ampLogarythmicValues.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "polyassert.h"
#include "tapeFx.h"

using namespace mtAudioEngineConstans;

//********************************** NOTE_ON
uint8_t playerEngine ::noteOn(uint8_t instr_idx, int8_t note, int8_t _velocity)
{
    if (mtProject.instrument[instr_idx].isActive != 1)
        return 0;
    if (isBlockedNoteOn[instr_idx])
        return 0;
    //	engine.clearDelay();

    endFx(lastSeqFx[0], 0);
    endFx(lastSeqFx[1], 1);
    if (lastSeqFx[2] | lastSeqFx[3]) // ranks 2 and 3 only ever hold an FX with the 4 FX switch
    {
        endFx(lastSeqFx[2], 2);
        endFx(lastSeqFx[3], 3);
    }

    //	__disable_irq();
    AudioNoInterrupts();

    uint8_t status;

    currentInstrument_idx = instr_idx;
    currentNote = note;
    stepVelocity = _velocity;

    handleInitNoteOnAllEnvelopes(); // kill + init

    handleNoteOnFilter();

    handleNoteOnGain();

    handleNoteOnPanning();

    handleNoteOnDelaySend();

    handleNoteOnReverbSend();

    handleNoteOnBitCrusher();

    status = 1; // playMemPtr->play(instr_idx,note);
    if (isTrackDisplayed)
        onEndDisplay = true;

    bool isAmpRandom = (mtProject.instrument[instr_idx].envelope[envAmp].loop == 1) &&
                       (mtProject.instrument[instr_idx].lfo[envAmp].shape == lfoShapeRandom);
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeSetIsRandom(isAmpRandom);
        playMemPtr[channel]->envelopeNoteOn(instr_idx, note);
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        if (mtProject.instrument[instr_idx].isEnvEnabled(i))
        {
            bool isRandom = (mtProject.instrument[instr_idx].envelope[i].loop == 1) &&
                            (mtProject.instrument[instr_idx].lfo[i].shape == lfoShapeRandom);
            envelopePtr[i]->setIsRandom(isRandom);
            envelopePtr[i]->setWavetableParams(
              mtProject.instrument[instr_idx].lfo[i].shape,
              mtProject.instrument[instr_idx].lfo[i].wtBend,
              mtProject.instrument[instr_idx].lfo[i].wtSust);
            envelopePtr[i]->start();
        }
    }

    //	__enable_irq();
    AudioInterrupts();
    return status;
}
//**********************************
//********************************** NOTE_ON FX
AUDIO_CORE_RAM uint8_t playerEngine ::noteOn(uint8_t instr_idx,
                                             int8_t note,
                                             int8_t velocity,
                                             uint8_t fx1_id,
                                             uint8_t fx1_val,
                                             uint8_t fx2_id,
                                             uint8_t fx2_val,
                                             uint8_t fx3_id,
                                             uint8_t fx3_val,
                                             uint8_t fx4_id,
                                             uint8_t fx4_val)
{
    if (mtProject.instrument[instr_idx].isActive != 1)
        return 0;
    if (isBlockedNoteOn[instr_idx])
        return 0;
    //	__disable_irq();
    AudioNoInterrupts();
    uint8_t status;

    currentInstrument_idx = instr_idx;
    currentNote = note;
    stepVelocity = velocity;

    handleInitFxNoteOnAllEnvelopes();

    handleFxNoteOnFilter();

    handleFxNoteOnGain();

    handleFxNoteOnPanning();

    handleFxNoteOnBitDepth();

    handleFxNoteOnDelaySend();

    handleFxNoteOnReverbSend();

    //********* obsluga performance parametrow obslugiwanych w play_memory
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint])
    {
        changeEndPointPerformanceMode(
          std::roundf(mapClamped<float>((float)performanceMod.endPoint,
                                        (float)-MAX_16BIT,
                                        (float)MAX_16BIT,
                                        -100.0f,
                                        100.0f))); // zalatanie na szybko
    }
    if (trackControlParameter[(int)controlType::performanceMode][(
          int)parameterList::startPoint]) // tu nie ma mapowania bo posredniczy change position
    {
        changeStartPointPerformanceMode(performanceMod.startPoint);
    }
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::wavetablePosition])
    {
        changeWavetableWindowPerformanceMode(performanceMod.wavetablePosition);
    }
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune])
    {
        changeTunePerformanceMode(performanceMod.tune);
    }
    //******** seqFx
    if (velocity >= 0)
    {
        if (isFxVelocity(fx1_id))
        {
            fx1_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx1_val = velocity;
        }

        if (isFxVelocity(fx2_id))
        {
            fx2_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx2_val = velocity;
        }

        if (isFxVelocity(fx3_id))
        {
            fx3_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx3_val = velocity;
        }

        if (isFxVelocity(fx4_id))
        {
            fx4_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;
            fx4_val = velocity;
        }
    }

    // the least significant first. Ranks 2 and 3 are left alone (not even an end of the last FX) while no FX ever sat there
    if (fx4_id | lastSeqFx[3])
        seqFx(fx4_id, fx4_val, 3);
    if (fx3_id | lastSeqFx[2])
        seqFx(fx3_id, fx3_val, 2);
    seqFx(fx2_id, fx2_val, 1); // mniej wazny najpierw
    seqFx(fx1_id, fx1_val, 0);

    bool isFx1Slide = (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                      (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
    bool isFx2Slide = (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                      (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);

    if (!isFx1Slide && !isFx2Slide && (fx3_id | fx4_id) &&
        fx1_id != Sequencer::strFxConsts::FX_TYPE_GLIDE)
    {
        // a Slide in rank 2 or 3 (the 4 FX switch): the more significant of the two plays when slots 1 and 2 hold none
        const bool isFx3Slide = (fx3_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                                (fx3_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
        const bool isFx4Slide = (fx4_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP) ||
                                (fx4_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
        if (isFx3Slide || isFx4Slide)
        {
            const uint8_t slideId = isFx3Slide ? fx3_id : fx4_id;
            const uint8_t slideVal = isFx3Slide ? fx3_val : fx4_val;
            for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSlideOnPlay((slideId == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP),
                                                    slideVal,
                                                    sequencer.getStepPeriodMs());
            }
        }
    }

    if (isFx1Slide || isFx2Slide)
    {
        bool isFx1Glide = (fx1_id == Sequencer::strFxConsts::FX_TYPE_GLIDE);
        if (isFx1Slide)
        {
            for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSlideOnPlay(
                  (fx1_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP),
                  fx1_val,
                  sequencer.getStepPeriodMs());
            }
        }
        else if (!isFx1Glide && isFx2Slide)
        {
            for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setSlideOnPlay(
                  (fx2_id == Sequencer::strFxConsts::FX_TYPE_SLIDE_UP),
                  fx2_val,
                  sequencer.getStepPeriodMs());
            }
        }
    }

    // Tape on the step of the note (value 1..64 Stop, 65..128 Start): the note starts the run (AudioPlayMemory::play), the first of the slots wins
    {
        const bool tape1 = (fx1_id == Sequencer::strFxConsts::FX_TYPE_TAPE);
        const bool tape2 = (fx2_id == Sequencer::strFxConsts::FX_TYPE_TAPE);
        const bool tape3 = (fx3_id == Sequencer::strFxConsts::FX_TYPE_TAPE);
        const bool tape4 = (fx4_id == Sequencer::strFxConsts::FX_TYPE_TAPE);
        if (tape1 || tape2 || tape3 || tape4)
        {
            const uint8_t tapeVal = tape1 ? fx1_val : tape2 ? fx2_val : tape3 ? fx3_val : fx4_val;
            const uint32_t frames = tapefx::lengthFrames(tapefx::lengthMs(tapeVal, sequencer.getStepPeriodMs()));
            for (int channel = 0; channel < playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setTapeOnPlay(tapefx::isStart(tapeVal), frames);
            }
        }
    }

    bool isAmpRandom = (mtProject.instrument[instr_idx].envelope[envAmp].loop == 1) &&
                       (mtProject.instrument[instr_idx].lfo[envAmp].shape == lfoShapeRandom);

    for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeSetIsRandom(isAmpRandom);
        playMemPtr[channel]->envelopeNoteOn(instr_idx,
                                            note); // zawsze odpalamy nawet jak nie aktywny
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        if (isActiveEnvelope(i))
        {
            bool isRandom = (mtProject.instrument[instr_idx].envelope[i].loop == 1) &&
                            (mtProject.instrument[instr_idx].lfo[i].shape == lfoShapeRandom);
            envelopePtr[i]->setIsRandom(isRandom);
            if (i != envCutoff ||
                (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::filterAttack] &&
                 !trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::filterAttack]))
            {
                envelopePtr[i]->setWavetableParams(
                  mtProject.instrument[instr_idx].lfo[i].shape,
                  mtProject.instrument[instr_idx].lfo[i].wtBend,
                  mtProject.instrument[instr_idx].lfo[i].wtSust);
            }
            envelopePtr[i]->start();
            setSyncParamsLFO(i);
        }
    }
    //*******

    //*******
    status = 1;
    if (isTrackDisplayed)
        onEndDisplay = true;
    //******* start env
    //	__enable_irq();
    AudioInterrupts();
    return status;
}

//********************************** NOTE OFF
//@formatter:off
AUDIO_CORE_RAM void playerEngine::noteOff(int8_t option)
{
    switch (option)
    {
        case Sequencer::STEP_NOTE_OFF_FADE: noteOffFade(); break;
        case Sequencer::STEP_NOTE_OFF_CUT: noteOffCut(); break;
        case Sequencer::STEP_NOTE_OFF_DEFAULT: noteOffOrdinary(); break;
        default: noteOffOrdinary(); break;
    }
}
//@formatter:on
//
//************************************* handle noteOff
AUDIO_CORE_RAM void playerEngine::noteOffFade()
{
    //	__disable_irq();
    AudioNoInterrupts();
    for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeRelease(300);
        playMemPtr[channel]->envelopeNoteOff();
    }
    // lfo ma dzialac do konca fade

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[i].loop;
        if ((curLoop == 0 || curLoop == 2) &&
            (!trackControlParameter[(int)controlType::sequencerMode][i]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2][i]) &&
            (!trackControlParameter[(int)controlType::performanceMode][i]))
        {
            envelopePtr[i]->stop();
        }
    }

    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_RAM void playerEngine::noteOffCut()
{
    //	__disable_irq();
    AudioNoInterrupts();
    // caly voice i automatyka zabijane
    for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeNoteOff();
        playMemPtr[channel]->envelopeSetIdle();
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->stop();
        envelopePtr[i]->killToZero();
    }

    for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->stop();
    }

    if (isTrackDisplayed)
        onEndDisplay = true;

    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_RAM void playerEngine::noteOffOrdinary()
{
    //	__disable_irq();
    AudioNoInterrupts();

    for (int channel = 0; size_t(channel) < playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeNoteOff();
    }

    uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
    if ((curAmpLoop == 1 || curAmpLoop == 3) ||
        (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp]) ||
        (trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp]) ||
        (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp]))
    {
        // amp loop nie ma release wiec wszystko zabijamy od razu
        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            envelopePtr[i]->stop();
        }
    }
    else
    {
        // env ampa nie zabija od razu loopow - maja grac do konca env

        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[i].loop;
            if ((curLoop == 0 || curLoop == 2) &&
                (!trackControlParameter[(int)controlType::sequencerMode][i]) &&
                (!trackControlParameter[(int)controlType::sequencerMode2][i]) &&
                (!trackControlParameter[(int)controlType::performanceMode][i]))
            {
                envelopePtr[i]->stop();
            }
        }
    }

    if (!mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        //		playMemPtr->stop();
        if (isTrackDisplayed)
            onEndDisplay = true;
        for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[i].loop;
            if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(i) &&
                 (curLoop == 1 || curLoop == 3)) ||
                (trackControlParameter[(int)controlType::sequencerMode]
                                      [envelopesControlValue[i]]) ||
                (trackControlParameter[(int)controlType::sequencerMode2]
                                      [envelopesControlValue[i]]) ||
                (trackControlParameter[(int)controlType::performanceMode]
                                      [envelopesControlValue[i]]))
            {
                currentEnvelopeModification[i] = 0;
            }
        }
    }
    else
    {
        if ((curAmpLoop == 0 &&
             mtProject.instrument[currentInstrument_idx].envelope[envAmp].release == 0.0f) ||
            (envelopePassFlag) ||
            (curAmpLoop == 1 || curAmpLoop == 3))
        {
            //			playMemPtr->stop();
            if (isTrackDisplayed)
                onEndDisplay = true;
            for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
            {
                uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[i].loop;
                if ((mtProject.instrument[currentInstrument_idx].isEnvEnabled(i) &&
                     (curLoop == 1 || curLoop == 3)) ||
                    (trackControlParameter[(int)controlType::sequencerMode]
                                          [envelopesControlValue[i]]) ||
                    (trackControlParameter[(int)controlType::sequencerMode2]
                                          [envelopesControlValue[i]]) ||
                    (trackControlParameter[(int)controlType::performanceMode]
                                          [envelopesControlValue[i]]))
                {
                    currentEnvelopeModification[i] = 0;
                }
            }
        }
    }
    AudioInterrupts();
    //    __enable_irq();
}

//************************************* handle noteOn
AUDIO_CORE_RAM void playerEngine::handleInitNoteOnAllEnvelopes()
{
    // envAmp killuje sie wewnetrznie
    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->kill();
    }

    handleInitNoteOnEnvelope(envAmp);

    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        handleInitNoteOnEnvelope(envCutoff);
    }

    if (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable)
    {
        handleInitNoteOnEnvelope(envWtPos);
    }

    if (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    {
        handleInitNoteOnEnvelope(envGranPos);
        handleInitNoteOnEnvelope(envGranLen);
    }
    handleInitNoteOnEnvelope(envPan);
    handleInitNoteOnEnvelope(envFinetune);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnFilter()
{
    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        filterConnect();

        for (int channel = 0; channel < (int)filterPtr.size(); channel++)
        {
            filterPtr[channel]->resonance(mtProject.instrument[currentInstrument_idx].resonance +
                                          RESONANCE_OFFSET);
        }
        if (!isActiveEnvelope(envCutoff))
            modCutoff(mtProject.instrument[currentInstrument_idx].cutOff);

        changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
    }
    else if (!mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        filterDisconnect();
    }

    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->setOverdriveAmount(
          mtProject.instrument[currentInstrument_idx].overdrive);
    }
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnGain()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying())
    {
        for (int channel = 0; channel < (int)ampPtr.size(); channel++)
        {
            ampPtr[channel]->gain(0.0f); // start approximation from 0 point if track isn't audible
            ampPtr[channel]->gain(0.0f); // twice to be sure that previous and current values are
                                         // cleared (even with synchronization problems)
        }
        // todo: proper solution is setting playMemPtr and ampPtr parameters in the same place
        // (update in audio thread after the seq and before the processing) and force this gain once
    }

    setRefreshBit(VOLUME_MASK);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnPanning()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
#ifndef PLATFORM_IMX
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::left);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::left);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::right);
        monoToStereoPtr->gain(1.0f, MonoToStereo::Channel::right);
#else
        engine.getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kLeft)->gain(nChannel, 1.0f);
        engine.getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kRight)->gain(nChannel, 1.0f);
#endif
    }

    setRefreshBit(PANNING_MASK);
}

void playerEngine::handleNoteOnBitCrusher()
{
    modBitDepth(mtProject.instrument[currentInstrument_idx].bitDepth);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnDelaySend()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
        preDelayMixer->gain(nChannel, 0.0f);
        preDelayMixer->gain(nChannel, 0.0f);
    }
    setRefreshBit(DELAY_SEND_MASK);
}

AUDIO_CORE_RAM void playerEngine::handleNoteOnReverbSend()
{
    assert(playMemPtr[0] != nullptr);
    if (!playMemPtr[0]->isPlaying()) // todo: see handleNoteOnGain() description
    {
        prePolyverbMixer->gain(nChannel, 0.0f);
        prePolyverbMixer->gain(nChannel, 0.0f);
    }

    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::handleInitNoteOnAmpEnvelope()
{
    if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        if (curAmpLoop == 1 || curAmpLoop == 3)
        {
            calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                                 &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                                 mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed,
                                 1,
                                 curAmpLoop);

            initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
            setSyncParamsLFO(envAmp);
        }
        else
        {
            initEnvelopesParameters(envAmp,
                                    &mtProject.instrument[currentInstrument_idx].envelope[envAmp]);
        }
    }
    else
    {
        initEnvelopesParameters(envAmp, (envelopeGenerator::strEnv*)&passEnvelope);
    }
}

void playerEngine::handleInitNoteOnEnvelope(uint8_t n)
{
    if (n == envAmp)
    {
        handleInitNoteOnAmpEnvelope();
    }
    else
    {
        currentEnvelopeModification[n] = 0;
        if (mtProject.instrument[currentInstrument_idx].isEnvEnabled(n))
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[n].loop;
            if (curLoop == 1 || curLoop == 3)
            {
                calcLfoBasedEnvelope(&lfoBasedEnvelope[n],
                                     &mtProject.instrument[currentInstrument_idx].lfo[n],
                                     mtProject.instrument[currentInstrument_idx].lfo[n].speed,
                                     0,
                                     curLoop);
                initEnvelopesParameters(n, &lfoBasedEnvelope[n]);
                setSyncParamsLFO(n);
            }
            else
            {
                initEnvelopesParameters(n,
                                        &mtProject.instrument[currentInstrument_idx].envelope[n]);
            }
        }
    }
}
//*************************************** FX NOTE ON
void playerEngine::handleInitFxNoteOnEnvelope(uint8_t n)
{
    if (n == envAmp)
    {
        handleInitFxNoteOnAmpEnvelope();
    }
    else
    {
        if ((!trackControlParameter[(int)controlType::sequencerMode][envelopesControlValue[n]]) &&
            (!trackControlParameter[(int)controlType::sequencerMode2][envelopesControlValue[n]]) &&
            (!trackControlParameter[(int)controlType::performanceMode][envelopesControlValue[n]]))
        {
            handleInitNoteOnEnvelope(n);
        }

        if (n == envCutoff &&
            (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::filterAttack] ||
             trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::filterAttack]))
        {
            uint8_t curFilterLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
            if (curFilterLoop >= 2)
            {
                envelopePtr[envCutoff]->setWavetableDurationMs(currentSeqModValues.filterAttack);
            }
            else
            {
                envelopePtr[envCutoff]->putMidiParameter(0, 41, (currentSeqModValues.filterAttack / 1000) * 8);
                envelopePtr[envCutoff]->putMidiParameter(0, 43, (currentSeqModValues.filterAttack / 1000) * 8);
                envelopePtr[envCutoff]->putMidiParameter(0, 45, (currentSeqModValues.filterRelease / 1000) * 8);
            }
        }

        if (mtProject.instrument[currentInstrument_idx].isEnvEnabled(n))
        {
            uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[n].loop;
            if (curLoop == 1 || curLoop == 3)
            {
                if (trackControlParameter[(int)controlType::performanceMode][n])
                {
                    switch (n)
                    {
                        case envCutoff:
                            changeCutoffLfoRatePerformanceMode(performanceMod.lfoCutoffRate);
                            break;
                        case envGranPos:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
                            break;
                        case envWtPos:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
                            break;
                        case envPan:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoPanningRate);
                            break;
                        case envFinetune:
                            changePositionLfoRatePerformanceMode(performanceMod.lfoFinetuneRate);
                            break;
                    }
                }
            }
        }
    }
}
void playerEngine::handleInitFxNoteOnAmpEnvelope()
{
    if ((!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp]) &&
        (!trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp]))
    {
        handleInitNoteOnAmpEnvelope();
    }
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::ampAttack] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::ampAttack])
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeAttack(currentSeqModValues.ampAttack);
            playMemPtr[channel]->envelopeDecay(curAmpLoop >= 2 ? currentSeqModValues.ampAttack : currentSeqModValues.ampRelease);
            playMemPtr[channel]->envelopeRelease(currentSeqModValues.ampRelease);
        }
    }

    if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        if (curAmpLoop == 1 || curAmpLoop == 3)
        {
            if (trackControlParameter[(int)controlType::performanceMode]
                                     [(int)parameterList::lfoAmp])
            {
                changeCutoffLfoRatePerformanceMode(performanceMod.lfoAmpRate);
            }
        }
    }
}
AUDIO_CORE_RAM void playerEngine::handleInitFxNoteOnAllEnvelopes()
{
    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->kill();
    }

    handleInitFxNoteOnEnvelope(envAmp);

    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        handleInitFxNoteOnEnvelope(envCutoff);
    }

    if (mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWavetable)
    {
        handleInitFxNoteOnEnvelope(envWtPos);
    }

    if ((mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWaveFile) &&
        (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular))
    {
        handleInitFxNoteOnEnvelope(envGranPos);
        handleInitFxNoteOnEnvelope(envGranLen);
    }
    handleInitFxNoteOnEnvelope(envPan);
    handleInitFxNoteOnEnvelope(envFinetune);
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnFilter()
{
    //***** resonance
    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        for (int channel = 0; size_t(channel) < filterPtr.size(); channel++)
        {
            filterPtr[channel]->resonance(mtProject.instrument[currentInstrument_idx].resonance +
                                          RESONANCE_OFFSET);
        }
    }
    //*****
    //***** cutoff + enable

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff])
    {
        changeCutoffPerformanceMode(performanceMod.cutoff); // włączenie filtra jest w środku
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::filterCutoff]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::filterCutoff]))
    {
        filterConnect();
        if (!isActiveEnvelope(envCutoff))
            modCutoff(currentSeqModValues.filterCutoff);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].filterEnable)
        {
            filterConnect();
            if (!isActiveEnvelope(envCutoff))
                modCutoff(mtProject.instrument[currentInstrument_idx].cutOff);
        }
        else if (!mtProject.instrument[currentInstrument_idx].filterEnable)
            filterDisconnect();
    }
    //*****
    //***** filter type
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType])
    {
        changeFilterTypePerformanceMode(performanceMod.filterType);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::filterType]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::filterType]))
    {
        changeFilterType(currentSeqModValues.filterType);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].filterEnable)
        {
            changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
        }
    }
    //*****

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive])
    {
        changeOverdrivePerformanceMode(performanceMod.overdrive);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::overdrive]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::overdrive]))
    {
        modOverdrive(currentSeqModValues.overdrive);
    }
    else
    {
        modOverdrive(mtProject.instrument[currentInstrument_idx].overdrive);
    }
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnGain()
{
    handleNoteOnGain();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnPanning()
{
    handleNoteOnPanning();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnBitDepth()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth])
    {
        changeBitDepthPerformanceMode(performanceMod.bitDepth);
    }
    else if ((trackControlParameter[(int)controlType::sequencerMode]
                                   [(int)parameterList::bitDepth]) ||
             (trackControlParameter[(int)controlType::sequencerMode2]
                                   [(int)parameterList::bitDepth]))
    {
        modBitDepth(currentSeqModValues.bitDepth);
    }
    else
    {
        modBitDepth(mtProject.instrument[currentInstrument_idx].bitDepth);
    }
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnDelaySend()
{
    handleNoteOnDelaySend();
}

AUDIO_CORE_RAM void playerEngine::handleFxNoteOnReverbSend()
{
    handleNoteOnReverbSend();
}
