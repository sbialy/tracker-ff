#include "core/interfaceDefs.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtChord.h"
#include "mtPatternGen.h"
#include "mtSequencer.h"
#include "mtTuning.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"
#include "stereoPairs.h"
#include "tapeFx.h"
#include "tables_format.h"

#include "ctype.h"
#include "util_math.h"
#include "fx34.h"
#include <cr_section_macros.h>

extern Sequencer sequencer;

__NOINIT(EXTERNAL_RAM) Sequencer::strPattern copyPastePattern;

void fromToSwap(int16_t& from, int16_t& to)
{
    if (to < from)
    {
        uint8_t buff;
        buff = from;
        from = to;
        to = buff;
    }
}

void Sequencer::fillRandomNotes(int16_t fillStep,
                                int16_t scale,
                                int16_t from,
                                int16_t to,
                                int16_t density)
{
    fromToSwap(from, to);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;
    uint8_t root = mtProject.values.padBoardRootNote;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, density))
            {
                if (step->note == STEP_NOTE_EMPTY)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                }
                for (uint8_t a = 0; a <= 100; a -= -1)
                {
                    step->note = utilMathRandom::random(from, to + 1);
                    if (isInScale(step->note, root, scale))
                        break;
                }
            }
        }
    }
}
void Sequencer::fillLinearNotes(int16_t fillStep,
                                int16_t scale,
                                int16_t from,
                                int16_t to,
                                int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;
    //	uint8_t scale = mtProject.values.padBoardScale;
    uint8_t root = mtProject.values.padBoardRootNote;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, density))
            {
                int newNote = round(mapClamped<float>(
                  (float)offset + sel->firstStep, sel->firstStep, sel->lastStep, from, to));

                for (uint8_t a = 0; a < 12; a++)
                {
                    if (isInScale(newNote, root, scale))
                        break;
                    newNote++;
                }
                newNote = clamp<int>(newNote, STEP_NOTE_MIN, STEP_NOTE_MAX);

                if (isInScale(newNote, root, scale))
                {
                    if (step->isNoteEmpty())
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    }
                    step->note = newNote;
                }
            }
        }
    }
}
// One idea per selected track by the role of the track (Auto: kick, snare, hat, bass, lead in selection order). The selection is
// replaced: steps the idea does not hit lose their note and its volume, the hits get the note, the volume and (when the step had no
// note) the instrument the other fills use. Note offs, FX and micro-move of the steps stay. One seed per apply, so Fill again gives
// another idea and Undo takes it back.
void Sequencer::fillRoleNotes(int16_t role, int16_t scale, int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    const uint16_t steps = sel->lastStep - sel->firstStep + 1;
    const uint32_t seed = (uint32_t)utilMathRandom::random();
    patternGen::Cell cells[MAXSTEP + 1];

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        const uint8_t r = patternGen::roleOfTrack((uint8_t)role, t - sel->firstTrack);
        const uint8_t low = r == patternGen::roleBass ? (isSampleTrack(t) ? 48 : 36) : 60;
        patternGen::generate(r,
                             steps,
                             (uint8_t)clamp<int>(density, 1, MAX_FILL_DENSITY),
                             mtProject.values.padBoardRootNote,
                             (uint8_t)clamp<int>(scale, MIN_SCALE, MAX_SCALE),
                             low,
                             seed + t * 0x9E3779B1u,
                             cells);
        for (uint16_t i = 0; i < steps; i++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[sel->firstStep + i];
            if (cells[i].hit)
            {
                if (step->isNoteEmpty() || step->isNoteOff())
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                }
                if (r == patternGen::roleBass || r == patternGen::roleLead)
                    step->note = cells[i].note;
                else if (step->isNoteEmpty() || step->isNoteOff())
                    step->note = STEP_NOTE_DEFAULT;
                step->volume = cells[i].volume;
            }
            else if (step->note >= STEP_NOTE_MIN)
            {
                step->note = STEP_NOTE_EMPTY;
                step->volume = STEP_VELO_DEFAULT;
            }
        }
    }
}

void Sequencer::fillLinearInstruments(int16_t fillStep, int16_t from, int16_t to, int16_t density)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];
            if (isStepToFillNote(step, offset, fillStep, density))
            {
                int newInstrument =
                  mapClamped<int>(offset + sel->firstStep, sel->firstStep, sel->lastStep, from, to);

                step->instrument =
                  changeInstrumentInSpecificOrder(newInstrument, 0, !isSampleTrack(t));

                if (step->note == STEP_NOTE_EMPTY && isSampleInstrument(step->instrument))
                {
                    step->note = STEP_NOTE_DEFAULT;
                }
            }
        }
    }
}

void Sequencer::fillLinearFx(int16_t fxIndex,
                             int16_t fillStep,
                             int16_t fxName,
                             int16_t fromVal,
                             int16_t toVal,
                             int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > fx34::maxSlot())
        fxIndex = 0;

    int16_t fxType = interfaceGlobals.fxNameToId(fxName);

    //	if(fromVal < 0 && toVal <= 0) toVal++;
    //	else if(fromVal >= 0 && toVal > 0) toVal++;

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillFx(step, offset, fxIndex, fillStep, density))
            {
                fx34::FxRef fxr(currentPattern, t, s, fxIndex);
                fxr.p->type = (fxType >= 0) ? fxType : generateRandomFx();

                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }

                fxr.p->value = clamp<int>((uint8_t)tempFloat, getFxMin(fxr.p->type), getFxMax(fxr.p->type));
            }
        }
    }
}

uint8_t Sequencer::generateRandomFx()
{
    uint8_t retVal;
    for (uint8_t a = 0; a < 100; a++)
    {
        // ids, not list positions: the retired id 51 (R10) is a hole, so the ids reach one further than the list (Snap 52 stays drawable)
        retVal = utilMathRandom::random(fx.FX_TYPE_OFF + 1, FX_COUNT - FX_COUNT_HIDDEN_FXes + 2);
        if (retVal != fx.FX_TYPE_TEMPO && retVal != fx.FX_TYPE_BREAK_PATTERN // breaka nie chcemy
            && retVal != fx.FX_TYPE_PATCH_CHANGE // a random patch switch is never wanted from the picker (no-op id in stock)
            && retVal != fx.FX_TYPE_R10         // the retired Tape Start id, a hole in the list
            && retVal != fx.FX_TYPE_TAPE        // a random pick would silence a track
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
            && retVal != fx.FX_TYPE_PATCH_MORPH  // nor a random morph
#endif
            && retVal != fx.FX_TYPE_STRUM)      // a strum needs a MIDI Chord in the other slot

            break;
    }
    return retVal;
}

void Sequencer::fillRandomFx(int16_t fxIndex,
                             int16_t fillStep,
                             int16_t fxName,
                             int16_t fromVal,
                             int16_t toVal,
                             int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > fx34::maxSlot())
        fxIndex = 0;

    int16_t fxType = interfaceGlobals.fxNameToId(fxName);

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillFx(step, offset, fxIndex, fillStep, density))
            {
                fx34::FxRef fxr(currentPattern, t, s, fxIndex);
                fxr.p->value = utilMathRandom::random(fromVal, toVal + 1);
                fxr.p->type = (fxType >= 0) ? fxType : generateRandomFx();

                fxr.p->value = clamp<int>(fxr.p->value, getFxMin(fxr.p->type), getFxMax(fxr.p->type));
            }
        }
    }
}

bool Sequencer::isEuclideanStep(uint8_t offset, int16_t density)
{
    int length = selection.lastStep - selection.firstStep + 1;
    float euclStepSize = (float)length / density;
    float nearestStep = round(offset / euclStepSize) * euclStepSize;
    float diff = nearestStep - (float)offset;
    return diff <= 0.5f && diff > -0.5f;
}

bool Sequencer::isStepToFillFx(strPattern::strTrack::strStep* step,
                               uint8_t offset,
                               uint8_t fxIndex,
                               int16_t fillStep,
                               int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;

    else if (fillStep == fillStepEuclidean)
    {
        return isEuclideanStep(offset, density);
    }

    else if (fillStep == fillStepNoFx && fx34::getFx(step, fxIndex).type == 0)
        return true;

    else if (fillStep == fillStepFx && fx34::getFx(step, fxIndex).type != 0)
        return true;

    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;

    else if (fillStep == fillStepNote && step->note >= 0)
        return true;

    return false;
}

bool Sequencer::isStepToFillProp(strPattern::strTrack::strStep* step,
                                 uint8_t offset,
                                 bool isPropSet,
                                 int16_t fillStep,
                                 int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;
    else if (fillStep == fillStepEuclidean)
        return isEuclideanStep(offset, density);
    else if (fillStep == fillStepNoFx && !isPropSet)
        return true;
    else if (fillStep == fillStepFx && isPropSet)
        return true;
    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;
    else if (fillStep == fillStepNote && step->note >= 0)
        return true;

    return false;
}

void Sequencer::fillLinearMicromove(int16_t fillStep,
                                    int16_t fromVal,
                                    int16_t toVal,
                                    int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->micromove >= 0, fillStep, density))
            {
                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }
                step->micromove = clamp<int>((int)roundf(tempFloat), 0, 100);
            }
        }
    }
}

