#!/usr/bin/env python3 -I
"""Host-only overlay of the firmware sources and headers that clang (macOS, 64 bit, Mach-O) cannot build as they are.

Usage: gen_overlay.py ROOT OUTDIR [EXTDIR]. EXTDIR holds the module sources ui_shot.sh fetched from the pinned module repositories
(fx_dirt_plug.cpp of poly-effects-os). Writes OUTDIR/inc/<header> and OUTDIR/src/<source> (LF, edited copies). Nothing under
core/ or synth-open/ is touched: the Makefile compiles the copies instead of the originals and puts OUTDIR/inc first.
Every edit is listed here with its reason, a source that changes upstream and no longer matches an edit fails loudly.
"""
import os, re, sys

root, out = sys.argv[1], sys.argv[2]
ext = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(out), 'external')
# module sources that are not in this repository: read from EXTDIR (engines/external.py source), path in the SOURCES table -> file name there
EXTERNAL = {'synth-open/plug/fx_dirt_plug.cpp': 'fx_dirt_plug.cpp'}
DIRT_STUB = ('// stub: fx_dirt_plug.cpp of poly-effects-os was not available (ui_shot.sh fetches it), Dirt fails to load on the pages\n'
             '#include "plug_abi.h"\nextern "C" const plug_desc* plug_entry_dirt(const plug_host*, uint32_t) { return nullptr; }\n')
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
    # copied unchanged: its quote include of plug_abi.h must find the copy above, not the original next to it (host_chip_twin.cpp)
    'synth-open/plug/synth_plug.h': [],
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
        (re.compile(r'e = checkDesc\(d, pl.textRun, rwOff, wantKind\);'), 'e = d->kind == wantKind ? kOk : kDesc;')],
    'synth-open/open_cold.cpp': [SECTION],
    # newlib's strchr returns char*, libc++'s overloads return const char* for a const char* argument
    'core/AudioEngine/mtAudioEngine.cpp': [(re.compile(r'\(\(uint32_t\)this - \(uint32_t\)instrumentPlayer\)'), '((uint32_t)(uintptr_t)this - (uint32_t)(uintptr_t)instrumentPlayer)')],
    'core/FileManager/mtFileManagerOldVersionRecovery.cpp': [(re.compile(r'\(uint32_t\)&'), r'(uint32_t)(uintptr_t)&')],
    # synthEnsure installs an engine of the library into the FlexNVM bank at 0x10000000, an address the host does not have: no engine
    # installs, the part says Missing: <name> as on a card whose bank could not be written
    # CHIP is the one engine with a native twin (host_chip_twin.cpp, the plugin's engine_chip.cpp and the Polyend voice compiled for the host,
    # CHIP_PLUGIN_SRC=... in the Makefile): its library entry attaches from the twin's extension instead of the bank. Without the twin source
    # uishotTwinExt returns null and the part says Missing: CHIP like every other engine plugin
    'synth-open/plug_synth.cpp': [
        (re.compile(r'if \(!s_lib\[k\]\.attached && !loadEngine\(k, want, nWant, keep, nKeep\)\) ok = false;'),
         'if (!s_lib[k].attached && !(uishotTwinExt(s_sidx[k].synthStored) && attachMarked(k, Need{want, nWant, keep, nKeep}) == 0)) ok = false;'),
        (re.compile(r'(// Runs the installed engine k from its first byte)'),
         'extern "C" const plug_ext_synth* uishotTwinExt(int stored);   // host_chip_twin.cpp\n\\1'),
        (re.compile(r'(    const int bi = bankOf\(k\);\n    if \(bi < 0\) return 7;\n    const BankEntry& e = s_bank\.e\[bi\];)'),
         '    if (const plug_ext_synth* tw = uishotTwinExt(s_sidx[k].synthStored)) { *out = tw; return 0; }\n\\1')],
    # same-directory quote include of plug_abi.h would pick the original: a copy, found through GEN/inc
    'synth-open/plug/fx_dirt_plug.cpp': [],
    # the three test accessors of host_smoke_test.cpp (OpenInstrument::testParams/testModOut, OpenSynth::instrumentAt, open::testParams/testModOut): they
    # only read, the stress test (poly-devtools host_stress.cpp) reads the engine's block params and the automation outputs through them. Not
    # OPEN_HOST_TEST itself, that also removes the audio lock of the patch code
    'synth-open/open_synth.cpp': [
        (re.compile(r'#ifdef OPEN_HOST_TEST\n(const ot::audio::SynthParams\* open::testParams)'),
         'namespace open { const ot::audio::SynthParams* testParams(int part); float testModOut(int part, int slot); }\n#ifdef OPEN_HOST_TEST\n\\1'),
        (re.compile(r'#ifdef OPEN_HOST_TEST'), '#if 1 /* ui_shot: read only test accessors */')],
    'core/libs/NewDisplay/textBox.cpp': [(re.compile(r'char\* (enter_pos|space_pos) = strchr'), r'const char* \1 = strchr')],
}
for p, rules in SOURCES.items():
    wr('src', os.path.basename(p), edit(rd(p), rules, p))
