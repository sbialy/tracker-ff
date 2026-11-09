// Synth Automation 3/3: 4 slots per patch, each Off, an LFO or an envelope on one engine parameter or user macro (synth-open/open_mod.h).
// The sample automation page's look and controls with its wide destination column split in two: column 0 the slot, column 1 the
// destination, columns 2-7 as the sample page (Type, then Attack Decay Sustain Release Amount, or Shape Speed Amount). Every edit goes
// through Synth::setModSlot (audio interrupts off, patch dirty, never the project structure). Fixed buffers only: on the MK66 operator new
// returns null on a short heap.
#include "instrumentEditor/instrumentEditor.h"
#if GUI_SYNTH_AUTO

#include "core/interfacePopups.h"
#include "mtAudioEngine.h"

#include "util_math.h"

#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

using polysynth::open::EngineDef;
using polysynth::open::kModPickMacro;
using polysynth::open::kModSlots;
using polysynth::open::ModKind;
using polysynth::open::ModSlot;
using polysynth::open::ModType;

extern const char* const lfoShapeLabels[lfoShapeCount];
extern const char* const lfoSpeedLabels[ENVELOPE_SPEED_OTHER_COUNT];

static cInstrumentEditor* IE = &instrumentEditor;

// The page's list buffers: picker codes ([0] is "? name" while the slot is unresolved), the line texts the lists point at. In the
// SDRAM (plain data, no constructor may touch it: the SDRAM works only after main() set it up), rebuilt on every show.
namespace
{
constexpr int kAutoNameLen = 24;
struct AutoLists
{
    int16_t pick[polysynth::open::kModPickMax + 1];
    char* pickNames[polysynth::open::kModPickMax + 1];
    int16_t pickCount;
    char macroNames[polysynth::open::kModMacros][kAutoNameLen];
    char slotNames[kModSlots][kAutoNameLen];
    char* slotPtrs[kModSlots];
    char unresolved[kAutoNameLen];
};
static_assert(std::is_trivially_default_constructible_v<AutoLists>, "EXTERNAL_RAM: no static constructor may touch the SDRAM");
__NOINIT(EXTERNAL_RAM) AutoLists gAuto;
} // namespace

static const char* const kAutoTypeLabels[polysynth::open::kModTypes] = { "Off", "LFO", "Envelope" };

enum AutoPlace
{
    kPlaceSlot,
    kPlaceDest,
    kPlaceType,
    kPlaceAttack, // LFO: Shape
    kPlaceDecay,  // LFO: Speed
    kPlaceSustain, // LFO: Amount
    kPlaceRelease,
    kPlaceAmount,
};

static void functAutoActionButtons(uint8_t button);
static void functAutoEncoder(int16_t value);
static void functAutoUp();
static void functAutoDown();
static void functAutoLeft();
static void functAutoRight();
static void functAutoDelete();
static void functAutoMiniMiddle(uint8_t state);
static void functAutoPads(uint8_t pad, uint8_t state, int16_t);
static void functAutoShift(uint8_t state);

static int autoPart()
{
    return instrumentIdxToSynthIdx(mtProject.values.lastUsedInstrument);
}

// The part's engine, nullptr when it is missing (a plugin not attached): the page shows the slots but edits nothing, the files of a
// missing engine are never rewritten
static const EngineDef* autoEngine()
{
    const EngineDef* d = IE->getSynth()->getModEngine(autoPart());
    return polysynth::open::engineOf(d) ? d : nullptr;
}

static const ModSlot& autoCur()
{
    return IE->getSynth()->getModSlot(autoPart(), IE->autoSlot);
}

static void autoWrite(const ModSlot& m)
{
    IE->getSynth()->setModSlot(autoPart(), IE->autoSlot, m);
}

void cInstrumentEditor::setSynthAutomationScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    for (uint8_t i = interfaceButton0; i <= interfaceButton7; i++)
    {
        ip->setButtonObj(i, buttonPress, functAutoActionButtons);
    }

    ip->setPotObj(interfacePot0, functAutoEncoder);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functAutoLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functAutoRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functAutoUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functAutoDown);

    ip->setButtonObj(interfaceButtonDelete, buttonPress, functAutoDelete);
    ip->setButtonObj(interfaceButtonMiniMiddle, functAutoMiniMiddle);

    ip->setButtonObj(interfaceButtonShift, functAutoShift);

    ip->setPadsGlobal(functAutoPads);

    ip->clearButton(interfaceButtonInsert);
}