void Sequencer::fillRandomMicromove(int16_t fillStep,
                                    int16_t fromVal,
                                    int16_t toVal,
                                    int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->micromove >= 0, fillStep, density))
            {
                step->micromove = clamp<int>(utilMathRandom::random(fromVal, toVal + 1), 0, 100);
            }
        }
    }
}

// Quantize (Fill type of the Micro-move column): the micromove of every picked step moves toward the grid by strength percent.
// 100 clears it (on the grid), 50 halves it, 0 changes nothing. A step without micromove is already on the grid and stays as it is.
void Sequencer::quantizeMicromove(int16_t fillStep, int16_t strength, int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    strength = clamp<int16_t>(strength, 0, 100);
    if (strength == 0)
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (step->micromove < 0 || !isStepToFillProp(step, offset, true, fillStep, density))
                continue;
            const int left = (step->micromove * (100 - strength) + 50) / 100;
            step->micromove = left > 0 ? int8_t(left) : int8_t(-1);
        }
    }
}

void Sequencer::fillLinearVolume(int16_t fillStep,
                                 int16_t fromVal,
                                 int16_t toVal,
                                 int16_t density)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->volume >= 0, fillStep, density))
            {
                float tempFloat = fromVal;
                if (sel->firstStep != sel->lastStep)
                {
                    tempFloat = mapClamped<float>((float)(offset + sel->firstStep),
                                                  sel->firstStep,
                                                  sel->lastStep,
                                                  fromVal,
                                                  toVal);
                }
                step->volume = clamp<int>((int)roundf(tempFloat), 0, 100);
            }
        }
    }
}

void Sequencer::fillRandomVolume(int16_t fillStep,
                                 int16_t fromVal,
                                 int16_t toVal,
                                 int16_t density)
{
    fromToSwap(fromVal, toVal);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            if (isStepToFillProp(step, offset, step->volume >= 0, fillStep, density))
            {
                step->volume = clamp<int>(utilMathRandom::random(fromVal, toVal + 1), 0, 100);
            }
        }
    }
}

bool Sequencer::isStepToFillNote(strPattern::strTrack::strStep* step,
                                 uint8_t offset,
                                 int16_t fillStep,
                                 int16_t density)
{
    if (fillStep == fillStepByStep && offset % density == 0)
        return true;
    else if (fillStep == fillStepRandom && utilMathRandom::random(0, MAX_FILL_DENSITY) < density)
        return true;

    else if (fillStep == fillStepEuclidean)
    {
        return isEuclideanStep(offset, density);
    }

    else if (fillStep == fillStepNoFx && step->fx[0].type == 0 && step->fx[1].type == 0 && !fx34::hasExtra(step))
        return true;

    else if (fillStep == fillStepFx && (step->fx[0].type != 0 || step->fx[1].type != 0 || fx34::hasExtra(step)))
        return true;

    else if (fillStep == fillStepNoNote && step->note < 0)
        return true;

    else if (fillStep == fillStepNote && step->note >= 0)
        return true;

    return false;
}

void Sequencer::fillRandomInstruments(int16_t fillStep, int16_t from, int16_t to, int16_t density)
{
    fromToSwap(from, to);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isStepToFillNote(step, offset, fillStep, density))
            {
                int newInstrument = utilMathRandom::random(from, to + 1);
                step->instrument =
                  changeInstrumentInSpecificOrder(newInstrument, 0, !isSampleTrack(t));

                if (step->note == STEP_NOTE_EMPTY && isSampleInstrument(step->instrument))
                {
                    step->note = STEP_NOTE_DEFAULT;
                }
            }
        }
    }
}

void Sequencer::invertSelectedSteps(uint8_t elements)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    strPattern::strTrack::strStep* stepA;
    strPattern::strTrack::strStep* stepB;
    strPattern::strTrack::strStep buffStep;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t a = sel->firstStep, b = sel->lastStep;
             a <= ((sel->lastStep - sel->firstStep) / 2) + sel->firstStep;
             a++, b--)
        {

            stepA = &currentPattern->track[t].step[a];
            stepB = &currentPattern->track[t].step[b];

            // the extra FX swap with the step (rank 2 / 3 elements swap only that slot)
            if (fx34::Set* xs = fx34::setOf(currentPattern))
            {
                if (elements == ELEMENTS_ALL_NO_PREFERENCES)
                    fx34::swapSteps(xs, t, a, b, 3);
                else if (elements == ELEMENTS_FX_RANK2)
                    fx34::swapSteps(xs, t, a, b, 1);
                else if (elements == ELEMENTS_FX_RANK3)
                    fx34::swapSteps(xs, t, a, b, 2);
            }

            switch (elements)
            {
                case ELEMENTS_ALL_NO_PREFERENCES:
                    buffStep = *stepA;
                    *stepA = *stepB;
                    *stepB = buffStep;
                    break;
                case ELEMENTS_FX1:

                    buffStep = *stepA;
                    stepA->fx[0] = stepB->fx[0];
                    stepB->fx[0] = buffStep.fx[0];

                    break;

                case ELEMENTS_FX2:

                    buffStep = *stepA;
                    stepA->fx[1] = stepB->fx[1];
                    stepB->fx[1] = buffStep.fx[1];

                    break;

                case ELEMENTS_MICROMOVE:

                    buffStep = *stepA;
                    stepA->micromove = stepB->micromove;
                    stepB->micromove = buffStep.micromove;

                    break;

                case ELEMENTS_VOLUME:

                    buffStep = *stepA;
                    stepA->volume = stepB->volume;
                    stepB->volume = buffStep.volume;

                    break;

                case ELEMENTS_INSTRUMENTS:
                case ELEMENTS_NOTES:

                    buffStep = *stepA;
                    stepA->note = stepB->note;
                    stepB->note = buffStep.note;
                    stepA->instrument = stepB->instrument;
                    stepB->instrument = buffStep.instrument;

                    break;
                default: break;
            }
        }
    }
}

uint16_t Sequencer::moveVmFxToFields(uint16_t* skipped)
{
    uint16_t moved = 0, left = 0;
    const uint8_t ranks = uint8_t(fx34::maxSlot() + 1);
    for (uint8_t t = 0; t < kAllTracks; t++)
    {
        for (uint8_t s = 0; s <= MAXSTEP; s++)
        {
            strPattern::strTrack::strStep& step = currentPattern->track[t].step[s];
            // the play loop walks the slots 0, 1, 2, 3 and the last V (or m) wins, the same order here
            for (uint8_t r = 0; r < ranks; r++)
            {
                fx34::FxRef fxr(currentPattern, t, s, r);
                const uint8_t type = fxr.p->type;
                if (type != fx.FX_TYPE_VOLUME && type != fx.FX_TYPE_MICROMOVE)
                    continue;
                const uint8_t partner = fx34::getFx(&step, r ^ 1).type;
                if (step.note < 0 || partner == fx.FX_TYPE_RANDOM_VALUE || partner == fx.FX_TYPE_COND)
                {
                    left++;   // no note: a held modulation. Random value / Chance FX next to it: it would lose its target
                    continue;
                }
                const int8_t value = int8_t(clamp<int>(fxr.p->value, 0, 100));
                if (type == fx.FX_TYPE_VOLUME)
                    step.volume = value;
                else
                    step.micromove = value;
                fxr.p->type = 0;
                fxr.p->value = 0;
                moved++;
            }
        }
    }
    if (skipped)
        *skipped = left;
    if (moved)
        newFileManager.setActualPatternStructChanged();
    return moved;
}

// podwajanie dlugosci patternu z zdublowaniem zawartosci
bool Sequencer::duplicatePattern()
{
    int oldPatternMaxStep = currentPattern->track[0].length;
    assert(oldPatternMaxStep <= MAXSTEP);

    if (oldPatternMaxStep > (Sequencer::MAXSTEP / 2))
        return false;

    int newPatternLength = ((oldPatternMaxStep + 1) * 2) - 1;

    // polymeter: a shorter track doubles its own length (its own steps repeat once), the rest as stock
    const bool poly = isPolymeter(currentPattern);
    uint8_t own[kAllTracks];
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
        own[trackIdx] = getTrackLastStep(currentPattern, trackIdx);

    // the extra FX double with the steps: refuse the whole operation when they do not fit, nothing is half done
    if (fx34::Set* xs = fx34::setOf(currentPattern))
    {
        uint32_t need = 0;
        for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            need += fx34::countRange(xs, trackIdx, 0, uint8_t((poly ? own[trackIdx] : oldPatternMaxStep)));
        if (need > fx34::room(xs))
        {
            fx34::noteRefusal();
            return false;
        }
    }

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        int trackLen = (poly ? own[trackIdx] : oldPatternMaxStep) + 1;
        currentPattern->track[trackIdx].length = newPatternLength;

        for (int stepIdx = 0; stepIdx < trackLen; stepIdx++)
        {
            currentPattern->track[trackIdx].step[stepIdx + trackLen] =
              currentPattern->track[trackIdx].step[stepIdx];
        }
        fx34::duplicateTrack(fx34::setOf(currentPattern), trackIdx, uint8_t(trackLen));
    }

    if (poly)
    {
        stampPolymeter(currentPattern);
        for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            setTrackOwnLastStep(currentPattern, trackIdx, own[trackIdx] * 2 + 1);
        normalizePolymeter(currentPattern);
    }

    return true;
}

