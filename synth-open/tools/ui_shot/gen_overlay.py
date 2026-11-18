#!/usr/bin/env python3 -I
"""Host-only overlay of the firmware sources and headers that clang (macOS, 64 bit, Mach-O) cannot build as they are.

Usage: gen_overlay.py ROOT OUTDIR [EXTDIR]. EXTDIR holds the module sources ui_shot.sh fetched from the pinned module repositories
(fx_dirt_plug.cpp of tracker-effects-os). Writes OUTDIR/inc/<header> and OUTDIR/src/<source> (LF, edited copies). Nothing under
core/ or synth-open/ is touched: the Makefile compiles the copies instead of the originals and puts OUTDIR/inc first.
Every edit is listed here with its reason, a source that changes upstream and no longer matches an edit fails loudly.
"""
import os, re, sys

root, out = sys.argv[1], sys.argv[2]
ext = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(out), 'external')
# module sources that are not in this repository: read from EXTDIR (engines/external.py source), path in the SOURCES table -> file name there
EXTERNAL = {'synth-open/plug/fx_dirt_plug.cpp': 'fx_dirt_plug.cpp'}
DIRT_STUB = ('// stub: fx_dirt_plug.cpp of tracker-effects-os was not available (ui_shot.sh fetches it), Dirt fails to load on the pages\n'
             '#include "plug_abi.h"\nextern "C" const plug_desc* plug_entry_dirt(const plug_host*, uint32_t) { return nullptr; }\n')
HOST_VOICES = '''
namespace polysynth::open { int hostVoices(int part, char* buf, unsigned n); }
// ui_shot only (gen_overlay.py): the voices of part `part` that are on, "cap C n=N [slot:gate:note:id:fade ...]", -1 for no such part
int polysynth::open::hostVoices(int part, char* buf, unsigned n)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    int at = 0, cnt = 0;
    char list[200] = {};
    int k = 0;
    for (const Voice& v : i->voiceSpan())
    {
        if (v.on && at < 170)
        {
            at += snprintf(list + at, sizeof(list) - at, " %d:%d:%d:%d:%d", k, int(v.v.gate), int(v.v.note), int(v.id), int(v.v.fading));
            cnt++;
        }
        k++;
    }
    return snprintf(buf, n, "n=%d [%s ]", cnt, list);
}
namespace polysynth::open { int hostPart(int part, float* bend, float* range, int* voiceCount); }
// ui_shot only (synth_play_sim.sh): the live pitch bend (semitones), the bend range and the Voices of part `part`, -1 for no such part
int polysynth::open::hostPart(int part, float* bend, float* range, int* voiceCount)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    *bend = i->testBend();
    *range = i->testBendRange();
    *voiceCount = i->getVoiceCount();
    return 0;
}
namespace polysynth::open { int hostGain(int part, float* gain, float* voiceGain); }
// ui_shot only (synth_play_sim.sh A/B rows, "poke abstate"): the morph factor of part `part` and the gain of its first voice that is on (-1: none)
int polysynth::open::hostGain(int part, float* gain, float* voiceGain)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    *gain = i->testGain();
    *voiceGain = i->testVoiceGain();
    return 0;
}
namespace polysynth::open { int hostCapSim(int part, int maxVoices); }
// ui_shot only (synth_play_sim.sh P14, "poke capsim"): part `part` runs on a copy of its engine definition that declares `maxVoices` voices,
// what a plugin engine with max_voices 4 is (the real tables are read only), -1 for no such part
int polysynth::open::hostCapSim(int part, int maxVoices)
{
    static EngineDef copy[3];
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i && part >= 0 && part < 3 && i->testCap(copy[part], maxVoices) ? 0 : -1;
}
'''
APPLY_HOLD = '''#include <stdint.h>
#include <stdlib.h>
extern "C" volatile uint32_t systick_millis_count;   // host_stubs.cpp: the firmware's systick, it follows the virtual clock
// ui_shot only (gen_overlay.py): true while an apply must still look busy, the unit's apply time of the effect on list row `row`
// (UISHOT_APPLY_MS overrides it for every row, 0 = off). Rows past the table (card .ptfx effects) hold kUnitApplyDefaultMs
static const uint16_t kUnitApplyMs[14] = {100, 100, 100, 100, 100, 5000, 100, 100, 300, 8000, 200, 300, 600, 1500};
static const uint32_t kUnitApplyDefaultMs = 5000u;
static bool uishotApplyHold(bool applying, uint32_t row)
{
    static bool s_on = false;
    static uint32_t s_t0 = 0;
    static const int s_env = getenv("UISHOT_APPLY_MS") ? atoi(getenv("UISHOT_APPLY_MS")) : -1;
    const uint32_t ms = s_env >= 0 ? uint32_t(s_env) : row < 14 ? kUnitApplyMs[row] : kUnitApplyDefaultMs;
    if (!applying || ms == 0) { s_on = false; return false; }
    if (!s_on) { s_on = true; s_t0 = systick_millis_count; }
    return uint32_t(systick_millis_count - s_t0) < ms;
}
'''
SECTION = (re.compile(r'section\([^)]*\)'), 'unused')   # GCC/ELF section names are not valid Mach-O segment,section pairs


