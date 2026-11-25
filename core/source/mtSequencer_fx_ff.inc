// mtSequencer.cpp, included inside the step FX switch of the audio tick where these cases were: the FX of tracker-ff (Patch, Morph, Snap, Tape)
#if defined(FEATURE_SYNTH) && (defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX))
                case fx.FX_TYPE_PATCH_CHANGE:
                    // one-shot, runs in the audio callback before the synth renders: copies a preloaded bank slot into the part
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;
                    if (isSynthInstrument(stepToSend.instrument))
                    {
                        int synthIdx = instrumentIdxToMidiChannel(stepToSend.instrument) - 1;
                        assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));

                        // next to a Morph FX the Patch FX only names the Morph target (the Morph case uses it)
                        if (stepToSend.fx[fxIndex ^ 1].type != fx.FX_TYPE_PATCH_MORPH)
                        {
                            engine.seqPatchChange(_fx.value, synthIdx);
                        }
                    }
                    break;
#endif
#if defined(FEATURE_SYNTH) && (defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX))
                case fx.FX_TYPE_PATCH_MORPH:
                    // one-shot like the Patch FX: the part's values become a blend of the open slot and the target slot
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;
                    if (isSynthInstrument(stepToSend.instrument))
                    {
                        int synthIdx = instrumentIdxToMidiChannel(stepToSend.instrument) - 1;
                        assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));

                        const auto& other = stepToSend.fx[fxIndex ^ 1];
                        engine.seqPatchMorph(other.type == fx.FX_TYPE_PATCH_CHANGE ? int8_t(other.value) : int8_t(-1), _fx.value, synthIdx);
                    }
                    break;
#endif
#ifdef FEATURE_FF_CORE
                case fx.FX_TYPE_SNAP:
                    // latches the scale snap mode of the track from this step (its own note included, the note goes out below) until the
                    // next Snap FX or the end of the pass. A Chance FX that gated it has turned the slot into no FX before this loop. It
                    // does not count as an FX of the step (atLeastOneFx), like a slot of any FX it replaces in the slot. Value 4 (Track)
                    // clears the latch: the track plays its project setting again from this step
                    killFxOnSlot(row, fxIndex);
                    playerRow.snapFx = _fx.value >= kSnapFxTrack ? kSnapFxNone : _fx.value;
                    break;
#endif
#if defined(FEATURE_SYNTH) && defined(FEATURE_SYNTH_OPEN)
                case fx.FX_TYPE_TAPE:
                    // one-shot on the part (audio callback, before the synth renders): the voices of the part run down or up. The
                    // note of the step, if any, takes the run. A sample track goes on to the player engine (note-on or seqFx)
                    if (isSynthInstrument(stepToSend.instrument))
                    {
                        killFxOnSlot(row, fxIndex);
                        atLeastOneFx = 1;

                        const int synthIdx = instrumentIdxToMidiChannel(stepToSend.instrument) - 1;
                        assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));
                        polysynth::open::partTape(synthIdx,
                                                  tapefx::isStart(_fx.value),
                                                  tapefx::lengthFrames(tapefx::lengthMs(_fx.value, getStepPeriodMs())));
                    }
                    break;
#endif