// podwajanie dlugosci patternu z wstawieniem pustch stepów co 1 step
bool Sequencer::expandPattern()
{
    int patternLength = currentPattern->track[0].length;
    assert(patternLength <= MAXSTEP);

    if (patternLength > (Sequencer::MAXSTEP / 2))
        return false;

    const bool poly = isPolymeter(currentPattern);
    uint8_t own[kAllTracks];
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
        own[trackIdx] = getTrackLastStep(currentPattern, trackIdx);

    patternLength = ((patternLength + 1) * 2) - 1;

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        // step k goes to step 2k (the extra FX of the odd steps are empty, as the cleared steps)
        fx34::remapTrack(fx34::setOf(currentPattern), trackIdx, [patternLength](int st) { return 2 * st <= patternLength ? 2 * st : -1; });
        currentPattern->track[trackIdx].length = patternLength;

        for (int stepIdx = 0; stepIdx < patternLength; stepIdx += 2)
        {
            int srcStepIdx = (patternLength - stepIdx - 1) / 2;
            int destStepIdx = patternLength - stepIdx - 1;

            currentPattern->track[trackIdx].step[destStepIdx] =
              currentPattern->track[trackIdx].step[srcStepIdx];

            clearStep(&currentPattern->track[trackIdx].step[patternLength - stepIdx]);
        }
    }

    if (poly)
    {
        stampPolymeter(currentPattern);
        for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            setTrackOwnLastStep(currentPattern, trackIdx, (own[trackIdx] + 1) * 2 - 1);
        normalizePolymeter(currentPattern);
    }

    return true;
}

// dwukrotne zmniejszenie patternu poprzez skasowaniem co drugiego stepa
bool Sequencer::shrinkPattern()
{
    int patternMaxStep = currentPattern->track[0].length;
    if (patternMaxStep < 1)
        return false;

    const bool poly = isPolymeter(currentPattern);
    uint8_t own[kAllTracks];
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
        own[trackIdx] = getTrackLastStep(currentPattern, trackIdx);

    int newPatternMaxStep = patternMaxStep / 2;

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        int trackMaxStep = (patternMaxStep / 2) + 1;

        for (int s = 1, srcStep = 2; s < trackMaxStep; s++, srcStep += 2)
        {
            currentPattern->track[trackIdx].step[s] = currentPattern->track[trackIdx].step[srcStep];
        }
        // step 2k goes to step k, the odd steps and everything past the new length are dropped with the steps
        fx34::remapTrack(fx34::setOf(currentPattern), trackIdx, [trackMaxStep](int st) { return st == 0 ? 0 : ((st % 2 == 0 && st / 2 < trackMaxStep) ? st / 2 : -1); });

        uint8_t oldTrackLen = patternMaxStep + 1;

        for (uint8_t s = trackMaxStep; s < oldTrackLen; s++)
        {
            clearStep(&currentPattern->track[trackIdx].step[s]);
        }

        currentPattern->track[trackIdx].length = newPatternMaxStep;
    }

    if (poly)
    {
        stampPolymeter(currentPattern);
        for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            setTrackOwnLastStep(currentPattern, trackIdx, own[trackIdx] / 2);
        normalizePolymeter(currentPattern);
    }

    return true;
}

// ---- per-track pattern length (polymeter), see mtSequencer.h
void Sequencer::setTrackOwnLastStep(strPattern* p, int t, int last)
{
    if (t == 0)
        p->rezerwa2 = uint8_t(last + 1);
    else
        p->track[t].length = uint8_t(last);
}

void Sequencer::normalizePolymeter(strPattern* p)
{
    if (!isPolymeter(p))
        return;

    const uint8_t patternLast = p->track[0].length;
    bool differs = false;

    for (int t = 0; t < kAllTracks; t++)
    {
        const uint8_t last = getTrackLastStep(p, t);
        differs |= last != patternLast;
        if (t > 0)
            p->track[t].length = last;
    }
    // track 1: 0 = the pattern length
    const uint8_t last0 = getTrackLastStep(p, 0);
    p->rezerwa2 = last0 < patternLast ? uint8_t(last0 + 1) : 0;

    if (!differs)
    {
        p->rezerwa1 = 0;
        p->rezerwa3 = 0;
    }
}

bool Sequencer::setTrackLastStep(strPattern* p, int t, int last)
{
    if (t < 0 || t >= kAllTracks)
        return false;

    const uint8_t patternLast = p->track[0].length;
    last = clamp<int>(last, 0, patternLast);

    if (getTrackLastStep(p, t) == last)
        return false;

    if (!isPolymeter(p))
    {   // stale lengths of an unmarked pattern never count: every track starts at the pattern length
        for (strPattern::strTrack& track : p->track)
            track.length = patternLast;
        p->rezerwa2 = 0;
        p->rezerwa1 = kPolymeterMark;
        stampPolymeter(p);
    }

    setTrackOwnLastStep(p, t, last);
    normalizePolymeter(p);
    return true;
}

void Sequencer::setPatternLastStep(strPattern* p, int last)
{
    last = clamp<int>(last, 0, MAXSTEP);

    if (!isPolymeter(p))
    {   // stock: the pattern editor syncs tracks 2-8 only
        p->track[0].length = uint8_t(last);
        for (uint8_t i = 1; i < 8; i++)
            p->track[i].length = p->track[0].length;
        return;
    }

    const uint8_t oldLast = p->track[0].length;
    uint8_t own[kAllTracks];
    for (int t = 0; t < kAllTracks; t++)
        own[t] = getTrackLastStep(p, t);

    p->track[0].length = uint8_t(last);
    stampPolymeter(p);

    for (int t = 0; t < kAllTracks; t++)
        setTrackOwnLastStep(p, t, own[t] == oldLast ? last : std::min<int>(own[t], last));

    normalizePolymeter(p);
}

