// mtAudioEngine.cpp, included where this block was: the kPart destinations of the synth automation (FEATURE_PART_AUTO), once per block in the audio interrupt
#ifdef FEATURE_PART_AUTO
// kPart destinations of the synth automation, once per block in the audio interrupt (flag stores only, like refreshSendsInIrq). The engine
// params of the open synth come in ModPart order (pan, gain, reverb, delay), the SynthParam of each is in this table.
static const audioEngine::SynthParam kAutoParam[partauto::kParts] = { audioEngine::SynthParam::kPanning,
                                                                                audioEngine::SynthParam::kVolume,
                                                                                audioEngine::SynthParam::kReverbSend,
                                                                                audioEngine::SynthParam::kDelaySend };

AUDIO_CORE_RAM void audioEngine::updateSynthAuto(int synthIdx)
{
    partauto::tick(synthIdx, currentTempo); // Tracker+ and Mini: the Synth Automation slots set the store (the open stack: nothing)
    float v[partauto::kParts];
    const uint8_t mask = partauto::read(synthIdx, v);
    if (mask == 0 && synthAutoMask[synthIdx] == 0)
    {
        return; // the common case: no part slot now, none before
    }
    for (int n = 0; n < partauto::kParts; n++)
    {
        const SynthParam param = kAutoParam[n];
        auto& slot = synthParams[synthIdx][(int)param][(int)SynthValuesSource::kAuto];
        if (mask & (1u << n))
        {
            float x = v[n];
            if (n == partauto::kGain)
            {
                // Gain: the part gain is a powf per write (dbToAmplitude, and in post fader mode the two sends again), so the offset moves in
                // 0.1 dB steps and an unchanged step costs nothing (an LFO of a tempo synced speed holds one step for several blocks)
                x = floorf(x * 10.0f + 0.5f) * 0.1f;
            }
            if (!slot.has_value() || synthAutoLast[synthIdx][n] != x)
            {
                synthAutoLast[synthIdx][n] = x;
                slot = x;
                isSynthParamDirty[synthIdx][(int)param] = true;
            }
        }
        else if (synthAutoMask[synthIdx] & (1u << n))
        {
            slot = std::nullopt;
            isSynthParamDirty[synthIdx][(int)param] = true;
        }
    }
    synthAutoMask[synthIdx] = mask;
}

void audioEngine::resetSynthAuto()
{
    DISABLE_AUDIO_INTERRUPTS();
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        for (int n = 0; n < partauto::kParts; n++)
        {
            synthParams[synthIdx][(int)kAutoParam[n]][(int)SynthValuesSource::kAuto] = std::nullopt;
            isSynthParamDirty[synthIdx][(int)kAutoParam[n]] = true;
        }
        synthAutoMask[synthIdx] = 0;
    }
    partauto::clearAll(); // the store of Tracker+ and Mini holds the old project's destinations (the open stack: nothing to clear)
    ENABLE_AUDIO_INTERRUPTS();
}
#endif
