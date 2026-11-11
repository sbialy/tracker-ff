// Random edit and render fuzz for the parameter refresh of the open shim (host, NOT firmware). Built with -DOPEN_REFRESH_CHECK every refresh
// of every part is compared bit for bit with the reference algorithm inside open_synth.cpp (abort on a difference); the printed hash covers
// every output sample, so the same test built against another tree (no check flag) must print the same hash.
//   synth-open/tools/run_refresh_equiv.sh [ENGINES] [STEPS]
#include "open_load.h"
#include "open_patch.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace polysynth {
std::unique_ptr<InstrumentType> createMidiOut();
void InstrumentTypeRegistry::registerInstrumentTypes(InstrumentTypeRegistry& r, const Synth::Allocator&) {
    r.addInstrumentType(int(InstrumentTypeId::kMidiOut), createMidiOut());
    open::registerEngines(r);
}
}
using namespace polysynth;
struct Src : MidiSource { Src():MidiSource(Type::kInternalKeyboard){} std::weak_ptr<MidiEventCallback> cb;
  void subscribeMidiEvents(int, std::weak_ptr<MidiEventCallback> c) override { cb = c; }
  void send(MidiEvent::Type t, int ch, int a, int b) { MidiEvent e{}; e.type=t; e.channel=ch; e.data.rawData.value1=a; e.data.rawData.value2=b; if (auto c = cb.lock()) (*c)(e,0);} };
static uint32_t rngState = 0x1234567u;
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
static float rndf() { return float(rnd() & 0xFFFFFF) / float(0x1000000); }
int main(int argc, char** argv) {
  const int steps = argc > 1 ? atoi(argv[1]) : 20000;
  const int ids[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, ...) int(TYPE_ID),
#include "engine_list.h"
#undef SYNTH_ENGINE
  };
  const int nEng = int(sizeof(ids) / sizeof(ids[0]));
  auto synth = Synth::createSynth([](size_t){return nullptr;});
  auto store = synth->createPatchStore(Synth::FileSystemId::kPatches, true);
  std::shared_ptr<Patch> patch[3]; std::shared_ptr<Instrument> inst[3]; Src src[3];
  std::shared_ptr<Instrument> arr[3];
  for (int k = 0; k < 3; k++) {
    patch[k] = std::shared_ptr<Patch>(store->createPatch(ids[k % nEng]));
    inst[k] = synth->createInstrument(k); inst[k]->assignPatch(patch[k]); inst[k]->connect(src[k]); inst[k]->setVoiceCount(2); inst[k]->setMidiChannel(0);
    arr[k] = inst[k];
  }
  synth->setup(std::span<std::shared_ptr<Instrument>>(arr, 3), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
  std::array<float,128> L, R, A1, A2; std::array<AudioBufferRef,2> aux{AudioBufferRef(A1),AudioBufferRef(A2)};
  uint32_t hash = 0; long blocks = 0;
  auto render = [&]() { for (int k = 0; k < 3; k++) { synth->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), k);
      for (float v : L) { uint32_t b; std::memcpy(&b, &v, 4); hash = ((hash << 5) | (hash >> 27)) ^ b; } blocks++; } };
  const bool nanOk = getenv("EQUIV_NAN") != nullptr;   // NaN values break the engines' integer conversions (undefined): only for the check build, not for hash comparisons
  const int skipOp = getenv("EQUIV_SKIP") ? atoi(getenv("EQUIV_SKIP")) : -1;   // debugging: leave one operation out
  const bool trace = getenv("EQUIV_TRACE") != nullptr;   // a line with the running hash every step: diff two builds to find the first step that differs
  for (int step = 0; step < steps; step++) {
    const int k = int(rnd() % 3);
    const uint32_t opc = rnd() % 34;
    if (trace) printf("%d %08x op=%u part=%d\n", step, hash, opc, k);
    auto& p = *patch[k]; auto& op = static_cast<open::OpenPatch&>(p);
    const auto& md = p.getMetadata();
    const int np = int(md.params.size());
    if (skipOp >= 0 && int(opc) == skipOp) continue;
    switch (opc) {
      case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: render(); break;
      case 10: case 11: case 12: case 13: {
        const int id = int(rnd() % np); const float lo = float(md.params[id].minValue), hi = float(md.params[id].maxValue);
        float v = lo + (hi - lo) * rndf(); if (rnd() % 8 == 0) v = (rnd() & 1) ? lo : hi; if (rnd() % 16 == 0) v = lo - 1.0f;   // out of range: the setter clamps
        p.setParamValue(id, v, Patch::ParamChangeSource::kKnob); break; }
      case 14: p.setMacroValue(int(rnd() % 6), rndf() * 1.0f, {}); break;
      case 15: p.setMacroModifier(int(rnd() % 6), int(rnd() % 3), tiny_optional<int>(int(rnd() % np))); break;
      case 16: p.setMacroModifierAmount(int(rnd() % 6), int(rnd() % 3), rndf() * 2.0f - 1.0f); break;
      case 17: if (rnd() % 6 == 0) p.clearMacroModifiers(); break;
      case 18: p.setMacroBipolar(int(rnd() % 6), rnd() & 1); break;
      case 19: case 20: {
        open::ModSlot s; const uint32_t t = rnd() % 4; s.type = t == 0 ? open::ModType::kOff : t == 1 ? open::ModType::kEnv : open::ModType::kLfo;
        s.shape = uint8_t(rnd() % 5); s.speed = uint8_t(rnd() % 20); s.amount = int8_t(int(rnd() % 201) - 100);
        const auto& def = *op.getDef(); open::modSetDest(def, s, int(rnd() % np)); op.setModSlot(int(rnd() % open::kModSlots), s); break; }
      case 21: case 22: case 23: src[k].send(MidiEvent::Type::kNoteOn, 0, 40 + int(rnd() % 40), 20 + int(rnd() % 100)); break;
      case 24: case 25: src[k].send(MidiEvent::Type::kNoteOff, 0, 40 + int(rnd() % 40), 0); break;
      case 26: if (rnd() % 8 == 0) src[k].send(MidiEvent::Type::kStop, 0, 0, 0); break;
      case 27: src[k].send(MidiEvent::Type::kControlChange, 0, 100 + int(rnd() % 8), int(rnd() % 128)); break;
      case 28: if (rnd() % 10 == 0) { patch[k] = std::shared_ptr<Patch>(store->createPatch(ids[rnd() % nEng])); inst[k]->assignPatch(patch[k]); } break;
      case 29: inst[k]->setVoiceCount(int(rnd() % 4)); break;
      case 30: /* in range, as a bank row is (valuesFromData) */ if (rnd() % 12 == 0) { float v[open::kMaxParams]; op.getValuesRaw(v); for (int i = 0; i < op.paramCount(); i++) if (rnd() % 7 == 0) { const float lo = float(md.params[i].minValue), hi = float(md.params[i].maxValue); v[i] = (nanOk && rnd() % 5 == 0) ? NAN : lo + (hi - lo) * rndf(); } op.setValuesRaw(v); } break;
      case 31: op.useOwnCfg(); break;
      case 32: case 33: render(); render(); break;
    }
  }
  printf("refresh equivalence fuzz: %d steps, %ld part blocks, output hash %08x\n", steps, blocks, hash);
  return 0;
}