void Sequencer::changeSelectionNote(int16_t value)
{
    changeSelectionNote(value, false);
}
void Sequencer::changeSelectionNote(int16_t value, bool octaveShift)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool sometingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep* step;

    if (octaveShift)
        value = value * tuning::octaveKeys();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[t].step[s];

            if (isSingleSelection(sel))
            {
                if (step->note == STEP_NOTE_EMPTY && value > 0)
                {
                    if (isSampleInstrument(step->instrument))
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    }
                    step->note = STEP_NOTE_DEFAULT;

                    sometingWasChanged = true;
                }
                else if (step->note == STEP_NOTE_EMPTY)
                {
                    if (isSampleInstrument(step->instrument))
                    {
                        step->instrument = changeInstrumentInSpecificOrder(
                          mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    }
                    step->note = clamp<int>(step->note + value, -4, STEP_NOTE_MAX);

                    sometingWasChanged = true;
                }
                else
                {
                    step->note = clamp<int>(step->note + value, -4, STEP_NOTE_MAX);

                    mtProject.values.lastUsedNote = step->note;

                    sometingWasChanged = true;
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->note = clamp<int>(step->note + value, 0, STEP_NOTE_MAX);
                    sometingWasChanged = true;
                }
            }
        }
    }

    if (sometingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::changeSelectionFxValue(uint8_t fxIndex, int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > fx34::maxSlot())
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            fx34::FxRef fxr(currentPattern, t, s, fxIndex);
            _fx = fxr.p;

            if (isChanceFamily(_fx->type) && (value > 1 || value < -1))
            {
                if (value > 1)
                {
                    if (_fx->value < fx.CHANCE_PROB_MAX)
                        _fx->value = std::min<int>(_fx->value + value, fx.CHANCE_PROB_MAX);
                    else if (_fx->value == fx.CHANCE_PROB_MAX)
                        _fx->value = fx.CHANCE_GROUP_WITH_LAST;
                    else if (_fx->value == fx.CHANCE_GROUP_WITH_LAST)
                        _fx->value = fx.CHANCE_NOT_LAST;
                    else if (_fx->value == fx.CHANCE_NOT_LAST)
                        _fx->value = fx.CHANCE_PLAY_SKIP_START;
                    else
                        _fx->value = std::min<int>(_fx->value + fx.CHANCE_CYCLE_STEPS, fx.CHANCE_MAX);
                }
                else
                {
                    if (_fx->value <= fx.CHANCE_PROB_MAX)
                        _fx->value = std::max<int>(_fx->value + value, 0);
                    else if (_fx->value == fx.CHANCE_GROUP_WITH_LAST)
                        _fx->value = fx.CHANCE_PROB_MAX;
                    else if (_fx->value == fx.CHANCE_NOT_LAST)
                        _fx->value = fx.CHANCE_GROUP_WITH_LAST;
                    else if (_fx->value < fx.CHANCE_PLAY_SKIP_START + fx.CHANCE_CYCLE_STEPS)
                        _fx->value = fx.CHANCE_NOT_LAST;
                    else
                        _fx->value = _fx->value - fx.CHANCE_CYCLE_STEPS;
                }
            }
            else
            {
                _fx->value =
                  clamp<int>(_fx->value + value, getFxMin(_fx->type), getFxMax(_fx->type));
            }

            somethingWasChanged = true;

            if (!isMultiSelection() && _fx->type == 0)
            {
                _fx->type = interfaceGlobals.fxNameToId(patternEditor.getPreviousUsedFx());
                _fx->value = patternEditor.getPreviousUsedFxValue();
            }

            patternEditor.setPreviousUsedFxValue(_fx->value);
            patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(_fx->type));
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::setSelectionFxValueByPad(uint8_t fxIndex, int16_t pad)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxIndex > fx34::maxSlot())
        return;

    newFileManager.storePatternUndoRevision();
    bool sometinghWasChanged = false;

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            fx34::FxRef fxr(currentPattern, t, s, fxIndex);
            _fx = fxr.p;

            if (!isMultiSelection() && _fx->type == 0)
            {
                _fx->type = interfaceGlobals.fxNameToId(patternEditor.getPreviousUsedFx());
            }

            int lastHandledPad = std::min(47, getFxMax(_fx->type) - getFxMin(_fx->type));
            _fx->value = roundf(
              mapClamped<float>(pad, 0, lastHandledPad, getFxMin(_fx->type), getFxMax(_fx->type)));
            patternEditor.setPreviousUsedFxValue(_fx->value);

            sometinghWasChanged = true;
        }
    }

    if (sometinghWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::changeSelectionMicromove(int16_t value)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];

            if (step->micromove >= 0)
            {
                step->micromove = clamp<int>(step->micromove + value, 0, 100);
                somethingWasChanged = true;
            }
            else if (!isMultiSelection())
            {
                step->micromove = clamp<int>(value > 0 ? value : 0, 0, 100);
                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionMicromoveByPad(int16_t pad)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();
    bool somethingWasChanged = false;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            step->micromove = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            somethingWasChanged = true;
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::changeSelectionVolume(int16_t value)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];

            if (step->volume >= 0)
            {
                step->volume = clamp<int>(step->volume + value, 0, 100);
                somethingWasChanged = true;
            }
            else if (!isMultiSelection())
            {
                int baseVol = step->note >= 0 ? getInstrumentVolume(step->instrument) : 50;
                step->volume = clamp<int>(baseVol + value, 0, 100);
                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionVolumeByPad(int16_t pad)
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();
    bool somethingWasChanged = false;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            strPattern::strTrack::strStep* step = &currentPattern->track[t].step[s];
            step->volume = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            somethingWasChanged = true;
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::changeSelectionFxType(uint8_t index, int16_t value)
{

    value = clamp<int>(value, -1, 1);

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (index > fx34::maxSlot())
        return;

    bool somethingWasChanged = false;

    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep::strFx* _fx;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            fx34::FxRef fxr(currentPattern, t, s, index);
            _fx = fxr.p;

            if (_fx->type != fx.FX_TYPE_NONE)
            {
                uint8_t name = interfaceGlobals.fxIdToName(_fx->type);
                name += value;

                uint8_t previousType = _fx->type;

                _fx->type = clamp<int>(interfaceGlobals.fxNameToId(name), 0, fx.FX_TYPE_NNA); // the highest FX id (nna-fx: R13)

                if (previousType != _fx->type && !isMultiSelection())
                {
                    _fx->value = getFxDefault(_fx->type);
                }
                else
                {
                    _fx->value = clamp<int>(_fx->value, getFxMin(_fx->type), getFxMax(_fx->type));
                }

                patternEditor.setPreviousUsedFxValue(_fx->value);
                patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(_fx->type));

                somethingWasChanged = true;
            }
        }
    }

    if (somethingWasChanged)
        newFileManager.setActualPatternStructChanged();
}
void Sequencer::setSelectionFxType(uint8_t fxSlot, int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;
    if (fxSlot > fx34::maxSlot())
        return;

    newFileManager.storePatternUndoRevision();

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            fx34::FxRef fxr(currentPattern, t, s, fxSlot);
            strPattern::strTrack::strStep::strFx* fxp = fxr.p;

            fxp->type = value;

            if (fxp->type != patternEditor.getPreviousUsedFx() && !isMultiSelection())
            {
                fxp->value = getFxDefault(fxp->type);
            }
            else
            {
                fxp->value = clamp<int>(fxp->value, getFxMin(fxp->type), getFxMax(fxp->type));
            }

            patternEditor.setPreviousUsedFxValue(fxp->value);
            patternEditor.setPreviousUsedFx(interfaceGlobals.fxIdToName(fxp->type));
        }
    }

    newFileManager.setActualPatternStructChanged();
}

uint8_t Sequencer::changeInstrumentInSpecificOrder(int8_t actualValue,
                                                   int16_t delta,
                                                   bool midiTypeOnly)
{
    int instrumentCount =
      midiTypeOnly ? kTotalInstrumentsCount - kSampleInstrumentsCount : kTotalInstrumentsCount;

    if (midiTypeOnly && delta == 0 && isSampleInstrument(actualValue))
    {
        return MIDI_INSTRUMENTS_START_IDX;
    }

    uint8_t uiChangeInstrumentOrder[instrumentCount];
    [[maybe_unused]] bool initArray = [&]() -> bool
    {
        int pos = 0;
        for (pos = 0; pos < kMidiInstrumentsCount; pos++)
        {
            uiChangeInstrumentOrder[pos] =
              kSampleInstrumentsCount + kMidiInstrumentsCount - pos - 1;
        }

#ifdef FEATURE_SYNTH
        int synthInstrIdx = kSampleInstrumentsCount + kMidiInstrumentsCount;
        for (; pos < kMidiInstrumentsCount + kSynthInstrumentsCount; pos++, synthInstrIdx++)
        {
            uiChangeInstrumentOrder[pos] = synthInstrIdx;
        }
#endif

        int sampleInstrIdx = 0;
        for (; pos < instrumentCount; pos++, sampleInstrIdx++)
        {
            uiChangeInstrumentOrder[pos] = sampleInstrIdx;
        }
        return true;
    }();

    int instrumentPosInOrder = 0;
    for (uint8_t a = 0; a < instrumentCount; a++)
    {
        if (uiChangeInstrumentOrder[a] == actualValue)
        {
            instrumentPosInOrder = a;
            break;
        }
    }

    instrumentPosInOrder = clamp<int>(instrumentPosInOrder + delta, 0, instrumentCount - 1);

    return uiChangeInstrumentOrder[instrumentPosInOrder];
}