def put(path, text):
    # only touch a file whose content changed: make rebuilds on mtime
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and open(path, encoding='latin-1', newline='').read() == text:
        return
    with open(path, 'w', encoding='latin-1', newline='') as f:
        f.write(text)


def rd(p):
    if p in EXTERNAL:
        f = os.path.join(ext, EXTERNAL[p])
        if not os.path.exists(f):
            return DIRT_STUB
        with open(f, encoding='latin-1', newline='') as fh:
            return fh.read().replace('\r\n', '\n')
    with open(os.path.join(root, p), encoding='latin-1', newline='') as f:
        return f.read().replace('\r\n', '\n')


def wr(sub, name, text):
    d = os.path.join(out, sub)
    os.makedirs(d, exist_ok=True)
    put(os.path.join(d, name), text)


def edit(text, rules, path):
    for pat, rep in rules:
        new, n = pat.subn(rep, text)
        if n == 0:
            sys.exit('gen_overlay: edit %r no longer matches %s' % (pat.pattern, path))
        text = new
    return text


HEADERS = {
    'core/config/config.h': [SECTION,
        # CPSID/CPSIE are ARM, the host has no interrupt mask to touch
        (re.compile(r'__asm__ volatile\("CPS(ID|IE) i" ::: "memory"\)'), '((void)0)')],
    'core/include/cr_section_macros.h': [SECTION],
    'synth-open/ot/fastrun.h': [SECTION],
    'synth-open/plug/plug_abi.h': [SECTION],
}
for p, rules in HEADERS.items():
    wr('inc', os.path.basename(p), edit(rd(p), rules, p))

# dspinst.h: Teensy keeps portable C bodies for Cortex-M0 (KINETISL), the host takes those, and the bare asm helpers
# (QADD16, SMLAWB, SMUAD ...) get C bodies with the same semantics
# "utility/dspinst.h" is a quote include, found next to the including file before any -I dir: the whole libs/Audio tree is
# copied to OUTDIR/libs/Audio and the Makefile builds its sources from there
import shutil
src_audio = os.path.join(root, 'core/libs/Audio')
for dp, dn, fn in os.walk(src_audio):
    for n in fn:
        sp = os.path.join(dp, n)
        dst = os.path.join(out, 'libs/Audio', os.path.relpath(sp, src_audio))
        put(dst, open(sp, encoding='latin-1', newline='').read().replace('\r\n', '\n'))
p = 'core/libs/Audio/utility/dspinst.h'
t = rd(p)
t = t.replace('#if defined(KINETISK)', '#if 0').replace('#elif defined(KINETISL)', '#elif 1')
C = {
 'qadd16':   'int32_t lo=(int16_t)a+(int16_t)b, hi=(int16_t)(a>>16)+(int16_t)(b>>16); lo=lo>32767?32767:lo<-32768?-32768:lo; hi=hi>32767?32767:hi<-32768?-32768:hi; return (uint32_t)((hi<<16)|(lo&0xFFFF));',
 'qsub16':   'int32_t lo=(int16_t)a-(int16_t)b, hi=(int16_t)(a>>16)-(int16_t)(b>>16); lo=lo>32767?32767:lo<-32768?-32768:lo; hi=hi>32767?32767:hi<-32768?-32768:hi; return (int32_t)((uint32_t)(hi<<16)|(lo&0xFFFF));',
 'shadd16':  'int32_t lo=((int16_t)a+(int16_t)b)>>1, hi=((int16_t)(a>>16)+(int16_t)(b>>16))>>1; return (int32_t)((uint32_t)(hi<<16)|(lo&0xFFFF));',
 'shsub16':  'int32_t lo=((int16_t)a-(int16_t)b)>>1, hi=((int16_t)(a>>16)-(int16_t)(b>>16))>>1; return (int32_t)((uint32_t)(hi<<16)|(lo&0xFFFF));',
 'smlawb':   'return (int32_t)(sum + (int32_t)(((int64_t)a*(int16_t)b)>>16));',
 'smlawt':   'return (int32_t)(sum + (int32_t)(((int64_t)a*(int16_t)(b>>16))>>16));',
 'smuad':    'return (int16_t)a*(int16_t)b + (int16_t)(a>>16)*(int16_t)(b>>16);',
 'smuadx':   'return (int16_t)a*(int16_t)(b>>16) + (int16_t)(a>>16)*(int16_t)b;',
 'smlald':   'return sum + (int64_t)((int16_t)a*(int16_t)b) + (int64_t)((int16_t)(a>>16)*(int16_t)(b>>16));',
 'smlaldx':  'return sum + (int64_t)((int16_t)a*(int16_t)(b>>16)) + (int64_t)((int16_t)(a>>16)*(int16_t)b);',
 'and':      'return a & b;',
}
def asm_body(m):
    name = m.group(2)
    if name not in C:
        return m.group(0)
    return m.group(1) + '{\n    ' + C[name] + '\n}'
