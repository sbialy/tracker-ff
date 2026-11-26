#!/usr/bin/env python3 -I
"""Tracker+ simulator, phase 3: render one note of a STOCK Tracker+ synth engine (the closed libPolySynth) to a WAV under Unicorn.

Usage: run.py HARNESS.elf HARNESS.nm ENGINE OUT.wav [--note 60] [--vel 100] [--on-ms 500] [--off-ms 400] [--voices 1]
ENGINE: acd fat vap wtfm perc dirt (or a polysynth::InstrumentTypeId number). Prints peak and rms, exit 0 when the render ran.
Python of ~/prj/poly/build/pyenv (unicorn 2.1.4). Host tooling only: the lib runs unmodified as a black box (never disassembled).
"""
import argparse, math, struct, sys, wave

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_PROT_ALL, UC_HOOK_MEM_INVALID
from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_MAX)

TYPES = {'acd': 1, 'fat': 2, 'vap': 4, 'wtfm': 5, 'perc': 6, 'dirt': 7}
RAM, RAM_SIZE = 0x20000000, 32 << 20
RET = 0x00010000            # return trap: the runner stops when the callee returns here
BLOCK = 128
RATE = 44100                # the unit runs 44,117.6 Hz, a WAV header of 44,100 as ui_shot writes