void Sequencer::changeSelectionInstrument(int16_t value)
{

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();

    bool somethingWasChanged = false;

    strPattern::strTrack::strStep* step;

    for (uint8_t trackIdx = sel->firstTrack; trackIdx <= sel->lastTrack; trackIdx = stereopairs::next(trackIdx))
    {
        for (uint8_t s = sel->firstStep, offset = 0; s <= sel->lastStep; s++, offset++)
        {
            step = &currentPattern->track[trackIdx].step[s];

            if (isSingleSelection(sel))
            {
                if (step->note >= 0)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      step->instrument, value, !isSampleTrack(trackIdx));

                    mtProject.values.lastUsedInstrument = step->instrument;

                    somethingWasChanged = true;
                }
                else if (step->note == STEP_NOTE_EMPTY)
                {
                    step->note = STEP_NOTE_DEFAULT;

                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(trackIdx));

                    mtProject.values.lastUsedInstrument = step->instrument;

                    somethingWasChanged = true;
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      step->instrument, value, !isSampleTrack(trackIdx));
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}
void Sequencer::setSelectionInstrument(int16_t value)
{
    assert(value < kTotalInstrumentsCount);

    strSelection* sel = &selection;
    bool isSingleSel = isSingleSelection(sel);
    if (!isSelectionCorrect(sel))
        return;

    newFileManager.storePatternUndoRevision();

    bool somethingWasChanged = false;

    strPattern::strTrack::strStep* step;

    for (int trackIdx = sel->firstTrack; trackIdx <= sel->lastTrack; trackIdx = stereopairs::next(trackIdx))
    {
        for (int stepIdx = sel->firstStep, offset = 0; stepIdx <= sel->lastStep;
             stepIdx++, offset++)
        {
            step = &currentPattern->track[trackIdx].step[stepIdx];

            if (isSingleSel)
            {
                if (isSampleInstrument(value))
                {
                    if (step->isNoteEmpty())
                    {
                        step->note = STEP_NOTE_DEFAULT;
                        somethingWasChanged = true;
                    }

                    if (step->note >= 0)
                    {
                        step->instrument =
                          changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                        somethingWasChanged = true;

                        mtProject.values.lastUsedInstrument = step->instrument;
                    }
                }
                else
                {
                    if (step->note >= -1)
                    {
                        step->instrument =
                          changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                        somethingWasChanged = true;

                        mtProject.values.lastUsedInstrument = step->instrument;
                    }
                }
            }
            else
            {
                if (step->note >= 0)
                {
                    step->instrument =
                      changeInstrumentInSpecificOrder(value, 0, !isSampleTrack(trackIdx));
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::setSelectionNote(int16_t value)
{
    value = isOutOfRange<int>(value, (int)STEP_NOTE_MIN, (int)STEP_NOTE_MAX)
              ? (int)STEP_NOTE_DEFAULT
              : value;

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool somethingWasChanged = false;
    newFileManager.storePatternUndoRevision();

    strPattern::strTrack::strStep* step;

    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            step = &currentPattern->track[t].step[s];

            if (isSingleSelection(sel))
            {
                if (step->isNoteEmpty())
                {
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(t));
                    mtProject.values.lastUsedInstrument = step->instrument;
                }
                step->note = value;

                somethingWasChanged = true;
            }
            else
            {
                if (step->note != STEP_NOTE_EMPTY)
                {
                    step->note = value;
                    somethingWasChanged = true;
                }
            }
        }
    }

    if (somethingWasChanged)
    {
        newFileManager.setActualPatternStructChanged();
        if (!isPlay())
        {
            playSelection();
        }
    }
}

void Sequencer::clearStep(strPattern::strTrack::strStep* step)
{
    clearStep(step, ELEMENTS_ALL_NO_PREFERENCES);
}

// The extra FX of a step that a whole step or an element clears (nothing while the switch is off)
static void clearStepExtras(Sequencer::strPattern::strTrack::strStep* step, uint8_t elements, bool notesClear)
{
    if (!fx34::enabled())
        return;
    const fx34::Pattern* p;
    uint8_t t, s;
    if (!fx34::locate(step, &p, &t, &s))
        return;
    fx34::Set* set = fx34::setOf(p);
    if (!set || set->n == 0)
        return;
    switch (elements)
    {
        case Sequencer::ELEMENTS_ALL_NO_PREFERENCES: fx34::clearStep(set, t, s); break;
        case Sequencer::ELEMENTS_FX_RANK2: fx34::put(set, t, s, 2, 0, 0); break;
        case Sequencer::ELEMENTS_FX_RANK3: fx34::put(set, t, s, 3, 0, 0); break;
        case Sequencer::ELEMENTS_NOTES:
            if (notesClear)
                fx34::clearStep(set, t, s);
            break;
        default: break;
    }
}

void Sequencer::clearStep(strPattern::strTrack::strStep* step, uint8_t elements)
{
    const bool notesClear = elements == ELEMENTS_NOTES && (isSampleInstrument(step->instrument) || step->note < STEP_NOTE_EMPTY);
    clearStepExtras(step, elements, notesClear);
    clearStepStored(step, elements);
}

void Sequencer::clearStepStored(strPattern::strTrack::strStep* step, uint8_t elements)
{
    switch (elements)
    {
        case ELEMENTS_ALL_NO_PREFERENCES:
            step->note = STEP_NOTE_EMPTY;
            step->instrument = 0;
            step->fx[0].type = 0;
            step->fx[1].type = 0;
            step->volume = STEP_VELO_DEFAULT;
            step->micromove = -1;
            break;
        case ELEMENTS_FX1: step->fx[0].type = 0; break;
        case ELEMENTS_FX2: step->fx[1].type = 0; break;
        case ELEMENTS_MICROMOVE: step->micromove = -1; break;
        case ELEMENTS_VOLUME: step->volume = STEP_VELO_DEFAULT; break;
        case ELEMENTS_INSTRUMENTS:
            step->note = STEP_NOTE_EMPTY;
            step->instrument = 0;
            break;
        case ELEMENTS_NOTES:
            if (isSampleInstrument(step->instrument) || step->note < STEP_NOTE_EMPTY)
            {
                step->instrument = 0;
                step->fx[0].type = 0;
                step->fx[1].type = 0;
                step->volume = STEP_VELO_DEFAULT;
                step->micromove = -1;
            }
            step->note = STEP_NOTE_EMPTY;
            break;
        default: break;
    }
}

void Sequencer::clearSelected()
{
    clearSelected(&selection, 0);
}
void Sequencer::clearSelected(uint8_t elements)
{
    clearSelected(&selection, elements);
}
void Sequencer::clearSelected(strSelection* selection, uint8_t elements)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t = stereopairs::next(t))
    {
        stopOpenNoteOnTrack(t);

        for (uint8_t s = selection->firstStep; s <= selection->lastStep; s++)
        {
            clearStep(&currentPattern->track[t].step[s], elements);
        }
    }
}

void Sequencer::setPatternHeader(strPattern* patt)
{
    patt->header.id_file[0] = 'K';
    patt->header.id_file[1] = 'S';

    patt->header.fwVersion[0] = CORE_VER_1;
    patt->header.fwVersion[1] = CORE_VER_2;
    patt->header.fwVersion[2] = CORE_VER_3;
    patt->header.fwVersion[3] = CORE_VER_1;

    patt->header.type = fileTypePattern;

    patt->header.fileStructureVersion[0] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[1] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[2] = PATTERN_FILE_VERSION;
    patt->header.fileStructureVersion[3] = PATTERN_FILE_VERSION;

    patt->header.size = sizeof(strPattern);
}

void Sequencer::clearPattern(strPattern* patt)
{

    patt->swingRez = 0.0;
    patt->rezerwa1 = 0;
    patt->rezerwa2 = 0;
    patt->rezerwa3 = 0;
    patt->rezerwa4 = 0;

    setPatternHeader(patt);

    // the extra FX of every step go in one go, the steps below clear the stored fields only
    fx34::clearAll(fx34::setOf(patt));

    for (strPattern::strTrack& track : patt->track)
    {
        track.length = DEFAULT_PATTERN_LENGTH - 1;

        for (strPattern::strTrack::strStep& step : track.step)
        {
            clearStepStored(&step, ELEMENTS_ALL_NO_PREFERENCES);
        }
    }
}

void Sequencer::clearSingleTrack(strPattern::strTrack* track)
{
    for (uint8_t i = 0; i <= MAXSTEP; i++)
    {
        clearStep(&track->step[i]);
    }
}

void Sequencer::insert(strSelection* selection)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = MAXSTEP; s > selection->firstStep; s--)
        {
            currentPattern->track[t].step[s] = currentPattern->track[t].step[s - 1];
        }
        fx34::shiftDown(fx34::setOf(currentPattern), t, selection->firstStep, MAXSTEP);
        clearStep(&currentPattern->track[t].step[selection->firstStep],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }
}

void Sequencer::insertReversed(strSelection* selection)
{
    if (!isSelectionCorrect(selection))
        return;

    for (uint8_t t = selection->firstTrack; t <= selection->lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = MINSTEP; s < selection->firstStep; s++)
        {
            currentPattern->track[t].step[s] = currentPattern->track[t].step[s + 1];
        }
        fx34::shiftUp(fx34::setOf(currentPattern), t, MINSTEP, selection->firstStep);
        clearStep(&currentPattern->track[t].step[selection->firstStep],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }
}

// returns true if executed
bool Sequencer::backspace()
{
    if (!isSelectionCorrect(&selection))
        return false;

    if (selection.firstStep < 1)
        return false;

    strPattern::strTrack::strStep *stepFrom, *stepTo;

    for (uint8_t t = selection.firstTrack; t <= selection.lastTrack; t = stereopairs::next(t))
    {
        for (uint8_t s = selection.firstStep - 1; s < currentPattern->track[0].length; s++)
        {
            stepFrom = &currentPattern->track[t].step[s + 1];
            stepTo = &currentPattern->track[t].step[s];

            *stepTo = *stepFrom;
        }
        fx34::shiftUp(fx34::setOf(currentPattern), t, selection.firstStep - 1, currentPattern->track[0].length);

        clearStep(&currentPattern->track[t].step[currentPattern->track[0].length],
                  ELEMENTS_ALL_NO_PREFERENCES);
    }

    return true;
}
void Sequencer::copyToBuffer()
{
    copySelectionToBuffer(&sequencer.copySelection, &sequencer.pasteSelection);
}
void Sequencer::pasteFromBuffer(uint8_t elements)
{
    pasteSelectionFromBuffer(&sequencer.copySelection, &sequencer.pasteSelection, elements);
}

void Sequencer::clearCopyPasteBuffer()
{
    clearPattern(&copyPastePattern);
}