bool cInstrumentEditor::synthAutomationOn()
{
    return autoCur().type != ModType::kOff;
}

// The last place the frame may go to: Type while Off, Amount (column 5) for an LFO, column 7 for an envelope
int cInstrumentEditor::synthAutomationLastPlace()
{
    switch (autoCur().type)
    {
        case ModType::kLfo: return kPlaceSustain;
        case ModType::kEnv: return kPlaceAmount;
        default: return kPlaceType;
    }
}

static void autoClampPlace()
{
    uint8_t& place = IE->selectedPlace[mtInstEditModeSynthAutomation];
    const int last = IE->synthAutomationLastPlace();
    if (place > last)
    {
        place = uint8_t(last);
    }
}

static void functAutoActionButtons(uint8_t button)
{
    if (IE->mode != mtInstEditModeSynthAutomation)
    {
        return;
    }
    uint8_t& place = IE->selectedPlace[mtInstEditModeSynthAutomation];
    if (button > IE->synthAutomationLastPlace())
    {
        return; // locked while Off, columns 6-7 of an LFO are empty
    }
    // a second press on a list rolls it over (sample page)
    const bool list = button <= kPlaceType || (autoCur().type == ModType::kLfo && (button == kPlaceAttack || button == kPlaceDecay));
    if (place == button && list)
    {
        IE->rollListOver = true;
        IE->changeSynthAutomation(button, 1, cInstrumentEditor::kEncoder);
        IE->rollListOver = false;
    }
    place = button;
    IE->activateLabelsBorder();
}

static void functAutoEncoder(int16_t value)
{
    IE->changeSynthAutomation(IE->selectedPlace[mtInstEditModeSynthAutomation], value, cInstrumentEditor::kEncoder);
}

static void functAutoUp()
{
    IE->changeSynthAutomation(IE->selectedPlace[mtInstEditModeSynthAutomation], 1);
}

static void functAutoDown()
{
    IE->changeSynthAutomation(IE->selectedPlace[mtInstEditModeSynthAutomation], -1);
}

static void functAutoLeft()
{
    uint8_t& place = IE->selectedPlace[mtInstEditModeSynthAutomation];
    if (place > 0)
    {
        place--;
    }
    IE->activateLabelsBorder();
}

static void functAutoRight()
{
    uint8_t& place = IE->selectedPlace[mtInstEditModeSynthAutomation];
    if (place < IE->synthAutomationLastPlace())
    {
        place++;
    }
    IE->activateLabelsBorder();
}

static void functAutoDelete()
{
    IE->setDefaultSynthAutomation(IE->selectedPlace[mtInstEditModeSynthAutomation]);
}

static void functAutoMiniMiddle(uint8_t state)
{
    IE->synthCommonPreviewButtonFunct(state);
}

static void functAutoPads(uint8_t pad, uint8_t state, int16_t)
{
    IE->playSynthNoteByPad(pad, state);
}

static void functAutoShift(uint8_t state)
{
    if (state == buttonPress)
    {
        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kShowEditButtons);
        }
    }
    else if (state == buttonRelease)
    {
        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kForceShow);
        }
    }
}

// ------------------------------------------------------------------------------------------ values

static int autoStep(int value, int delta, int lo, int hi, bool roll)
{
    const int v = value + delta;
    if (v > hi)
    {
        return roll ? lo : hi;
    }
    return v < lo ? lo : v;
}

static int autoTimeStep(uint16_t ms, int delta)
{
    using namespace polysynth::open;
    return modStepToTime(clamp<int>(modTimeToStep(ms) + delta, 0, kModTimeSteps));
}

// Position of the slot's destination in the picker list: the "? name" entry while unresolved, the first real entry when none is picked
static int autoPickPos()
{
    const ModSlot& m = autoCur();
    const int code = polysynth::open::modPickCode(m);
    const int first = (gAuto.pickCount > 0 && gAuto.pick[0] < 0) ? 1 : 0;
    if (code < 0)
    {
        return 0;
    }
    for (int i = first; i < gAuto.pickCount; i++)
    {
        if (gAuto.pick[i] == code)
        {
            return i;
        }
    }
    return first;
}