# every function body that is only an asm statement: "{ [int32_t out;] asm volatile("name ...") ... return out; }"
t = re.sub(r'(static inline [^{;]*\))\s*\{\s*(?:int32_t out;\s*)?(?:asm|__asm__)(?: volatile)?\s*\(\s*"(\w+)[^;]*;\s*return [^;]*;\s*\}', lambda m: m.group(1) + '\n{\n    ' + C[m.group(2)] + '\n}' if m.group(2) in C else m.group(0), t)
# Teensy's Cortex-M0 branch leaves five helpers as "return 0; // TODO....": on the host every mixer output (saturate16) and every
# 32x32 multiply (SMMUL family: limiter, filters, polyverb) was silence. C bodies with the Cortex-M4 instruction semantics
# (SSAT #16, SMMUL, SMMULR, SMMLAR, SMMLSR: the 64 bit product, rounding adds 0x80000000 before the high word is taken).
# Same overlay as tracker-ff pluck-sim 8796b91. The Skip-back sim (skipback_sim.sh) needs a master output that is not silence
TODO_BODIES = {
 'saturate16': 'return (int16_t)(val > 32767 ? 32767 : (val < -32768 ? -32768 : val));',
 'multiply_32x32_rshift32': 'return (int32_t)(((int64_t)a * b) >> 32);',
 'multiply_32x32_rshift32_rounded': 'return (int32_t)(((int64_t)a * b + 0x80000000LL) >> 32);',
 'multiply_accumulate_32x32_rshift32_rounded': 'return (int32_t)((((int64_t)sum << 32) + (int64_t)a * b + 0x80000000LL) >> 32);',
 'multiply_subtract_32x32_rshift32_rounded': 'return (int32_t)((((int64_t)sum << 32) - (int64_t)a * b + 0x80000000LL) >> 32);',
}
for name, body in TODO_BODIES.items():
    t, n = re.subn(r'(static inline [^;{]*\b' + name + r'\([^)]*\)\s*\{(?:(?!\n\}).)*?#elif 1\s*\n)\s*return 0; // TODO\.\.\.\.', lambda m: m.group(1) + '    ' + body, t, flags=re.S)
    if n != 1:
        sys.exit('gen_overlay: dspinst.h %s TODO body no longer matches' % name)
if 'TODO....' in t:
    sys.exit('gen_overlay: dspinst.h has a TODO body this overlay does not replace')
put(os.path.join(out, 'libs/Audio/utility/dspinst.h'), t)
pu = os.path.join(out, 'libs/Audio/Polyverb/util.h')
pt = open(pu, encoding='latin-1', newline='').read()
pt, n = re.subn(r'__asm\("ssat %0, %1, %2" : "=r"\(result\) : "I"\(16\), "r"\(x\)\);', 'result = x > 32767 ? 32767 : (x < -32768 ? -32768 : x);', pt)
if n != 1:
    sys.exit('gen_overlay: Polyverb/util.h ssat no longer matches')
put(pu, pt)