void Sequencer::copySelectionToBuffer(strSelection* from, strSelection* /*to*/)
{
    // zbedne w przypadku kopiowania calego patternu
    // ale tez nie powinno przeszkadzac
    if (!isSelectionCorrect(from))
        return;

    copyPastePattern = *currentPattern;
    fx34::copySet(fx34::setOf(&copyPastePattern), fx34::setOf(currentPattern));
}
void Sequencer::copyStep(strPattern::strTrack::strStep* srcStep,
                         strPattern::strTrack::strStep* destStep,
                         bool toMidi,
                         int elements)
{

    bool pasteNote = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                     elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_NOTES;

    bool pasteInstruments = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                            elements == ELEMENTS_ALL_WITH_PREFERENCES ||
                            elements == ELEMENTS_INSTRUMENTS;

    bool pasteFx1 = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                    elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_FX1;

    bool pasteFx2 = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                    elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_FX2;

    bool pasteMicromove = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                          elements == ELEMENTS_ALL_WITH_PREFERENCES ||
                          elements == ELEMENTS_MICROMOVE;

    bool pasteVolume = elements == ELEMENTS_ALL_NO_PREFERENCES ||
                       elements == ELEMENTS_ALL_WITH_PREFERENCES || elements == ELEMENTS_VOLUME;

    clearStep(destStep);

    if (pasteNote)
    {
        destStep->note = srcStep->note;
        destStep->instrument = mtProject.values.lastUsedInstrument;
    }

    if (pasteInstruments)
    {
        destStep->instrument = srcStep->instrument;
    }

    if (pasteFx1)
    {
        destStep->fx[0] = srcStep->fx[0];
    }

    if (pasteFx2)
    {
        destStep->fx[1] = srcStep->fx[1];
    }

    if (pasteMicromove)
    {
        destStep->micromove = srcStep->micromove;
    }

    if (pasteVolume)
    {
        destStep->volume = srcStep->volume;
    }

    // the extra FX follow the same element rules (the destination was cleared above)
    if (fx34::enabled())
    {
        const bool pasteAll = elements == ELEMENTS_ALL_NO_PREFERENCES || elements == ELEMENTS_ALL_WITH_PREFERENCES;
        const uint8_t mask = pasteAll ? 3 : (elements == ELEMENTS_FX_RANK2 ? 1 : (elements == ELEMENTS_FX_RANK3 ? 2 : 0));
        const fx34::Pattern *sp, *dp;
        uint8_t st, ss, dt, ds;
        if (mask && fx34::locate(srcStep, &sp, &st, &ss) && fx34::locate(destStep, &dp, &dt, &ds))
        {
            fx34::copyStep(fx34::setOf(dp), dt, ds, fx34::setOf(sp), st, ss, mask);   // a full set raises the refusal itself
        }
    }

    if (toMidi && isSampleInstrument(destStep->instrument))
    {
        if (!destStep->isNoteOff())
        {
            destStep->note = STEP_NOTE_EMPTY;
        }

        destStep->instrument = 0;
    }
}

void Sequencer::pasteSelectionFromBuffer(strSelection* from, strSelection* to, uint8_t elements)
{
    if (!isSelectionCorrect(from))
        return;
    if (!isSelectionCorrect(to))
        return;

    if (elements == ELEMENTS_ALL_WITH_PREFERENCES)
    {
        *currentPattern = copyPastePattern;
        fx34::copySet(fx34::setOf(currentPattern), fx34::setOf(&copyPastePattern));
        return;
    }

    const int tracksToCopy = from->lastTrack - from->firstTrack + 1;
    const int rowsToCopy = from->lastStep - from->firstStep + 1;

    for (int destTrackIdx = to->firstTrack, trackOffset = 0;
         destTrackIdx <= to->firstTrack + tracksToCopy - 1;
         destTrackIdx++, trackOffset++)
    {
        // a follower of a linked stereo pair is neither read nor written (its data stays as it is)
        if (stereopairs::isFollower(destTrackIdx) || stereopairs::isFollower(from->firstTrack + trackOffset))
            continue;
        for (int destStepIdx = to->firstStep, stepOffset = 0;
             destStepIdx <= to->firstStep + rowsToCopy - 1;
             destStepIdx++, stepOffset++)
        {
            int srcTrackIdx, srcStepIdx;
            srcTrackIdx = from->firstTrack + trackOffset;
            srcStepIdx = from->firstStep + stepOffset;

            // czy step istnieje?
            if (srcStepIdx <= MAXSTEP && srcTrackIdx < kAllTracks && destStepIdx <= MAXSTEP &&
                destTrackIdx < kAllTracks && destStepIdx <= currentPattern->track[0].length)
            {
                Sequencer::strPattern::strTrack::strStep *srcStep, *destStep;

                srcStep = &copyPastePattern.track[srcTrackIdx].step[srcStepIdx];
                destStep = &currentPattern->track[destTrackIdx].step[destStepIdx];

                bool toMidiTrack = !isSampleTrack(destTrackIdx);
                copyStep(srcStep, destStep, toMidiTrack, elements);
            }
        }
    }
}

bool Sequencer::isSelectionCorrect(strSelection* selection)
{
    if (selection->firstStep > selection->lastStep ||
        selection->firstTrack > selection->lastTrack || selection->firstStep > MAXSTEP ||
        selection->firstTrack > kAllTracks - 1 || selection->lastStep > MAXSTEP ||
        selection->lastTrack > kAllTracks - 1)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

bool Sequencer::isSingleSelection(strSelection* selection)
{
    if (selection->firstStep == selection->lastStep &&
        selection->firstTrack == selection->lastTrack && isSelectionCorrect(selection))

    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void Sequencer::setCopySelection(uint8_t stepFrom,
                                 uint8_t trackFrom,
                                 uint8_t stepTo,
                                 uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        copySelection.firstStep = stepTo;
        copySelection.lastStep = stepFrom;
    }
    else
    {
        copySelection.firstStep = stepFrom;
        copySelection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        copySelection.firstTrack = trackTo;
        copySelection.lastTrack = trackFrom;
    }
    else
    {
        copySelection.firstTrack = trackFrom;
        copySelection.lastTrack = trackTo;
    }
}
uint8_t Sequencer::getCopySelectionHeight()
{
    if (isSelectionCorrect(&copySelection))
    {
        return copySelection.lastStep - copySelection.firstStep + 1;
    }
    else
    {
        return 0;
    }
}

int8_t Sequencer::getSelectionFirstNote()
{
    int8_t retNote = getActualPattern()->track[selection.firstTrack].step[selection.firstStep].note;

    if (retNote < 0 || !isSelectionCorrect(&selection))
    {
        retNote = STEP_NOTE_DEFAULT;
    }
    return retNote;
}

void Sequencer::setSelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        selection.firstStep = stepTo;
        selection.lastStep = stepFrom;
    }
    else
    {
        selection.firstStep = stepFrom;
        selection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        selection.firstTrack = trackTo;
        selection.lastTrack = trackFrom;
    }
    else
    {
        selection.firstTrack = trackFrom;
        selection.lastTrack = trackTo;
    }
}

void Sequencer::setPasteSelection(uint8_t stepFrom,
                                  uint8_t trackFrom,
                                  uint8_t stepTo,
                                  uint8_t trackTo)
{
    if (stepTo < stepFrom)
    {
        pasteSelection.firstStep = stepTo;
        pasteSelection.lastStep = stepFrom;
    }
    else
    {
        pasteSelection.firstStep = stepFrom;
        pasteSelection.lastStep = stepTo;
    }

    if (trackTo < trackFrom)
    {
        pasteSelection.firstTrack = trackTo;
        pasteSelection.lastTrack = trackFrom;
    }
    else
    {
        pasteSelection.firstTrack = trackFrom;
        pasteSelection.lastTrack = trackTo;
    }
}
//@formatter:off
AUDIO_CORE_RAM int16_t Sequencer::getFxMax(uint8_t fxID, bool forRandom)
{
    switch (fxID)
    {
        case fx.FX_TYPE_VOLUME: return 100;
        case fx.FX_TYPE_PANNING: return 100;
        case fx.FX_TYPE_MICROTUNING: return 198;
        case fx.FX_TYPE_CHANCE:
        case fx.FX_TYPE_COND: return forRandom ? fx.CHANCE_PROB_MAX : fx.CHANCE_MAX;

        case fx.FX_TYPE_BREAK_PATTERN: return 1;

        case fx.FX_TYPE_MICROMOVE: return 100;
        case fx.FX_TYPE_TEMPO: return MAX_TEMPO_FX;
        case fx.FX_TYPE_SWING: return 75;
        case fx.FX_TYPE_OFF: return 0;
        case fx.FX_TYPE_RANDOM_INSTRUMENT: return 47;
        case fx.FX_TYPE_RANDOM_VALUE: return 255;
        case fx.FX_TYPE_SAMPLE_SLICE: return 47;
        case fx.FX_TYPE_REVERSE_PLAYBACK: return 1;
        case fx.FX_TYPE_ROLL: return (fx.ROLL_PERIOD_MAX + 1) * (fx.rollNoteDir_max + 1) - 1;

        case fx.FX_TYPE_SEND_CC_A:
        case fx.FX_TYPE_SEND_CC_B:
        case fx.FX_TYPE_SEND_CC_C:
        case fx.FX_TYPE_SEND_CC_D:
        case fx.FX_TYPE_SEND_CC_E:
        case fx.FX_TYPE_SEND_CC_F: return 127;

        case fx.FX_TYPE_FILTER_LFO:
        case fx.FX_TYPE_PANNING_LFO:
        case fx.FX_TYPE_FINETUNE_LFO:
        case fx.FX_TYPE_POSITION_LFO: return CONSTRAIN_SPEED_OTHER_ENVELOPE;
        case fx.FX_TYPE_VOLUME_LFO: return CONSTRAIN_SPEED_AMP_ENVELOPE;

        case fx.FX_TYPE_ARP: return 33;
        case fx.FX_TYPE_MIDI_CHORD: return MAX_CHORD;
        case fx.FX_TYPE_STRUM: return FX_VALUE_MAX;
        case fx.FX_TYPE_TAPE: return tapefx::kValueMax;
        case fx.FX_TYPE_SNAP: return kSnapFxMax;
        case fx.FX_TYPE_TABLE: return tables::kFxValueMax;
        case fx.FX_TYPE_NNA: return kNnaFxMax;
#ifdef FEATURE_SYNTH_OPEN
        case fx.FX_TYPE_PATCH_CHANGE: return 15;
        case fx.FX_TYPE_PATCH_MORPH: return 100;
#elif defined(FEATURE_SYNTH_PATCHFX)
        case fx.FX_TYPE_PATCH_CHANGE: return 15;
        case fx.FX_TYPE_PATCH_MORPH: return 100;
#endif

        case fx.FX_TYPE_BIT_DEPTH: return BIT_DEPTH_MAX;
        case fx.FX_TYPE_TUNE: return 48;
        case fx.FX_TYPE_SLIDE_UP:
        case fx.FX_TYPE_SLIDE_DOWN:
        case fx.FX_TYPE_AMP_ENV:
        case fx.FX_TYPE_FILTER_ENV: return 255;
        default: return 100;
    }
}