void cInstrumentEditor::changeSynthAutomation(int place, int16_t delta, ValueChangeSource source)
{
    if (!isSynthInstrument(mtProject.values.lastUsedInstrument))
    {
        return;
    }
    const bool invert = source != kEncoder || interfaceGlobals.invertValForList;
    const int listDelta = invert ? -delta : delta;

    if (place == kPlaceSlot)
    {
        autoSlot = uint8_t(autoStep(autoSlot, listDelta, 0, kModSlots - 1, rollListOver));
        buildSynthAutomationLists();
        autoClampPlace();
        showSynthAutomationColumns();
        activateLabelsBorder();
        return;
    }

    const EngineDef* def = autoEngine();
    if (!def)
    {
        return;
    }
    ModSlot m = autoCur();
    const ModType wasType = m.type;
    switch (place)
    {
        case kPlaceDest:
        {
            const int pos = autoStep(autoPickPos(), listDelta, 0, gAuto.pickCount - 1, rollListOver);
            if (gAuto.pick[pos] < 0)
            {
                return; // the unresolved entry stays as it is
            }
            polysynth::open::modSetDest(*def, m, gAuto.pick[pos]);
            break;
        }
        case kPlaceType:
            m.type = ModType(autoStep(int(m.type), listDelta, 0, polysynth::open::kModTypes - 1, rollListOver));
            if (m.type != ModType::kOff && m.dest < 0 && !m.name[0] && gAuto.pickCount > 0)
            {
                // no destination yet: the first of the list (a wave like param, or Macro 1)
                polysynth::open::modSetDest(*def, m, gAuto.pick[autoPickPos()]);
            }
            break;
        case kPlaceAttack:
            if (m.type == ModType::kLfo)
            {
                m.shape = uint8_t(autoStep(m.shape, listDelta, 0, polysynth::open::kLfoShapes - 1, rollListOver));
            }
            else if (m.type == ModType::kEnv)
            {
                m.attack = uint16_t(autoTimeStep(m.attack, delta));
            }
            break;
        case kPlaceDecay:
            if (m.type == ModType::kLfo)
            {
                m.speed = uint8_t(autoStep(m.speed, listDelta, 0, polysynth::open::kLfoSpeeds - 1, rollListOver));
            }
            else if (m.type == ModType::kEnv)
            {
                m.decay = uint16_t(autoTimeStep(m.decay, delta));
            }
            break;
        case kPlaceSustain:
            if (m.type == ModType::kLfo)
            {
                m.amount = int8_t(clamp<int>(m.amount + delta, -polysynth::open::kModAmountMax, polysynth::open::kModAmountMax));
            }
            else if (m.type == ModType::kEnv)
            {
                m.sustain = uint8_t(clamp<int>(m.sustain + delta, 0, 100));
            }
            break;
        case kPlaceRelease:
            if (m.type == ModType::kEnv)
            {
                m.release = uint16_t(autoTimeStep(m.release, delta));
            }
            break;
        case kPlaceAmount:
            if (m.type == ModType::kEnv)
            {
                m.amount = int8_t(clamp<int>(m.amount + delta, -polysynth::open::kModAmountMax, polysynth::open::kModAmountMax));
            }
            break;
        default: return;
    }
    autoWrite(m);
    if (place == kPlaceDest || m.type != wasType)
    {
        buildSynthAutomationLists(); // the "? name" entry goes once another destination is picked
    }
    autoClampPlace();
    showSynthAutomationColumns();
    activateLabelsBorder();
}