SOURCES = {
    'core/source/Tracker.cpp': [SECTION],
    # pointer differences: the 32 bit cast of a pointer is an error on a 64 bit host
    'core/source/mtConfig.cpp': [(re.compile(r'\(uint32_t\)(&|eepromStructsPtr)'), r'(uint32_t)(uintptr_t)\1')],
    # case labels through the SR pointer: gcc folds them, clang wants the class constants
    'core/interface/sampleRecorder/fSampleRec.cpp': [(re.compile(r'case SR->'), 'case cSampleRecorder::')],
    'core/libs/Audio/effect_polyverb.cpp': [SECTION],
    # plug_host.cpp: the section attribute, plus what only the K66 can do. The core registers (LMEM cache control, DWT cycle counter, system
    # MPU) point at a plain variable, the cache and barrier instructions go, and the descriptor of a loaded plugin comes from the plugin's
    # native build (host_twins.cpp, the same source as the .plg) instead of from running its Thumb code
    'synth-open/plug_host.cpp': [SECTION,
        (re.compile(r'reinterpret_cast<volatile uint32_t\*>\((?:0xE0001004|0x4000D000|0xE0082000)\)'), 'uishot_fake_reg()'),
        (re.compile(r'asm volatile\("[a-z]+ 0xf" ::: "memory"\);'), ';'),
        (re.compile(r'while \(\*pcccr & 0x80000000u\) \{\}'), ';'),
        (re.compile(r'(#include "plug_host.h")'), '\\1\nextern "C" volatile uint32_t* uishot_fake_reg();\nextern "C" const plug_desc* uishot_twin(uint32_t uid, const plug_host* host);'),
        (re.compile(r'const plug_desc\* d = entry\(&s_host, \(PLUG_ABI_MAJOR << 16\) \| PLUG_ABI_MINOR\);'), 'const plug_desc* d = uishot_twin(h.uid, &s_host); (void)entry;'),
        (re.compile(r'e = checkDesc\(d, pl.textRun, rwOff, wantKind\);'), 'e = d->kind == wantKind ? kOk : kDesc;'),
        # PLUG_EXT_TEMPO of the native twin: its pointers are 64 bit host code (no Thumb bit, no image range)
        (re.compile(r'out\.tempo = tempoExt\(d, pl\.textRun, rwOff\);'),
         'out.tempo = d->kind == PLUG_KIND_MASTER_INSERT && d->query ? static_cast<const plug_ext_tempo*>(d->query(PLUG_EXT_TEMPO)) : nullptr;'),
        (re.compile(r'out\.tail = tailExt\(d, pl\.textRun, rwOff\);'),
         'out.tail = d->kind == PLUG_KIND_MASTER_INSERT && d->query ? static_cast<const plug_ext_tail*>(d->query(PLUG_EXT_TAIL)) : nullptr;')],
    # plug_insert.cpp: the insert node runs on the host when the script says "audio 1" (plug_tempo_sim.sh): the DWT cycle counter is a plain variable
    'synth-open/plug_insert.cpp': [
        (re.compile(r'reinterpret_cast<volatile uint32_t\*>\(0xE0001004\)'), 'uishot_fake_reg()'),
        (re.compile(r'(#include "plug_host.h")'), '\\1\nextern "C" volatile uint32_t* uishot_fake_reg();')],
    'synth-open/open_cold.cpp': [SECTION],
    # newlib's strchr returns char*, libc++'s overloads return const char* for a const char* argument
    'core/AudioEngine/mtAudioEngine.cpp': [(re.compile(r'\(\(uint32_t\)this - \(uint32_t\)instrumentPlayer\)'), '((uint32_t)(uintptr_t)this - (uint32_t)(uintptr_t)instrumentPlayer)')],
    # the source pointer of the old-version copy is a 32 bit integer turned back into a pointer: on the host that cut the stack address
    # (above 4 GB) and the copy read a wild address, so the pointer stays a pointer (uintptr_t). The offset is a plain difference
    'core/FileManager/mtFileManagerOldVersionRecovery.cpp': [
        (re.compile(r'\(uint8_t\*\)\(\(\(uint32_t\)&tmp\.values\.limiterAttack\) - 4\)'), '(uint8_t*)(((uintptr_t)&tmp.values.limiterAttack) - 4)'),
        (re.compile(r'\(uint32_t\)&'), r'(uint32_t)(uintptr_t)&')],
    # synthEnsure installs an engine of the library into the FlexNVM bank at 0x10000000, an address the host does not have: no engine
    # installs, the part says Missing: <name> as on a card whose bank could not be written. Every engine plugin stays Missing in the plain
    # build, the BRIDGE=1 build below runs the real .plg bytes. The bank reads (bankOpen's CRC, bankInstall's compare before a program)
    # go to an erased host copy of the bank: read through the device address they faulted the plain build on the first plugin engine
    # (engines.picker, since the twins retired in 359cd994). The host nvmProgram fails, so the install stops at its first phrase
    'synth-open/plug_synth.cpp': [
        (re.compile(r'const uint8_t\* at\(uint32_t off\) \{ return reinterpret_cast<const uint8_t\*>\(kBankRun \+ off\); \}'),
         'const uint8_t* at(uint32_t off) { static uint8_t erased[kBankBytes]; static const bool once = (memset(erased, 0xFF, sizeof(erased)), true); (void)once; return erased + off; }')],
    # same-directory quote include of plug_abi.h would pick the original: a copy, found through GEN/inc
    'synth-open/plug/fx_dirt_plug.cpp': [],
    # the three test accessors of host_smoke_test.cpp (OpenInstrument::testParams/testModOut, OpenSynth::instrumentAt, open::testParams/testModOut): they
    # only read, the stress test (the console, host_stress.cpp) reads the engine's block params and the automation outputs through them. Not
    # OPEN_HOST_TEST itself, that also removes the audio lock of the patch code
    'synth-open/open_synth.cpp': [
        (re.compile(r'#ifdef OPEN_HOST_TEST\n(const ot::audio::SynthParams\* open::testParams)'),
         'namespace open { const ot::audio::SynthParams* testParams(int part); float testModOut(int part, int slot); bool testVoice(int part, int slot, bool& gate, int& note, int& id); }\n#ifdef OPEN_HOST_TEST\n\\1'),
        (re.compile(r'#ifdef OPEN_HOST_TEST'), '#if 1 /* ui_shot: read only test accessors */'),
        # `poke voices` (host_poke.cpp, synth_nna_sim.sh): the voices of a part as text, appended at the end of the file (it reads the Voice record
        # of the anonymous namespace)
        (re.compile(r'\Z'), HOST_VOICES),
        # `poke renderhash` (host_poke.cpp): a hash of every block the shim renders for a part, the bit exact reference of synth_nna_sim.sh
        (re.compile(r'\A'), 'extern "C" void uishotHashBlock(int part, const float* l, const float* r);\n'),
        # a mono engine (EngineDef::monoOut) leaves R alone: its R is the L it always equalled, so the hashes recorded before L1 stay valid
        (re.compile(r'(inst->render\(l, r, busyKnown\);)'), r'\1\n            uishotHashBlock(instrumentId, l, mono ? l : r);')],
    # ptfx_loader.cpp: the host cannot run the Thumb code of a .ptfx (the plain build keeps the effects built in, the BRIDGE=1 build below
    # runs the card files): the instruction cache sync (K66 registers) is dropped
    'core/effector/ptfx_loader.cpp': [
        (re.compile(r'    syncInstructionCache\(codeBuf, totalCodeSize\);'), '    (void)totalCodeSize;   // ui_shot: no K66 cache registers')],
    # fSampleEdit.cpp: an apply on the unit takes from a few ms to seconds (the effect is stepped in update(), "Applying...", the functions
    # cleared, every key lost), the host a few loop passes. While an apply is in progress the steps of the effect wait until the unit's time
    # for that effect list row has passed in virtual time (systick_millis_count) since it began. kUnitApplyMs: measured on the unit
    # (12261be, 2026-11-17, demo kick.wav 30912 B, default parameters, Apply tap to "Applying..." gone, texts polled, 3 runs of the slow rows
    # within 30 ms): Normalizer 0.03 s, Crop 0.02, Reverse 0.02, Amplifier 0.05, Overdrive 0.09, Delay 4.57, Bitcrusher 0.03, Chorus 0.05,
    # Flanger 0.20, Equalizer 7.56 (the console is silent the first 4 s), Limiter 0.12, Compressor 0.20, WT Smoother 0.47, Timestretch 1.29.
    # Each hold is that time rounded up (100 ms at least), Delay keeps 5000: the flow version that typed into the Delay apply (devtools
    # 4da5852) drops the same keys on the emu and saves the unit's file byte for byte (instrument_13.pti 419342 B crc 8b287569), 4000 ms let
    # one key through. A longer sample takes longer on the unit: flows wait with `expect no-text "Applying..."`, not a fixed time.
    # UISHOT_APPLY_MS=N holds every row N ms (0 = off). Preview processing (no apply) is not held. Reviewed in emu/overlay-allowlist.txt
    'core/interface/sampleEditor/fSampleEdit.cpp': [
        (re.compile(r'\A'), APPLY_HOLD),
        (re.compile(r'^        currentEffect->update\(\);$', re.M), '        if (!uishotApplyHold(applyingInProgress, selectedEffectListIdx)) currentEffect->update();')],
    'core/libs/NewDisplay/textBox.cpp': [(re.compile(r'char\* (enter_pos|space_pos) = strchr'), r'const char* \1 = strchr')],
    # ext_module.cpp (FEATURE_EXT_HOOKS): the host runs the loader rules of modit.plg (header, layout, run address, file CRC, image id) and then binds the native
    # MOD/IT. The two values the rules compare with are those of the release image the file is bound to: the image id (CRC of the flash the .ptf programs) and the
    # run address of the image's extension slot. A build that knows the release passes them (UISHOT_MODIT_ID, UISHOT_MODIT_RUN, emu-release.sh reads them from the
    # release's modit.plg and checks that file against the release .ptf), so the card's real modit.plg is the file that passes. Without them the host keeps its
    # stand-in values (image id 0, run 0x70D88000: tools/modit/make_host_module.py writes a file for those)
    'synth-open/ext_module.cpp': [
        (re.compile(r'uint32_t imageCrc\(\) \{ return 0; \}'),
         '#ifndef UISHOT_MODIT_ID\n#define UISHOT_MODIT_ID 0u\n#endif\nuint32_t imageCrc() { return UISHOT_MODIT_ID; }'),
        (re.compile(r'constexpr uintptr_t kModitHostRun = 0x70D88000u;'),
         '#ifndef UISHOT_MODIT_RUN\n#define UISHOT_MODIT_RUN 0x70D88000u\n#endif\nconstexpr uintptr_t kModitHostRun = UISHOT_MODIT_RUN;')],
}
# BRIDGE=1 (UISHOT_BRIDGE=1 from the Makefile, emu parity plan T2): plug_host.cpp loads the REAL insert .plg bytes and runs them under
# libunicorn (host_ucbridge.cpp) instead of the native twins. The SRAM_L insert slot and the insert arenas are the guest windows at the
# release axf's own addresses, hostAlloc's heap is the guest heap, the run address of a placed image is its device address, the entry
# call, plug::checkDesc, tempoExt and tailExt are the release image's own Thumb code, and the descriptor and its extensions reach the native
# firmware as host mirrors with thunks. hostAlloc, hostCycles and hostQuery stay native: the image's s_host traps to them. Without
# BRIDGE=1 these rules are not applied and the overlay is byte for byte what it was.
BRIDGE_PLUG_HOST = [SECTION,
    (re.compile(r'reinterpret_cast<volatile uint32_t\*>\((?:0xE0001004|0x4000D000|0xE0082000)\)'), 'uishot_fake_reg()'),
    (re.compile(r'asm volatile\("[a-z]+ 0xf" ::: "memory"\);'), ';'),
    # syncCode is the unit's cache invalidation after code was written: every Unicorn lane drops its translated blocks
    (re.compile(r'while \(\*pcccr & 0x80000000u\) \{\}'), 'ucb::codeChanged();'),
    (re.compile(r'(#include "plug_host.h")'), '\\1\n#include "host_ucbridge.h"\nextern "C" volatile uint32_t* uishot_fake_reg();'),
    # the slot and the arenas at their device addresses (the axf's symbols), so the guest sees what the firmware writes
    (re.compile(r'__attribute__\(\(.*\)\) uint8_t s_slot\[kSlotBytes\];'),
     'uint8_t (&s_slot)[kSlotBytes] = *reinterpret_cast<uint8_t (*)[kSlotBytes]>(ucb::symWindow("plug::(anonymous namespace)::s_slot", kSlotBytes));'),
    (re.compile(r'__NOINIT\(EXTERNAL_RAM\) __attribute__\(\(aligned\(8\)\)\) uint8_t s_arena\[kSlots\]\[kArenaPerSlot\];'),
     'uint8_t (&s_arena)[kSlots][kArenaPerSlot] = *reinterpret_cast<uint8_t (*)[kSlots][kArenaPerSlot]>(ucb::symWindow("plug::(anonymous namespace)::s_arena", kSlots * kArenaPerSlot));'),
    # hostAlloc's SRAM heap (malloc on the unit) is the guest heap, unload frees it there
    (re.compile(r'void\* p = malloc\(bytes\);   // SRAM heap'), 'void* p = ucb::heapAlloc(bytes);   // SRAM heap'),
    (re.compile(r'for \(uint8_t i = 0; i < s\.nHeap; i\+\+\) free\(s\.heap\[i\]\);'), 'for (uint8_t i = 0; i < s.nHeap; i++) ucb::heapFree(s.heap[i]);'),
    # the native services behind the image's patched hostAlloc, hostCycles, hostQuery, and the Strum depth a plugin reads through query
    (re.compile(r'(const plug_host s_host = \{[^}]*\};)'),
     '\\1\nconst bool s_ucbServices = (ucb::setServices({hostAlloc, hostCycles, hostQuery}),\n    ucb::shadow(ucb::sym("strumramp::g_depth", sizeof(strumramp::g_depth)), &strumramp::g_depth, sizeof(strumramp::g_depth)), true);'),
    # host.txt prints the device text address of process() and the guest instance, as the unit does
    (re.compile(r'\(unsigned long\)\(s\.desc \? uintptr_t\(s\.desc->process\) & ~1u : 0\), \(unsigned long\)uintptr_t\(s\.inst\)'),
     '(unsigned long)(s.desc ? ucb::guestProcess(s.desc) & ~1u : 0), (unsigned long)ucb::h2gOr0(s.inst)'),
    # the insert slot runs at its device address
    (re.compile(r'pl\.textRun = uintptr_t\(s_slot \+ at\);'), 'pl.textRun = ucb::h2gOr0(s_slot + at);'),
    # entry call, checkDesc, tempoExt and tailExt: image calls on the guest descriptor, then the host mirror
    (re.compile(r'const plug_desc\* d = entry\(&s_host, \(PLUG_ABI_MAJOR << 16\) \| PLUG_ABI_MINOR\);'),
     'const uint32_t gd = ucb::entry(uint32_t(pl.textRun + h.entry_offset));   // plug_entry(&s_host, ABI), image s_host\n    (void)entry;\n    const plug_desc* d = gd ? ucb::mirrorDesc(gd) : nullptr;'),
    (re.compile(r'e = checkDesc\(d, pl\.textRun, rwOff, wantKind\);'), 'e = static_cast<Err>(ucb::checkDesc(gd, uint32_t(pl.textRun), rwOff, wantKind));'),
    (re.compile(r'out\.tempo = tempoExt\(d, pl\.textRun, rwOff\);'), 'out.tempo = ucb::mirrorTempo(ucb::tempoExt(gd, uint32_t(pl.textRun), rwOff));'),
    (re.compile(r'out\.tail = tailExt\(d, pl\.textRun, rwOff\);'), 'out.tail = ucb::mirrorTail(ucb::tailExt(gd, uint32_t(pl.textRun), rwOff));')]