AUDIO_CORE_RAM int16_t Sequencer::getFxMin(uint8_t fxID, bool forRandom)
{
    switch (fxID)
    {
        case fx.FX_TYPE_MICROMOVE: return 0;
        case fx.FX_TYPE_SAMPLE_SLICE: return 0;
        case fx.FX_TYPE_TEMPO:
        {
            return forRandom ? MIN_TEMPO_FX : TEMPO_FX_STP;
        }
        case fx.FX_TYPE_SWING: return 25;
        case fx.FX_TYPE_PANNING: return 0;
        case fx.FX_TYPE_BREAK_PATTERN: return 1;
        case fx.FX_TYPE_ROLL: return fx.ROLL_PERIOD_MIN;
        case fx.FX_TYPE_MIDI_CHORD: return MIN_CHORD;

        case fx.FX_TYPE_BIT_DEPTH: return BIT_DEPTH_MIN;
        case fx.FX_TYPE_TAPE: return tapefx::kValueMin;
        default: return 0;
    }
}
int16_t Sequencer::getFxDefault(uint8_t fxID)
{
    switch (fxID)
    {
        case fx.FX_TYPE_OFF: return 0;
        case fx.FX_TYPE_TEMPO: return 60;
        case fx.FX_TYPE_SWING: return 50;
        case fx.FX_TYPE_ROLL: return 1;
        case fx.FX_TYPE_BREAK_PATTERN: return 1;
        case fx.FX_TYPE_PANNING: return 50;
        case fx.FX_TYPE_MICROTUNING: return 99;
        case fx.FX_TYPE_MIDI_CHORD: return MIN_CHORD;
        case fx.FX_TYPE_STRUM: return 8; // up, 16 ms between notes
        case fx.FX_TYPE_COND: return 50;
        case fx.FX_TYPE_TAPE: return tapefx::kValueDefault; // one step
        case fx.FX_TYPE_SNAP: return 1; // Nearest
        case fx.FX_TYPE_TABLE: return 1;
        case fx.FX_TYPE_NNA: return kNnaCut;
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
        case fx.FX_TYPE_PATCH_MORPH: return 50;
#endif

        default: return 0;
    }
}
//@formatter:on

int16_t Sequencer::rollValueToPeriod(int16_t value)
{
    switch (value)
    {
        case fx.ROLL_PERIOD_1_1: return 1;
        case fx.ROLL_PERIOD_1_2: return 2;
        case fx.ROLL_PERIOD_1_3: return 3;
        case fx.ROLL_PERIOD_1_4: return 4;
        case fx.ROLL_PERIOD_1_6: return 6;
        case fx.ROLL_PERIOD_1_8: return 8;
        case fx.ROLL_PERIOD_1_12: return 12;
        case fx.ROLL_PERIOD_1_16: return 16;
        default: return 0;
    }
}

AUDIO_CORE_RAM int16_t Sequencer::getFxValueCorrection(uint8_t type, uint8_t value)
{

    switch (type)
    {
        case fx.FX_TYPE_TEMPO: return value * 2; break;
        case fx.FX_TYPE_PANNING: return value - 50; break;
        case fx.FX_TYPE_MICROTUNING: return value - 99; break;
        case fx.FX_TYPE_TUNE: return value - 24; break;
        case fx.FX_TYPE_SAMPLE_SLICE: // jawnie od 1, programowo od 0
            return value + 1;
            break;

        default: return value;
    }
}
const uint8_t MAX_LFOSPEED_LABELS_AMP = ENVELOPE_SPEED_AMP_COUNT;
const uint8_t MAX_LFOSPEED_LABELS = ENVELOPE_SPEED_OTHER_COUNT;
const char seqlfoSpeedLabels_ampOnly[MAX_LFOSPEED_LABELS_AMP + 1][4] = {
    " 24", " 16", " 12", "  8", "  6", "  4", "  3", "  2", "3/2", "  1", "3/4", "1/2", "3/8",
    "1/3", "1/4", "316", "1/6", "1/8", "/12", "/16", "/24", "/32", "/48", "/64", "???"
};
const char seqlfoSpeedLabels[MAX_LFOSPEED_LABELS + 1][4] = {
    "128", " 96", " 64", " 48", " 32", " 24", " 16", " 12", "  8", "  6",
    "  4", "  3", "  2", "3/2", "  1", "3/4", "1/2", "3/8", "1/3", "1/4",
    "316", "1/6", "1/8", "/12", "/16", "/24", "/32", "/48", "/64", "???"
};
const uint8_t arpFxLabels_count = 34;
const char arpFxLabels[arpFxLabels_count][4] = {
    "  0",  "/ 8",  "/ 6",  "/ 4",  "/ 3",  "/ 2",  "/ 1",  "/.2",  "/.3",  "/.4",  "/.6",  "/.8",
    "\\ 8", "\\ 6", "\\ 4", "\\ 3", "\\ 2", "\\ 1", "\\.2", "\\.3", "\\.4", "\\.6", "\\.8", "R 8",
    "R 6",  "R 4",  "R 3",  "R 2",  "R 1",  "R.2",  "R.3",  "R.4",  "R.6",  "R.8",
};

void Sequencer::makeFxValLabelForSeqStep(char* ptr,
                                         int len,
                                         uint8_t fxID,
                                         uint8_t track,
                                         uint8_t step)
{
    assert(track < kAllTracks);

    if (step > MAXSTEP)
        return;

    strPattern::strTrack::strStep* actualStep = &currentPattern->track[track].step[step];

    const fx34::Fx f = fx34::getFx(actualStep, fxID);   // ranks 2 and 3 come from the extra FX of the pattern
    makeFxValLabelWithAlgin(ptr, len, f.type, f.value, fxValLabelAlign_right);
}

const char* skipSpaceOnStart(const char* str)
{
    while (*str != 0 && isspace(*str))
    {
        str++;
    }
    return str;
}