def load_elf(uc, path):
    d = open(path, 'rb').read()
    assert d[:4] == b'\x7fELF' and d[4] == 1, 'not a 32 bit ELF'
    phoff, = struct.unpack_from('<I', d, 28)
    phentsize, phnum = struct.unpack_from('<HH', d, 42)
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _paddr, p_filesz, p_memsz, _flags, _align = struct.unpack_from('<8I', d, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            uc.mem_write(p_vaddr, d[p_offset:p_offset + p_filesz])


def symbols(nm_path):
    out = {}
    for line in open(nm_path):
        f = line.split()
        if len(f) == 3:
            out[f[2]] = int(f[0], 16)
    return out


class Guest:
    def __init__(self, elf, nm):
        self.sym = symbols(nm)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        # ARMv8 AArch32 model: has every Cortex-M7 FPv5 instruction the lib may use (VRINT, VSEL, VMAXNM, VCVTA), Thumb-2 as on the unit
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        uc.mem_map(RAM, RAM_SIZE, UC_PROT_ALL)
        uc.mem_map(RET, 0x1000, UC_PROT_ALL)
        uc.mem_write(RET, b'\x00\xbf' * 8)   # NOPs, never executed (emu_start stops at RET)
        v = uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20)   # CPACR: CP10, CP11 full access
        uc.reg_write(UC_ARM_REG_C1_C0_2, v)
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)        # FPEXC.EN
        load_elf(uc, elf)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._bad)
        self.uc = uc
        self.sp = RAM + RAM_SIZE - 64

    def _bad(self, uc, access, addr, size, value, _):
        print('run.py: invalid memory access at 0x%08x (pc 0x%08x, step %d)' % (addr, uc.reg_read(UC_ARM_REG_PC), self.step()), file=sys.stderr)
        return False

    def step(self):
        return struct.unpack('<i', self.uc.mem_read(self.sym['h_step'], 4))[0]

    def call(self, name, *args):
        uc = self.uc
        for r, a in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(r, a & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_SP, self.sp)
        uc.reg_write(UC_ARM_REG_LR, RET | 1)
        try:
            uc.emu_start(self.sym[name] | 1, RET)
        except UcError as e:
            pc = uc.reg_read(UC_ARM_REG_PC)
            near = max((a, n) for n, a in self.sym.items() if a <= pc)
            sys.exit('run.py: %s faulted: %s at pc 0x%08x (%s+0x%x), step %d' % (name, e, pc, near[1], pc - near[0], self.step()))
        r = uc.reg_read(UC_ARM_REG_R0)
        return r - (1 << 32) if r & 0x80000000 else r

    def floats(self, name, n):
        return struct.unpack('<%df' % n, self.uc.mem_read(self.sym[name], 4 * n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf'); ap.add_argument('nm'); ap.add_argument('engine'); ap.add_argument('wav')
    ap.add_argument('--note', type=int, default=60); ap.add_argument('--vel', type=int, default=100)
    ap.add_argument('--on-ms', type=int, default=500); ap.add_argument('--off-ms', type=int, default=400)
    ap.add_argument('--voices', type=int, default=1)
    ap.add_argument('--bend', type=int, default=None, help='a pitch bend (-8192..8191) sent to the instrument before the note')
    ap.add_argument('--scala', default=None, help='ROOT,PERIOD_MC,MC1,MC2,...: a Scala file tuning (cfg.file 1): degree d at MCd 1e-3 cent, '
                    'n = the degrees + 1, the 1/1 at the default Hz (mtTuning.h rel)')
    ap.add_argument('--tune', default=None, help='N,ROOT,PERIOD_MC,HZ_E4: the project tuning (mtTuning.h Cfg, equal steps) before the note')
    ap.add_argument('--evseen', action='store_true', help='print the MidiEvent types that reached PlugVoice (harness h_plug_evseen)')
    ap.add_argument('--chan', type=int, default=0, help='the MIDI channel of the instrument (a synth part: its part index, Synth.cpp)')
    ap.add_argument('--param', action='append', default=[], help='ID=VALUE: a patch parameter set before the note (h_param_set), repeatable')
    ap.add_argument('--chord', default=None, help='CHORD[,STRUM[,PART[,NOTE]]]: a pending MIDI Chord (h2_note_chord) right before the note on, '
                    'PART default the instrument channel, NOTE default --note')
    ap.add_argument('--no-first', action='store_true', help='no first note on (only the --seq notes play), its note off still goes out')
    ap.add_argument('--tag', type=int, default=0, help='the note id of the first note on (h2_note_tag around it), printed back as the id the voice saw')
    ap.add_argument('--chordinfo', action='store_true', help='print the PLUG_EXT_CHORD answer of the engine (h2_chord_info)')
    ap.add_argument('--seq', default=None, help='MS:on|off:NOTE[:ID[:END]][,...] extra note ons and offs at MS after the first note on, ID END: the note id and end mode '
                    'sent around it (h2_note_tag, the sequencer\'s noteTag), '
                    'printed with the probe counters (offs reached, offs that hit a gated voice, ons, ons on a sounding voice)')
    ap.add_argument('--event', action='append', default=[], help='TYPE,D0,D1 sent after the note on (h_event, channel 0), repeatable')
    ap.add_argument('--plg', help='a synth engine .plg (P7 H5): loaded into code slot 0 of the guest and played instead of ENGINE')
    ap.add_argument('--events', help='replay a plus sim part (host_plussynth.cpp UISHOT_PLUS_DUMP part<N>.events): ENGINE is ignored, '
                    'the init line gives type and voices, WAV gets the raw float32 L R frames (as part<N>.f32) instead of a WAV')
    ap.add_argument('--blocks', type=int, default=0, help='with --events: the number of blocks to render (the sim dump length)')
    a = ap.parse_args()
    if a.events:
        return replay(a)
    t = TYPES.get(a.engine.lower()) if not a.engine.isdigit() else int(a.engine)
    if t is None and not a.plg:
        sys.exit('run.py: engine %s: one of %s' % (a.engine, ' '.join(TYPES)))
    g = Guest(a.elf, a.nm)
    g.call('h_boot')
    g.uc.mem_write(g.sym['h_chan'], struct.pack('<i', a.chan))
    if a.plg:
        # the registry exists once a Synth is made (createSynth registers the types and hands PlugSynthVoice the registry): a
        # first h_init with ACD makes it, then the engine is attached and h_init runs again with its type (a new Synth gets the
        # attached engines, PlugSynthVoice.cpp setRegistry)
        r = g.call('h_init', TYPES['acd'], 1)
        if r != 0:
            sys.exit('run.py: h_init(acd) returned %d (step %d)' % (r, g.step()))
        data = open(a.plg, 'rb').read()
        g.uc.mem_write(g.sym['h2_plg'], data)
        t = g.call('h2_plug_attach', 0, len(data))
        if t < 0:
            sys.exit('run.py: h2_plug_attach(%s) returned %d' % (a.plg, t))
        print('plg %s type %d' % (a.plg, t))
    r = g.call('h_init', t, a.voices)
    if r != 0:
        sys.exit('run.py: h_init(%d) returned %d (step %d)' % (t, r, g.step()))
    frames = []
    def blocks(ms):
        for _ in range(int(ms * RATE / 1000 / BLOCK)):
            g.call('h_main')
            g.call('h_render')
            L, R = g.floats('h_outL', BLOCK), g.floats('h_outR', BLOCK)
            frames.extend(zip(L, R))
    if a.tune:
        n, root, pmc, hz = (int(x) for x in a.tune.split(','))
        if '_ZN6tuning3cfgE' in g.sym:   # absent when PlugSynthVoice takes no tuning (the guest plays the note untuned)
            g.uc.mem_write(g.sym['_ZN6tuning3cfgE'], struct.pack('<BBxxIIBBBx', n, root, pmc, hz, 0, 0, 0))
        else:
            print('run.py: the guest has no tuning::cfg, --tune ignored')
    if a.scala:
        v = [int(x) for x in a.scala.split(',')]
        root, pmc, deg = v[0], v[1], [0] + v[2:]
        g.uc.mem_write(g.sym['_ZN6tuning5degMcE'], struct.pack('<%dI' % len(deg), *deg))
        g.uc.mem_write(g.sym['_ZN6tuning3cfgE'], struct.pack('<BBxxIIBBBx', len(deg), root, pmc, 2616256, 1, 0, 0))
    for pv in a.param:
        pid, val = pv.split('=')
        g.call('h_param_set', int(pid), struct.unpack('<i', struct.pack('<f', float(val)))[0])
    if a.chordinfo:
        ci = g.call('h2_chord_info', t)
        print('chordinfo %d' % ci if ci < 0 else 'chordinfo chord %d mode %d strum %d last %d' %
              (ci & 0xFF, ci >> 8 & 0xFF, (ci >> 16 & 0xFF) - (256 if ci >> 16 & 0x80 else 0), ci >> 24 & 0xFF))
    if a.bend is not None:
        b = a.bend & 0xFFFF   # int16 little endian in the first two data bytes
        g.call('h_event', 8, 0, b & 0xFF, b >> 8)
    blocks(20)
    if a.chord:
        c = [int(x) for x in a.chord.split(',')] + [None] * 3
        part = a.chan if c[2] is None else c[2]
        g.call('h2_note_chord', part, a.note if c[3] is None else c[3], (c[0] & 0xFF) | ((c[1] or 0) & 0xFF) << 8)
    if a.tag:
        g.call('h2_note_tag', a.tag, 0)
    if not a.no_first:
        g.call('h_note', 1, a.note, a.vel)
    if a.tag:
        g.call('h2_note_tag', 0, 0)
        if 'h_plug_tag' in g.sym:
            print('evtag %d (sent %d)' % (struct.unpack('<i', g.uc.mem_read(g.sym['h_plug_tag'], 4))[0], a.tag))
    for e in a.event:
        ty, d0, d1 = (int(x) for x in e.split(','))
        g.call('h_event', ty, 0, d0, d1)
    if a.seq:
        done = 0
        for item in a.seq.split(','):
            f = item.split(':') + ['0', '0']
            ms, kind, nn, tid, end = f[0], f[1], f[2], int(f[3]), int(f[4])
            blocks(int(ms) - done); done = int(ms)
            if tid or end:
                g.call('h2_note_tag', tid, end)
            g.call('h_note', 1 if kind == 'on' else 0, int(nn), a.vel if kind == 'on' else 0)
            if tid or end:
                g.call('h2_note_tag', 0, 0)
        blocks(max(0, a.on_ms - done))
        if 'h_plug_offs' in g.sym:
            rd = lambda n: struct.unpack('<i', g.uc.mem_read(g.sym[n], 4))[0]
            print('routing offs %d offhit %d ons %d reused %d' % (rd('h_plug_offs'), rd('h_plug_offhit'), rd('h_plug_ons'), rd('h_plug_reused')))
    else:
        blocks(a.on_ms)
    g.call('h_note', 0, a.note, 0)
    blocks(a.off_ms)
    peak = max((max(abs(l), abs(r)) for l, r in frames), default=0.0)
    rms = math.sqrt(sum(l * l + r * r for l, r in frames) / (2 * len(frames))) if frames else 0.0
    nan = sum(1 for l, r in frames if l != l or r != r)
    clip = lambda x: max(-32768, min(32767, int(round((0.0 if x != x else x) * 32767))))
    with wave.open(a.wav, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<hh', clip(l), clip(r)) for l, r in frames))
    if a.evseen and 'h_plug_evseen' in g.sym:
        m = struct.unpack('<I', g.uc.mem_read(g.sym['h_plug_evseen'], 4))[0]
        names = 'clock start stop continue noteon noteoff cc pc bend polyat chanat'.split()
        print('evseen 0x%x: %s' % (m, ' '.join(n for i, n in enumerate(names) if m >> i & 1)))
        if 'h_plug_chan' in g.sym:
            print('evchan %d (instrument channel %d)' % (struct.unpack('<i', g.uc.mem_read(g.sym['h_plug_chan'], 4))[0], a.chan))
    print('plus_synth %s type %d note %d: %d frames peak %.4f rms %.5f nan %d -> %s' % (a.engine, t, a.note, len(frames), peak, rms, nan, a.wav))


def replay(a):
    """The plus sim's order for one part: events queued before a block go in first, then h_main, h_render (host_plussynth.cpp)"""
    lines = [l.split() for l in open(a.events) if l.strip()]
    assert lines[0][0] == 'init', 'events file starts with init'
    t, voices = int(lines[0][1]), int(lines[0][2])
    evs = {}
    for f in lines[1:]:   # ev BLOCK type ch d0 d1 (h_event) or par BLOCK id floatbits (h_param_set), in the sim's order
        evs.setdefault(int(f[1]), []).append((f[0],) + tuple(int(x) for x in f[2:6]))
    g = Guest(a.elf, a.nm)
    g.call('h_boot')
    r = g.call('h_init', t, voices)
    if r != 0:
        sys.exit('run.py: h_init(%d) returned %d (step %d)' % (t, r, g.step()))
    with open(a.wav, 'wb') as out:
        for b in range(a.blocks):
            for e in evs.get(b, ()):
                g.call('h_param_set' if e[0] == 'par' else 'h_event', *e[1:])
            g.call('h_main')
            g.call('h_render')
            L, R = g.floats('h_outL', BLOCK), g.floats('h_outR', BLOCK)
            out.write(b''.join(struct.pack('<ff', l, r) for l, r in zip(L, R)))
    print('plus_synth replay type %d voices %d: %d blocks, %d events -> %s' % (t, voices, a.blocks, len(lines) - 1, a.wav))


if __name__ == '__main__':
    main()