void cInstrumentEditor::setDefaultSynthAutomation(int place)
{
    const EngineDef* def = autoEngine();
    if (!def || place == kPlaceSlot)
    {
        return;
    }
    const ModSlot d;
    ModSlot m = autoCur();
    switch (place)
    {
        case kPlaceDest:
        {
            const int first = (gAuto.pickCount > 0 && gAuto.pick[0] < 0) ? 1 : 0;
            if (first < gAuto.pickCount)
            {
                polysynth::open::modSetDest(*def, m, gAuto.pick[first]);
            }
            break;
        }
        case kPlaceType: m.type = ModType::kOff; break;
        case kPlaceAttack:
            m.shape = m.type == ModType::kLfo ? d.shape : m.shape;
            m.attack = m.type == ModType::kEnv ? d.attack : m.attack;
            break;
        case kPlaceDecay:
            m.speed = m.type == ModType::kLfo ? d.speed : m.speed;
            m.decay = m.type == ModType::kEnv ? d.decay : m.decay;
            break;
        case kPlaceSustain:
            m.amount = m.type == ModType::kLfo ? d.amount : m.amount;
            m.sustain = m.type == ModType::kEnv ? d.sustain : m.sustain;
            break;
        case kPlaceRelease: m.release = d.release; break;
        case kPlaceAmount: m.amount = d.amount; break;
        default: return;
    }
    autoWrite(m);
    buildSynthAutomationLists();
    autoClampPlace();
    showSynthAutomationColumns();
    activateLabelsBorder();
}

// ------------------------------------------------------------------------------------------ GUI

// The picker of the part's engine and the slot lines. Names point into flash (param names), the macro buffers or the "? name" buffer.
void cInstrumentEditor::buildSynthAutomationLists()
{
    const int part = autoPart();
    Synth* s = getSynth();
    const ModSlot& m = s->getModSlot(part, autoSlot);
    int off = 0;
    if (m.type != ModType::kOff && m.dest < 0 && m.name[0])
    {
        snprintf(gAuto.unresolved, sizeof(gAuto.unresolved), "? %s", m.name);
        gAuto.pick[0] = -1;
        gAuto.pickNames[0] = gAuto.unresolved;
        off = 1;
    }
    const int n = s->modDestinations(part, gAuto.pick + off);
    for (int i = off; i < off + n; i++)
    {
        const int code = gAuto.pick[i];
        if (code >= kModPickMacro)
        {
            const int mi = code - kModPickMacro;
            snprintf(gAuto.macroNames[mi], sizeof(gAuto.macroNames[mi]), "M%d %s", mi + 1, s->modDestName(part, code));
            gAuto.pickNames[i] = gAuto.macroNames[mi];
        }
        else
        {
            gAuto.pickNames[i] = const_cast<char*>(s->modDestName(part, code));
        }
    }
    gAuto.pickCount = int16_t(off + n);

    for (int k = 0; k < kModSlots; k++)
    {
        const ModSlot& sk = s->getModSlot(part, k);
        if (sk.type == ModType::kOff)
        {
            snprintf(gAuto.slotNames[k], sizeof(gAuto.slotNames[k]), "%d Off", k + 1);
        }
        else if (sk.dest < 0)
        {
            snprintf(gAuto.slotNames[k], sizeof(gAuto.slotNames[k]), "%d ? %s", k + 1, sk.name);
        }
        else if (sk.kind == ModKind::kMacro)
        {
            snprintf(gAuto.slotNames[k], sizeof(gAuto.slotNames[k]), "%d M%d", k + 1, sk.dest + 1);
        }
        else
        {
            snprintf(gAuto.slotNames[k], sizeof(gAuto.slotNames[k]), "%d %s", k + 1, s->modDestName(part, sk.dest));
        }
        gAuto.slotPtrs[k] = gAuto.slotNames[k];
    }
}

// The section short name of a param (the Synth page's section), "Macro" for a macro destination
static const char* autoSectionOf(const EngineDef* def, int code)
{
    if (code >= kModPickMacro)
    {
        return "Macro";
    }
    if (!def || code < 0)
    {
        return "";
    }
    for (int i = 0; i < def->sectionCount; i++)
    {
        const auto& sec = def->sections[i];
        for (int k = 0; k < sec.count; k++)
        {
            if (sec.ids[k] == code)
            {
                return sec.shortName ? sec.shortName : sec.name;
            }
        }
    }
    return "";
}

static void autoBarColors(int col, bool on)
{
    display.setControlColors(IE->label[col], on ? display.getTheme().activeLabelsColors : display.getTheme().inactiveLabelsColors);
    display.setControlColors(IE->barControl[col], on ? IE->localActiveBarColors : IE->localInactiveBarColors);
}

