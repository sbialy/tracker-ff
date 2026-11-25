// mtAudioEngine.cpp, included where this block was: the Patch and Morph step FX into the synth's patch bank (FEATURE_SYNTH_OPEN or FEATURE_SYNTH_PATCHFX), audio callback
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
// Audio callback (Sequencer::tick): synth patch change FX, bank slot -> part values. Ignored without a synth or a slot.
AUDIO_CORE_RAM void audioEngine::seqPatchChange(uint8_t slot, uint8_t synthIdx)
{
    if (patchBankSynth != nullptr)
    {
        patchBankSynth->applyPatchBankSlot(synthIdx, slot);
    }
}
#endif
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)

// Audio callback: Morph FX, blend of the open slot and `slot` (-1: the part's Morph target) by amount 0..100
AUDIO_CORE_RAM void audioEngine::seqPatchMorph(int8_t slot, uint8_t amount, uint8_t synthIdx)
{
    if (patchBankSynth != nullptr)
    {
        patchBankSynth->applyPatchMorph(synthIdx, slot, amount);
    }
}
#endif
