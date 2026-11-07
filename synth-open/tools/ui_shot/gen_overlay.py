#!/usr/bin/env python3 -I
"""Host-only overlay of the firmware sources and headers that clang (macOS, 64 bit, Mach-O) cannot build as they are.

Usage: gen_overlay.py ROOT OUTDIR. Writes OUTDIR/inc/<header> and OUTDIR/src/<source> (LF, edited copies). Nothing under
core/ or synth-open/ is touched: the Makefile compiles the copies instead of the originals and puts OUTDIR/inc first.
Every edit is listed here with its reason, a source that changes upstream and no longer matches an edit fails loudly.
"""
import os, re, sys

root, out = sys.argv[1], sys.argv[2]
SECTION = (re.compile(r'section\([^)]*\)'), 'unused')   # GCC/ELF section names are not valid Mach-O segment,section pairs


def put(path, text):
    # only touch a file whose content changed: make rebuilds on mtime
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and open(path, encoding='latin-1', newline='').read() == text:
        return
    with open(path, 'w', encoding='latin-1', newline='') as f:
        f.write(text)


def rd(p):
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
    'synth-open/plug_synth.cpp': [(re.compile(r'if \(!s_lib\[k\]\.attached && !loadEngine\(k, want, nWant, keep, nKeep\)\) ok = false;'), 'if (!s_lib[k].attached) ok = false;')],
    # same-directory quote include of plug_abi.h would pick the original: a copy, found through GEN/inc
    'synth-open/plug/fx_dirt_plug.cpp': [],
    'core/libs/NewDisplay/textBox.cpp': [(re.compile(r'char\* (enter_pos|space_pos) = strchr'), r'const char* \1 = strchr')],
}
for p, rules in SOURCES.items():
    wr('src', os.path.basename(p), edit(rd(p), rules, p))