static void autoShowTime(int col, uint16_t ms, bool on)
{
    snprintf(IE->labelText[col], sizeof(IE->labelText[col]), "%.3fs", float(ms) / 1000.0f);
    autoBarColors(col, on);
    display.setControlText2(IE->label[col], IE->labelText[col]);
    display.setControlValue(IE->barControl[col],
                            ms > ENVELOPE_MICRO_RANGE ? map<int>(ms, ENVELOPE_MICRO_RANGE, ATTACK_MAX, 50, 100)
                                                      : map<int>(ms, 0, ENVELOPE_MICRO_RANGE, 0, 50));
}

static void autoShowAmount(int col, int amount, bool on)
{
    snprintf(IE->labelText[col], sizeof(IE->labelText[col]), "%d", amount);
    autoBarColors(col, on);
    display.setControlText2(IE->label[col], IE->labelText[col]);
    display.setControlValue(IE->barControl[col], amount);
}

void cInstrumentEditor::showSynthAutomationColumns()
{
    const ModSlot& m = autoCur();
    const bool on = m.type != ModType::kOff;
    const bool lfo = m.type == ModType::kLfo;

    uint32_t shadow = (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    uint32_t gradient = (display.getTheme().barShadow > 0 ? controlStyleGradient : 0);

    // column 0: the slot list
    list1Data.start = autoSlot;
    list1Data.length = kModSlots;
    list1Data.linesCount = kModSlots;
    list1Data.data = gAuto.slotPtrs;
    display.setControlData(list1Control, &list1Data);
    display.setControlValue(list1Control, autoSlot);
    snprintf(labelText[0], sizeof(labelText[0]), "Mod %d", autoSlot + 1);
    display.setControlText2(label[0], labelText[0]);

    // column 1: the destination
    const int pos = autoPickPos();
    list2Data.start = pos;
    list2Data.length = gAuto.pickCount;
    list2Data.linesCount = 14;
    list2Data.lineWidthMax = 0;
    list2Data.lineTextLenMax = 0;
    list2Data.data = gAuto.pickNames;
    display.setControlData(list2Control, &list2Data);
    display.setControlValue(list2Control, pos);
    display.setControlText2(label[1], gAuto.pick[pos] < 0 ? "?" : autoSectionOf(getSynth()->getModEngine(autoPart()), gAuto.pick[pos]));

    // column 2: the type
    autoTypeList.start = int16_t(m.type);
    autoTypeList.length = polysynth::open::kModTypes;
    autoTypeList.linesCount = polysynth::open::kModTypes;
    autoTypeList.data = const_cast<char**>(kAutoTypeLabels);
    display.setControlData(filterModeListControl, &autoTypeList);
    display.setControlValue(filterModeListControl, int(m.type));
    display.setControlText2(label[2], kAutoTypeLabels[int(m.type)]);

    if (lfo)
    {
        display.setControlText(label[3], "Shape");
        display.setControlText(label[4], "Speed");
        display.setControlText(label[5], "Amount");
        display.setControlText(label[6], "");
        display.setControlText(label[7], "");
        display.setControlText2(label[6], "");
        display.setControlText2(label[7], "");

        display.setControlHide(barControl[3]);
        display.setControlHide(barControl[4]);
        display.setControlHide(barControl[6]);
        display.setControlHide(barControl[7]);
        display.setControlStyle(barControl[5],
                                (controlStyleShow | controlStyleValueLeftRight_100_100 | controlStyleBackground) | shadow | gradient);

        list3Data.start = m.shape;
        list3Data.length = lfoShapeCount;
        list3Data.linesCount = lfoShapeCount;
        list3Data.data = const_cast<char**>(lfoShapeLabels);
        display.setControlData(list3Control, &list3Data);
        display.setControlValue(list3Control, m.shape);
        display.setControlText2(label[3], lfoShapeLabels[m.shape]);

        list4Data.start = m.speed;
        list4Data.length = ENVELOPE_SPEED_OTHER_COUNT;
        list4Data.linesCount = 14;
        list4Data.data = const_cast<char**>(lfoSpeedLabels);
        display.setControlData(list4Control, &list4Data);
        display.setControlValue(list4Control, m.speed);
        display.setControlText2(label[4], lfoSpeedLabels[m.speed]);

        autoShowAmount(5, m.amount, true);

        display.setControlShow(list3Control);
        display.setControlShow(list4Control);

        display.setControlPosition(blankSpaceCover[0], (800 / 8) * 6 + 1, -1);
        display.setControlSize(blankSpaceCover[0], 800 / 4 - 3, 394);
        display.setControlShow(blankSpaceCover[0]);
    }
    else
    {
        // Envelope, and Off shows the envelope columns greyed (the sample page's look)
        display.setControlText(label[3], "Attack");
        display.setControlText(label[4], "Decay");
        display.setControlText(label[5], "Sustain");
        display.setControlText(label[6], "Release");
        display.setControlText(label[7], "Amount");

        for (int i = 3; i < 7; i++)
        {
            display.setControlStyle(barControl[i],
                                    (controlStyleShow | controlStyleValue_0_100 | controlStyleBackground) | shadow | gradient);
        }
        display.setControlStyle(barControl[7],
                                (controlStyleShow | controlStyleValueLeftRight_100_100 | controlStyleBackground) | shadow | gradient);

        autoShowTime(3, m.attack, on);
        autoShowTime(4, m.decay, on);
        snprintf(labelText[5], sizeof(labelText[5]), "%d", int(m.sustain));
        autoBarColors(5, on);
        display.setControlText2(label[5], labelText[5]);
        display.setControlValue(barControl[5], int(m.sustain));
        autoShowTime(6, m.release, on);
        autoShowAmount(7, m.amount, on);

        display.setControlHide(list3Control);
        display.setControlHide(list4Control);
        display.setControlHide(blankSpaceCover[0]);
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
    for (uint8_t i = 3; i < 8; i++)
    {
        display.refreshControl(barControl[i]);
    }
    display.refreshControl(list1Control);
    display.refreshControl(list2Control);
    display.refreshControl(filterModeListControl);
    display.refreshControl(list3Control);
    display.refreshControl(list4Control);
    display.refreshControl(blankSpaceCover[0]);
    display.synchronizeRefresh();
}

void cInstrumentEditor::showSynthAutomationScreen()
{
    assert(mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX);
    if (autoSlot >= kModSlots)
    {
        autoSlot = 0;
    }

    showTitleBar();
    refreshFramePosition();

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        display.setControlColors(barControl[i], localActiveBarColors);
        display.setAddControlStyle(label[i], controlStyleLimitTextWidth);
        display.setControlPosition(label[i], (800 / 8) * i + (800 / 16), -1);
    }
    display.setRemoveControlStyle(label[1], controlStyleShowBitmap);
    display.setRemoveControlStyle(label[6], controlStyleShowBitmap);
    display.setRemoveControlStyle(label[6], controlStyleSpecialFx2);
    display.setRemoveControlStyle(label[7], controlStyleSpecialFx2);

    display.setControlText(label[0], "Slot");
    display.setControlText(label[1], "Destination");
    display.setControlText(label[2], "Type");

    display.setControlHide(doubleLabelAutomation);
    display.setControlHide(instrumentSynthCCListControl);
    display.setControlHide(instrumentListControl);
    display.setControlHide(noteLabel);
    display.setControlHide(paramsListControl);
    display.setControlHide(modelListControl);
    display.setControlHide(imgGridControl);
    display.setControlHide(barControl[0]);
    display.setControlHide(barControl[1]);
    display.setControlHide(barControl[2]);

    display.setControlPosition(list1Control, (800 / 8) * 0 + 1, -1);
    display.setControlSize(list1Control, 800 / 8 - 3, -1);
    display.setControlColors(list1Control, display.getTheme().activeListColors);
    display.setControlShow(list1Control);

    display.setControlPosition(list2Control, (800 / 8) * 1 + 1, -1);
    display.setControlSize(list2Control, 800 / 8 - 3, -1);
    display.setControlColors(list2Control, display.getTheme().activeListColors);
    display.setControlShow(list2Control);

    display.setControlPosition(filterModeListControl, (800 / 8) * 2 + 1, -1);
    display.setControlColors(filterModeListControl, display.getTheme().activeListColors);
    display.setControlShow(filterModeListControl);

    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    buildSynthAutomationLists();
    autoClampPlace();
    showSynthAutomationColumns();
    activateLabelsBorder();
}

#endif