# BRIDGE=1, plug_synth.cpp (emu parity plan T5): the synth engine plugins of the card install and attach through the firmware's own path.
# synthEnsure -> loadEngine -> bankInstall writes the FlexNVM bank through nvmErase/nvmProgram (host_nvm.cpp: a model on the bridge's
# window at 0x10000000 instead of the plain build's stubs that refuse every command) and reads it back through that window, s_pool (rw
# and bss of the installed engines) and s_work (their work memory) are the guest windows at the release axf's own addresses, and
# start() is host_synthcheck.cpp's ucbs::start: the image's plug_entry, plug::checkDesc, init and query under libunicorn plus the ports of
# checkSynth and checkChord (inlined in the image, host_synthcheck_test.cc compares their verdicts with the image's own start()). The
# extension reaches the firmware as the bridge's host mirror. kWorkBytes is the device's 20 KB: the real .plg bytes run, or the part says why not
BRIDGE_START = r"""    ucbs::StartIn in{uint32_t(kBankRun + e.off), e.text, e.len, e.entry, e.rwRun, e.rwLen, s_sidx[k].synthStored, s_sidx[k].synthPatch,
                     s_sidx[k].synthType, s_lib[k].name, sizeof(s_lib[k].name)};
    ucbs::StartOut o;
    const int r = ucbs::start(in, o);   // ui_shot BRIDGE=1: the image's calls, the ports of checkSynth and checkChord (host_synthcheck.cpp)
    if (o.fault) LOGEF("syn%d fault", k);
    if (o.chordSet)
    {
        s_lib[k].chord = o.chord;
        s_lib[k].mode = o.mode;
        s_lib[k].strum = o.strum;
    }
    if (r) return r;
    const plug_ext_synth* x = ucb::mirrorSynth(o.ext);
    if (!x) return 4;
    *out = x;
    return 0;
}"""
BRIDGE_RENDER_HELPERS = r"""// ui_shot BRIDGE=1 (host_plugrender.cpp): the PLUG_EXT_SYNTH of the attached library engine of type typeId (the bridge's host mirror),
// nullptr when none is attached
const plug_ext_synth* uishotSynthExt(int typeId)
{
    for (int k = 0; k < s_sn; k++)
        if (def(k).typeId == typeId && s_lib[k].attached) return eng(k).x;
    return nullptr;
}
// The engine's work block zeroed and handed over again (giveWork), what the unit console's plugrender does at its start: an engine that
// keeps state in its work memory (PLK's string pool) renders a transcript from the state of a fresh attach. 1 done, 0 no work memory,
// -1 not attached or no block
int uishotSynthWorkReset(int typeId)
{
    for (int k = 0; k < s_sn; k++)
    {
        if (def(k).typeId != typeId || !s_lib[k].attached) continue;
        const plug_ext_synth* x = eng(k).x;
        if (!x) return -1;
        if (!x->work_bytes) return 0;
        if (!x->set_work || !s_arena.len[k]) return -1;
        giveWork(k, x);
        return 1;
    }
    return -1;
}
"""
BRIDGE_PLUG_SYNTH = [
    (re.compile(r'(#include "plug/plug_chord.h")'), '\\1\n#include "host_nvm.h"\n#include "host_synthcheck.h"\n#include "host_ucbridge.h"'),
    (re.compile(r'__attribute__\(\(aligned\(8\)\)\) uint8_t s_pool\[kPoolBytes\];'),
     'uint8_t (&s_pool)[kPoolBytes] = *reinterpret_cast<uint8_t (*)[kPoolBytes]>(ucb::symWindow("plug::(anonymous namespace)::s_pool", kPoolBytes));'),
    (re.compile(r'__NOINIT\(EXTERNAL_RAM\) __attribute__\(\(aligned\(8\)\)\) uint8_t s_work\[kWorkBytes\];'),
     'uint8_t (&s_work)[kWorkBytes] = *reinterpret_cast<uint8_t (*)[kWorkBytes]>(ucb::symWindow("plug::(anonymous namespace)::s_work", kWorkBytes));'),
    # the bank is read where the guest runs it
    (re.compile(r'const uint8_t\* at\(uint32_t off\) \{ return reinterpret_cast<const uint8_t\*>\(kBankRun \+ off\); \}'),
     'const uint8_t* at(uint32_t off) { return nvmHostAt(off); }'),
    # rwRun of a bank entry is the guest address in s_pool (the image is rebased for it, installed.txt records it as the unit does)
    (re.compile(r'uintptr_t\(s_pool\)'), 'uintptr_t(ucb::h2gOr0(s_pool, kPoolBytes))'),
    (re.compile(r'(int start\(int k, const plug_ext_synth\*\* out\)\n\{\n    const int bi = bankOf\(k\);\n    if \(bi < 0\) return 7;\n'
                r'    const BankEntry& e = s_bank\.e\[bi\];\n).*?    \*out = x;\n    return 0;\n\}', re.S), lambda m: m.group(1) + BRIDGE_START),
    # emu parity T7, host_plugrender.cpp (the ui_shot plugrender verb): the attached engine's extension (service, the descriptor) and the
    # work reset the unit console's plugrender does before a transcript (devtools UsbTest.cpp renderOpen: the block zeroed, set_work again)
    (re.compile(r'^\}   // namespace plug$', re.M), lambda m: BRIDGE_RENDER_HELPERS + m.group(0))]