void Sequencer::makeFxValLabelWithAlgin(char* ptr,
                                        int len,
                                        uint8_t fxType,
                                        uint8_t valueFromFx,
                                        enFxValLabelAlign align)
{

    int16_t valAfterCorrection = getFxValueCorrection(fxType, valueFromFx);

    switch (fxType)
    {
        case fx.FX_TYPE_ROLL:
        {
            char typeChar = getRollTypeChar(valueFromFx);
            if (align == fxValLabelAlign_center && typeChar == ' ')
            {
                snprintf(ptr, len, "%u", rollValueToPeriod(valueFromFx % (fx.ROLL_PERIOD_MAX + 1)));
            }
            else
            {
                snprintf(ptr,
                         len,
                         "%c%2u",
                         getRollTypeChar(valueFromFx),
                         rollValueToPeriod(valueFromFx % (fx.ROLL_PERIOD_MAX + 1)));
            }
            break;
        }
        case fx.FX_TYPE_FILTER_LFO:
        case fx.FX_TYPE_PANNING_LFO:
        case fx.FX_TYPE_POSITION_LFO:
        case fx.FX_TYPE_FINETUNE_LFO:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, MAX_LFOSPEED_LABELS);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? seqlfoSpeedLabels[valAfterCorrection]
                      : skipSpaceOnStart(seqlfoSpeedLabels[valAfterCorrection]),
                    len);

            break;
        }
        case fx.FX_TYPE_VOLUME_LFO:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, MAX_LFOSPEED_LABELS_AMP);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? seqlfoSpeedLabels_ampOnly[valAfterCorrection]
                      : skipSpaceOnStart(seqlfoSpeedLabels_ampOnly[valAfterCorrection]),
                    len);
            break;
        }
        case fx.FX_TYPE_ARP:
        {
            valAfterCorrection = clamp<int>(valAfterCorrection, 0, arpFxLabels_count);
            strncpy(ptr,
                    align != fxValLabelAlign_center
                      ? arpFxLabels[valAfterCorrection]
                      : skipSpaceOnStart(arpFxLabels[valAfterCorrection]),
                    len);
            break;
        }
        case fx.FX_TYPE_OFF: strncpy(ptr, "OFF", len); break;

        case fx.FX_TYPE_REVERSE_PLAYBACK:
            strncpy(ptr, valAfterCorrection > 0 ? "<<<" : ">>>", len);
            break;

        case fx.FX_TYPE_AMP_ENV:
        case fx.FX_TYPE_FILTER_ENV:
            snprintf(ptr, len, align == fxValLabelAlign_right ? " %02X" : "%02X", valueFromFx);
            break;

        case fx.FX_TYPE_BREAK_PATTERN:
            strncpy(ptr, valAfterCorrection > 0 ? "BRK" : "---", len);
            break;
        case fx.FX_TYPE_CHANCE:
        case fx.FX_TYPE_COND:
        {
            if (valueFromFx <= fx.CHANCE_PROB_MAX)
            {
                if (align == fxValLabelAlign_right)
                {
                    snprintf(ptr, len, "%3i", valAfterCorrection);
                }
                else
                {
                    snprintf(ptr, len, "%i", valAfterCorrection);
                }
            }
            else if (valueFromFx == fx.CHANCE_GROUP_WITH_LAST)
            {
                strncpy(ptr, align == fxValLabelAlign_right ? " GL" : "GL", len);
            }
            else if (valueFromFx == fx.CHANCE_NOT_LAST)
            {
                strncpy(ptr, align == fxValLabelAlign_right ? " !L" : "!L", len);
            }
            else if (valueFromFx >= fx.CHANCE_PLAY_SKIP_START &&
                     valueFromFx <= fx.CHANCE_PLAY_SKIP_END)
            {
                uint8_t idx = valueFromFx - fx.CHANCE_PLAY_SKIP_START;
                uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
                uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
                snprintf(ptr, len, "%u:%u", n, m);
            }
            else if (valueFromFx >= fx.CHANCE_SKIP_PLAY_START &&
                     valueFromFx <= fx.CHANCE_SKIP_PLAY_END)
            {
                uint8_t idx = valueFromFx - fx.CHANCE_SKIP_PLAY_START;
                uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
                uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
                snprintf(ptr, len, "S%u%u", n, m);
            }
            else if (valueFromFx >= fx.CHANCE_PASS8_START && valueFromFx <= fx.CHANCE_PASS8_END)
            {   // pass k of 8: "3/8"
                snprintf(ptr, len, "%u/8", unsigned(1 + valueFromFx - fx.CHANCE_PASS8_START));
            }
            else if (valueFromFx >= fx.CHANCE_PASS16_START && valueFromFx <= fx.CHANCE_PASS16_END)
            {   // pass k of 16: "P03", "P16"
                snprintf(ptr, len, "P%02u", unsigned(1 + valueFromFx - fx.CHANCE_PASS16_START));
            }
            break;
        }
        case fx.FX_TYPE_MIDI_CHORD:
            // a centred label with room (the Fill popup) shows the readable name, the 3 character grid cell the short name
            // (Config > Features > Chord names, on by default) or the stock interval digits
            valAfterCorrection = valAfterCorrection > MAX_CHORD ? MIN_CHORD : valAfterCorrection;
            if (align == fxValLabelAlign_center && len > 4)
            {
                chordName(ptr, len, uint8_t(valAfterCorrection), -1);
            }
            else
            {
                strncpy(ptr, chordShortNamesOn ? chordShortName[valAfterCorrection] : chordDefName[valAfterCorrection], len);
            }
            break;
        case fx.FX_TYPE_STRUM:
        {   // +n up, -n down (n x 2 ms between notes), off at 0 and 64
            const int s = strumUnits(uint8_t(valueFromFx));
            if (s == 0)
            {
                strncpy(ptr, "off", len);
            }
            else
            {
                snprintf(ptr, len, align == fxValLabelAlign_right ? "%+3i" : "%+i", s);
            }
            break;
        }
        case fx.FX_TYPE_TAPE:
        {   // the run length in steps. A popup with room: "Stop 1", "Start 0.25", "Stop 1.50" (value 1..64 Stop, 65..128 Start, the grid
            // letter says W or Z). The 3 character grid cell: 1, 1/4, 1/2, 3/4, 1.2 (1 1/4), 2.5, 3.7
            const uint8_t v = tapefx::clampValue(uint8_t(valueFromFx));
            const int whole = tapefx::labelWhole(v), quarter = tapefx::labelQuarter(v);
            if (align != fxValLabelAlign_right && len > 10)
            {
                const char* const word = tapefx::isStart(v) ? "Start" : "Stop";
                if (quarter == 0)
                {
                    snprintf(ptr, len, "%s %i", word, whole);
                }
                else
                {
                    snprintf(ptr, len, "%s %i.%02i", word, whole, quarter);
                }
            }
            else if (quarter == 0)
            {
                snprintf(ptr, len, align == fxValLabelAlign_right ? "%3i" : "%i", whole);
            }
            else if (len > 4)
            {
                snprintf(ptr, len, "%i.%02i", whole, quarter);
            }
            else if (whole == 0)
            {
                strncpy(ptr, quarter == 25 ? "1/4" : quarter == 50 ? "1/2" : "3/4", len);
            }
            else
            {
                snprintf(ptr, len, "%i.%i", whole, quarter / 10);
            }
            break;
        }
        case fx.FX_TYPE_SNAP:
        {   // 0 Off, 1 Nearest, 2 Down, 3 Up (the track snap modes of scales.h), 4 Track (the project setting of the track). A popup with room: the word, the grid cell: 3 characters
            static const char* const snapFull[kSnapFxMax + 1] = { "Off", "Nearest", "Down", "Up", "Track" };
            static const char* const snapCell[kSnapFxMax + 1] = { "off", "nea", "dwn", " up", "trk" };
            const uint8_t v = valueFromFx > kSnapFxMax ? kSnapFxMax : uint8_t(valueFromFx);
            strncpy(ptr, len > 4 ? snapFull[v] : snapCell[v], len);
            break;
        }
        case fx.FX_TYPE_NNA:
        {   // the names of the New note block of the Synth page, a 3 character cell in the grid
            static const char* const nnaFull[kNnaFxMax + 1] = { "Release", "Continue", "Cut", "Fade" };
            static const char* const nnaCell[kNnaFxMax + 1] = { "rel", "con", "cut", "fad" };
            const uint8_t v = valueFromFx > kNnaFxMax ? kNnaFxMax : uint8_t(valueFromFx);
            strncpy(ptr, len > 4 ? nnaFull[v] : nnaCell[v], len);
            break;
        }
        case fx.FX_TYPE_TABLE:
        {   // 0 stops the table of the track, 1..16 the table
            if (valueFromFx == 0)
                strncpy(ptr, len > 4 ? "Stop" : "off", len);
            else
                snprintf(ptr, len, len > 4 ? "Table %d" : "%3d", int(valueFromFx));
            break;
        }
        case fx.FX_TYPE_TEMPO:
        {
            if (valueFromFx == getFxMin(fxType))
            {
                strncpy(ptr, "STP", len);
            }
            else
            {
                if (align == fxValLabelAlign_right)
                {
                    snprintf(ptr, len, "%3i", valAfterCorrection);
                }
                else
                {
                    snprintf(ptr, len, "%i", valAfterCorrection);
                }
            }
            break;
        }
        default:
        {
            if (align == fxValLabelAlign_right)
            {
                snprintf(ptr, len, "%3i", valAfterCorrection);
            }
            else
            {
                snprintf(ptr, len, "%i", valAfterCorrection);
            }
            break;
        }
    }
    ptr[len - 1] = 0;
}

char Sequencer::getRollTypeChar(uint8_t val)
{
    switch (val / (fx.ROLL_PERIOD_MAX + 1))
    {
        case fx.rollType_const: return ' ';
        case fx.rollType_noteUp: return 'N';
        case fx.rollType_noteDown: return 'n';
        case fx.rollType_noteRandom: return 'R';
        case fx.rollType_volDown: return 'v';
        case fx.rollType_volUp: return 'V';
        default: break;
    }
    return '?';
}
AUDIO_CORE_RAM uint8_t Sequencer::calcStepLength(uint8_t track, uint8_t step)
{
    uint8_t trackLen = getActualPattern()->track[0].length;

    uint8_t b = 1;
    for (uint8_t a = step + 1; a <= trackLen; a++, b++)
    {
        if (getActualPattern()->track[track].step[a].note != STEP_NOTE_EMPTY)
        {
            break;
        }
    }

    return b;
}

AUDIO_CORE_RAM uint8_t Sequencer::isTrackEngineMuted(uint8_t track)
{
    return isSampleTrack(track) ? mtProject.values.trackMute[track]
                                : mtProject.values.midiTrackMute[track % 8];
}