# BRIDGE=1, emu parity plan T6: the master insert node and the Sample Editor .ptfx loader run the REAL plugin bytes.
# plug_insert.cpp: the node (AudioEffectPlug::update, the audio interrupt on the unit) runs on the insert lane of the bridge, so every guest
# call of the block (the tempo and tail extensions, set_param, process with the io pointer pair, all thunks of host_ucbridge.cpp) is one
# Unicorn instance on one thread, the parts' lanes stay free. The plugin_host.cpp rules (T2) already put the insert text in the SRAM_L slot
# at its device address, so there is no twin to unhook here: with BRIDGE=1 plug::load never asks uishot_twin().
BRIDGE_PLUG_INSERT = [
    (re.compile(r'reinterpret_cast<volatile uint32_t\*>\(0xE0001004\)'), 'uishot_fake_reg()'),
    (re.compile(r'(#include "plug_host.h")'), '\\1\n#include "host_ucbridge.h"\nextern "C" volatile uint32_t* uishot_fake_reg();'),
    (re.compile(r'(void AudioEffectPlug::update\(void\)\n\{\n)'), '\\1    ucb::LaneScope ucbLane(ucb::kInsert);   // the insert chain\'s Unicorn instance\n')]
# ptfx_loader.cpp: the 128 KB code buffer and the 640 KB scratch are the guest window of externalRamScratchBuff at its device address (the
# release axf's symbol), the file is read into it by the real loader code, syncInstructionCache is the bridge's translation flush, and the three
# entry points are native thunks of the guest functions (mirrorPtfx: init, set_param, process_block with the int16 sample buffers aliased,
# the .ptfx host API is the image's own s_ptfxHostApi with newlib libm as Thumb code). The one function of that table that touches
# firmware state, set_extra_samples (hostSetExtraSamples of this file), is answered natively through an override of the image's function.
BRIDGE_PTFX_LOADER = [
    (re.compile(r'(#include "ptfx_loader.h")'), '\\1\n#include "host_ucbridge.h"'),
    (re.compile(r'(extern uint8_t externalRamScratchBuff\[\];)'),
     '\\1\n// the window of externalRamScratchBuff at its device address: the guest sees the code and the scratch where the unit has them\n'
     'static uint8_t* ucbScratch()\n{\n    static uint8_t* const w = ucb::symSize("externalRamScratchBuff") >= kPtfxScratchTopOffset\n        ? ucb::symWindow("externalRamScratchBuff", ucb::symSize("externalRamScratchBuff")) : externalRamScratchBuff;\n    return w;\n}'),
    (re.compile(r'(    sampleEditorModular\.setExtraSamples\(prependSamples, appendSamples\);\n\})'),
     '\\1\n// the image\'s s_ptfxHostApi.set_extra_samples is this function of the release image: native here (r0 = prepend, r1 = append)\n'
     'static void ucbSetExtraSamples(void* uc)\n{\n    hostSetExtraSamples(ucb::reg(uc, 0), ucb::reg(uc, 1));\n}\n'
     '[[maybe_unused]] static const bool s_ucbPtfxApi = ucb::override(ucb::sym("hostSetExtraSamples"), ucbSetExtraSamples);'),
    (re.compile(r'    return externalRamScratchBuff;'), '    return ucbScratch();'),
    (re.compile(r'reinterpret_cast<void\*>\(externalRamScratchBuff \+ kPtfxScratchOffset\)'), 'reinterpret_cast<void*>(ucbScratch() + kPtfxScratchOffset)'),
    (re.compile(r'    syncInstructionCache\(codeBuf, totalCodeSize\);'), '    ucb::codeChanged();   // ui_shot: the K66 cache registers are the translation flush of every Unicorn lane\n    (void)totalCodeSize;'),
    (re.compile(r'    uintptr_t base = reinterpret_cast<uintptr_t>\(codeBuf\);'), '    const uintptr_t base = ucb::h2gOr0(codeBuf);   // the guest address of the code\n    if (!base) return false;'),
    (re.compile(r'(    outProcessBlock = reinterpret_cast<PtfxProcessBlockFunc>\(\(base \+ outHeader\.process_block_offset\) \| 0x1\);)'),
     '\\1\n    ucb::mirrorPtfx(uint32_t((base + outHeader.init_offset) | 1u), uint32_t((base + outHeader.set_param_offset) | 1u),\n                    uint32_t((base + outHeader.process_block_offset) | 1u), outInit, outSetParam, outProcessBlock);')]
if os.environ.get('UISHOT_BRIDGE') == '1':
    SOURCES['synth-open/plug_host.cpp'] = BRIDGE_PLUG_HOST
    SOURCES['synth-open/plug_synth.cpp'] = BRIDGE_PLUG_SYNTH
    SOURCES['synth-open/plug_insert.cpp'] = BRIDGE_PLUG_INSERT
    SOURCES['core/effector/ptfx_loader.cpp'] = BRIDGE_PTFX_LOADER
for p, rules in SOURCES.items():
    wr('src', os.path.basename(p), edit(rd(p), rules, p))
