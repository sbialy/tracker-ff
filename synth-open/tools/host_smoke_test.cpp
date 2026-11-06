// Host smoke test of the open shim (NOT firmware, not built by make). Build and run on the Mac (x86_64 via Rosetta):
//   clang++ -arch x86_64 -std=c++20 -O1 "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" -I synth/include -I util/include \
//     -I util/3rdparty -I synth-open -I synth-open/ot -DFEATURE_SYNTH_OPEN -DOPEN_HOST_TEST synth-open/tools/host_smoke_test.cpp \
//     synth-open/open_synth.cpp synth-open/open_cold.cpp synth-open/open_patchio.cpp synth-open/engine_{acd,fat,vap,wtfm}.cpp synth-open/ot/audio/engines/{acd,fat,vap,wtfm}.cpp \
//     synth-open/ot/audio/{governor,cpu_meter}.cpp \
//     -o /tmp/smoke && /tmp/smoke
// Optional OpenTracker oracle (ref/opentracker, not in the repository): add -DOT_ORACLE -I ref/opentracker/src/core
// ref/opentracker/src/core/formats/patch_pb.cpp to the line above.
// Checks: ACD metadata (29 params, 6 sections), note on -> pitch/amplitude, 2 voices, steal, release to silence, macro CC,
// then one note per engine of the picked set (engine_list.h, e.g. ACD FAT VAP WTFM PERC CHIP JU6 PHZ): metadata counts, output finite and non-silent, fails with exit 1 otherwise.
// PERC also gets one hit per drum (kick, tom, snare, hats, cymbal, perc) and the checks of its one voice kit.
// Then the voice pool and the governor: 6 notes over 3 slots sound at most 4 voices (the oldest are stolen), budget 8 and the
// per-instrument cap, a simulated overload (block cycles) makes the governor cut with hold-off, calm blocks restore one voice
// at a time, Stop and offline render reset it (they use the first engine of the set and 2 voices per instrument, so a set that starts with PERC, 1 voice, fails them: the canonical order puts PERC after WTFM, only PERC alone starts with it). Then .patch save/load on an in-memory file system (all picked engines, then references to an engine this build lacks fall back safely).
#include "open_load.h"
#include "open_bankcopy.h"
#include "open_patchdata.h"
#include "open_patch.h"
#include "PolySynth/FileSystem.h"
#include <map>
#include <set>
#ifdef OT_ORACLE
#include "formats/patch_pb.h"
#endif
#include "PolySynth/Instrument.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>
#ifdef SYNTH_ENGINE_PHZ
#include "phz/phz.h"
#endif
namespace polysynth {
std::unique_ptr<InstrumentType> createMidiOut();
// The run time engine list (the engines picked with -DSYNTH_ENGINE_<NAME>, no plugins here) and the missing placeholders,
// as core/synth/registry.cpp
void InstrumentTypeRegistry::registerInstrumentTypes(InstrumentTypeRegistry& r, const Synth::Allocator&) {
    r.addInstrumentType(int(InstrumentTypeId::kMidiOut), createMidiOut());
    open::registerEngines(r);
}
}
using namespace polysynth;
struct Src : MidiSource { Src():MidiSource(Type::kInternalKeyboard){} std::weak_ptr<MidiEventCallback> cb;
  void subscribeMidiEvents(int, std::weak_ptr<MidiEventCallback> c) override { cb = c; }
  void send(MidiEvent::Type t, int ch, int a, int b) { MidiEvent e{}; e.type=t; e.channel=ch; e.data.rawData.value1=a; e.data.rawData.value2=b; (*cb.lock())(e,0);} };
static float peakOf(std::array<float,128>& l){ float p=0; for(float v:l){ if(!std::isfinite(v)) return INFINITY; p=std::max(p,std::fabs(v)); } return p; }
// In-memory FileSystem for the patch store tests: "dir/name" keys, one level of folders.
struct MemFS : FileSystem {
  struct H { std::string path; size_t pos; bool w; };
  std::map<std::string, std::vector<uint8_t>> files; std::vector<std::string> listed; std::vector<H*> hs;
  std::vector<std::string_view> listFiles(const std::string& dir, std::function<bool(const std::string&)> m, tiny_optional<int>) override {
    listed.clear(); const std::string pre = dir.empty() ? "" : dir + "/";
    for (auto& [k, v] : files) if (k.compare(0, pre.size(), pre) == 0 && k.find('/', pre.size()) == std::string::npos && m(k.substr(pre.size()))) listed.push_back(k.substr(pre.size()));
    return std::vector<std::string_view>(listed.begin(), listed.end()); }
  std::optional<FileHandle> open(const std::string& path, OpenOptions o) override {
    const bool w = o == OpenOptions::kWrite; if (!w && onRead) onRead(path);   // the audio callback runs while the card is read
    if (!w && (!files.count(path) || failOpenRead.count(path))) return std::nullopt; if (w && failOpenWrite) return std::nullopt;
    if (w) files[path].clear();
    hs.push_back(new H{path, 0, w}); openNow++; return hs.back(); }
  int openNow = 0; // open handles (FatFs on the device has a fixed table, a leak ends in failed opens)
  void close(FileHandle) override { openNow--; }
  bool write(FileHandle h, std::span<const uint8_t> d) override {
    auto* x = (H*)h; if (onWrite) onWrite(x->path);                  // the audio callback runs while the card is written
    if (failWrite) { files[x->path].insert(files[x->path].end(), d.begin(), d.begin() + d.size() / 2); return false; }   // a full card: half of it
    files[x->path].insert(files[x->path].end(), d.begin(), d.end()); return true; }
  int32_t read(FileHandle h, std::span<uint8_t> b) override { auto* x = (H*)h; auto& f = files[x->path];
    if (readFailAt >= 0 && x->pos >= size_t(readFailAt)) return -1;   // the card fails after readFailAt bytes
    size_t n = std::min(b.size(), f.size() - x->pos); if (readFailAt >= 0) n = std::min(n, size_t(readFailAt) - x->pos);
    std::copy_n(f.begin() + x->pos, n, b.begin()); x->pos += n; return (int32_t)n; }
  bool seek(FileHandle, int) override { return true; }
  void createFolder(const std::string&) override {}
  bool replace(const std::string& from, const std::string& to) override {
    if (failReplace || !files.count(from)) return false; files[to] = std::move(files[from]); files.erase(from); return true; }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  Presence presence(const std::string& path) override { return statError.count(path) ? Presence::kUnknown : files.count(path) ? Presence::kPresent : Presence::kAbsent; }
  // failure injection and hooks of the save, load and copy tests
  bool failOpenWrite = false, failWrite = false, failReplace = false; int readFailAt = -1;
  std::set<std::string> statError, failOpenRead;   // paths the card cannot stat (kUnknown), existing files that do not open
  std::function<void(const std::string&)> onRead, onWrite;
  void healthy() { failOpenWrite = failWrite = failReplace = false; readFailAt = -1; onRead = nullptr; onWrite = nullptr; statError.clear(); failOpenRead.clear(); }
};
// The card operations of the bank copy (Synth::copyWorkspacePatches) over two in-memory folders: paths "S\\name" are the source folder, "D\\name" the destination.
struct MemBankOps {
  using Files = std::map<std::string, std::vector<uint8_t>>;
  Files* src; Files* dst; std::string failCopy;       // failCopy: the copy to this file fails (the destination keeps what it had)
  std::set<std::string> statErr;                      // paths the card cannot stat ("S\\0-2.patch"): kUnknown
  bool failRemove = false; int removed = 0;
  Files& mapOf(const std::string& p) { return p[0] == 'S' ? *src : *dst; }
  static std::string base(const std::string& p) { return p.substr(2); }
  open::Presence stat(const std::string& p) { return statErr.count(p) ? open::Presence::kUnknown : mapOf(p).count(base(p)) ? open::Presence::kPresent : open::Presence::kAbsent; }
  bool copyFile(const std::string& a, const std::string& b) {
    auto it = mapOf(a).find(base(a)); if (it == mapOf(a).end() || base(b) == failCopy) return false;
    mapOf(b)[base(b)] = it->second; return true; }
  bool remove(const std::string& p) { if (failRemove) return false; removed++; return mapOf(p).erase(base(p)) > 0; }
};
// A FatFs-like card for open_fileops.h: rename fails when the target exists, a rename can be made to fail (bit k of failMask: the k-th rename),
// and the power can go after the n-th change (every later call does nothing and says unknown / false).
struct RenameOps {
  std::map<std::string, std::string> f; int renames = 0, changes = 0, failMask = 0, powerAfter = -1; bool dead = false;
  void step() { if (powerAfter >= 0 && changes >= powerAfter) dead = true; }
  open::Presence stat(const char* p) { step(); if (dead) return open::Presence::kUnknown; return f.count(p) ? open::Presence::kPresent : open::Presence::kAbsent; }
  bool remove(const char* p) { step(); if (dead) return false; changes++; return f.erase(p) > 0; }
  bool rename(const char* a, const char* b) { step(); if (dead) return false; const int k = renames++; if (failMask >> k & 1) return false;
    if (!f.count(a) || f.count(b)) return false; changes++; f[b] = f[a]; f.erase(a); return true; }
};
// A short heap, as on the device after a while: nothrow new (what the patch store and bank use) fails above gShortHeap bytes (0: off)
static size_t gShortHeap = 0;
void* operator new(size_t n, const std::nothrow_t&) noexcept { return gShortHeap && n > gShortHeap ? nullptr : std::malloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return gShortHeap && n > gShortHeap ? nullptr : std::malloc(n); }
int main() {
  auto synth = Synth::createSynth([](size_t){return nullptr;});
  auto store = synth->createPatchStore(Synth::FileSystemId::kPatches, true);
  const int kLead = open::firstEngineTypeId();   // the first engine of this build (ACD unless left out)
  auto patch = std::shared_ptr<Patch>(store->createPatch(kLead));
  const auto& md = patch->getMetadata();
  printf("params=%zu sections=%zu macros=%zu maxVoices=%d\n", md.params.size(), md.layoutFlat.sections.size(), md.macros.size(), md.maxVoices);
#ifdef SYNTH_ENGINE_ACD
  for (int i : {5, 9, 15, 23, 28}) printf("  [%d] %s = %s (editmapped %.3f)\n", i, md.params[i].displayName.c_str(), md.params[i].valueFormatter(*patch, *patch->getParamValue(i)).c_str(), *patch->getParamValueEditMapped(i));
  patch->setParamValueEditMapped(9, 0.5f, Patch::ParamChangeSource::kKnob);
  printf("  cutoff at knob 0.5 = %s\n", md.params[9].valueFormatter(*patch,*patch->getParamValue(9)).c_str());
  patch->setParamValueEditMapped(9, 0.7f, Patch::ParamChangeSource::kKnob);
#endif
  Src src;
  auto inst = synth->createInstrument(0);
  inst->assignPatch(patch); inst->connect(src); inst->setVoiceCount(2); inst->setMidiChannel(0);
  std::shared_ptr<Instrument> arr[1] = { inst };
  synth->setup(std::span<std::shared_ptr<Instrument>>(arr,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
  std::array<float,128> L, R, A1, A2; std::array<AudioBufferRef,2> aux{AudioBufferRef(A1),AudioBufferRef(A2)};
  auto blk=[&](){ synth->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); return peakOf(L); };
  printf("silent peak=%.4f\n", blk());
  src.send(MidiEvent::Type::kNoteOn, 0, 57, 100);   // A3
  float pk=0; int zc=0; float prev=0; double e=0;
  for (int b=0;b<40;b++){ float p=blk(); pk=std::max(pk,p); for(float v:L){ if((v>0)!=(prev>0)) zc++; prev=v; e+=v*v; } }
  printf("noteOn A3: peak=%.4f rms=%.4f zero-cross/5120 smp=%d (A3=220Hz -> ~%d)\n", pk, std::sqrt(e/5120), zc, int(2*220*5120/44117.6));
  src.send(MidiEvent::Type::kNoteOn, 0, 64, 127);   // second voice
  pk=0; for (int b=0;b<20;b++) pk=std::max(pk,blk()); printf("2 voices peak=%.4f\n", pk);
  src.send(MidiEvent::Type::kNoteOn, 0, 72, 127);   // third: steal
  pk=0; for (int b=0;b<20;b++) pk=std::max(pk,blk()); printf("3rd note (steal) peak=%.4f finite=%d\n", pk, std::isfinite(pk));
  src.send(MidiEvent::Type::kNoteOff, 0, 64, 0); src.send(MidiEvent::Type::kNoteOff, 0, 72, 0);
  float last=0; for (int b=0;b<600;b++) last=blk(); printf("after release+1.7s peak=%.6f (expect ~0)\n", last);
  // macro CC 102 (cutoff)
  src.send(MidiEvent::Type::kNoteOn, 0, 57, 100); src.send(MidiEvent::Type::kControlChange, 0, 102, 127);
#ifdef SYNTH_ENGINE_ACD
  printf("final cutoff incl macro=%s\n", md.params[9].valueFormatter(*patch,*patch->getParamFinalValue(9)).c_str());
#endif
  pk=0; for (int b=0;b<20;b++) pk=std::max(pk,blk()); printf("with macro peak=%.4f\n", pk);
  // One note per engine: default patch, 40 blocks, output must be finite and non-silent
  struct E { const char* name; InstrumentTypeId id; size_t params; int note; } engines[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, ...) {#NAME, TYPE_ID, size_t(open::DEF.paramCount), TYPE_ID == InstrumentTypeId::kDrumSynth || int(TYPE_ID) == 0x85 ? 60 : 57},
#include "engine_list.h"
#undef SYNTH_ENGINE
  };
  int bad = 0;
  for (const auto& e : engines) {
    auto ep = std::shared_ptr<Patch>(store->createPatch(int(e.id)));
    if (ep->getMetadata().instrumentTypeId != int(e.id)) { printf("%s: patch has the wrong type id\n", e.name); bad++; }
    const auto& emd = ep->getMetadata();
    auto ei = synth->createInstrument(0);
    Src es; ei->assignPatch(ep); ei->connect(es); ei->setVoiceCount(2); ei->setMidiChannel(0);
    std::shared_ptr<Instrument> earr[1] = { ei };
    synth->setup(std::span<std::shared_ptr<Instrument>>(earr,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    es.send(MidiEvent::Type::kNoteOn, 0, e.note, 100);
    float epk = 0; double en = 0;
    for (int b = 0; b < 40; b++) { float p = blk(); epk = std::max(epk, p); for (float v : L) en += v * v; }
    bool ok = std::isfinite(epk) && epk > 1e-4f && emd.params.size() == e.params && !emd.layoutFlat.sections.empty();
    printf("%-5s params=%zu sections=%zu peak=%.4f rms=%.4f %s\n", e.name, emd.params.size(), emd.layoutFlat.sections.size(), epk, std::sqrt(en / 5120), ok ? "OK" : "FAIL");
    bad += !ok;
  }
  printf(bad ? "ENGINE CHECK FAILED\n" : "all engines OK\n");

  // ---- global voice pool and governor: 3 ACD slots, 2 voices each per instrument, pool default 4
  auto check = [&](bool ok, const char* what) { printf("  %-62s %s\n", what, ok ? "OK" : "FAIL"); bad += !ok; };
  auto s3 = Synth::createSynth([](size_t){return nullptr;});
  auto st3 = s3->createPatchStore(Synth::FileSystemId::kPatches, true);
  std::shared_ptr<Patch> pat3[3]; std::shared_ptr<Instrument> ins3[3]; Src src3[3];
  for (int k = 0; k < 3; k++) {
    pat3[k] = std::shared_ptr<Patch>(st3->createPatch(kLead));
    ins3[k] = s3->createInstrument(k); ins3[k]->assignPatch(pat3[k]); ins3[k]->connect(src3[k]);
    ins3[k]->setVoiceCount(2); ins3[k]->setMidiChannel(0);
  }
  s3->setup(std::span<std::shared_ptr<Instrument>>(ins3,3), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
  float slotPk[3];
  auto renderAll = [&]() { for (int k = 0; k < 3; k++) { s3->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), k); slotPk[k] = peakOf(L); } };
  printf("voice pool: budget=%d cap=%d (default 4, max 8)\n", open::voiceBudget(), open::voiceCap());
  check(open::voiceBudget() == 4 && open::voiceCap() == 4, "default budget 4");
  int maxSounding = 0;
  const int notes[6][2] = {{0,60},{0,64},{1,67},{1,71},{2,72},{2,76}};   // {slot, note}: 6 notes over 3 slots
  for (const auto& n : notes) {
    src3[n[0]].send(MidiEvent::Type::kNoteOn, 0, n[1], 100);
    maxSounding = std::max(maxSounding, open::voicesSounding());
    renderAll(); renderAll();   // age the voices so "oldest" is defined
    maxSounding = std::max(maxSounding, open::voicesSounding());
  }
  for (int b = 0; b < 4; b++) renderAll();
  printf("6 notes over 3 slots: max voices sounding=%d final=%d slot peaks %.3f %.3f %.3f\n", maxSounding, open::voicesSounding(), slotPk[0], slotPk[1], slotPk[2]);
  check(maxSounding <= 4 && open::voicesSounding() == 4, "6 notes sound at most 4 voices (pool full)");
  check(slotPk[0] < 1e-4f && slotPk[1] > 1e-3f && slotPk[2] > 1e-3f, "the two oldest notes (slot 0) were stolen and went silent");
  open::setVoiceBudget(99);
  check(open::voiceBudget() == 8, "budget clamps to 8");
  src3[0].send(MidiEvent::Type::kNoteOn, 0, 60, 100); src3[0].send(MidiEvent::Type::kNoteOn, 0, 64, 100); renderAll();
  check(open::voicesSounding() == 6, "budget 8: 3 slots x 2 voices = 6 (per-instrument cap 2 kept)");
  src3[0].send(MidiEvent::Type::kNoteOn, 0, 62, 100);
  check(open::voicesSounding() == 6, "third note on one slot steals inside the slot (cap 2)");
  open::setVoiceBudget(4);
  check(open::voicesSounding() == 4 && open::voiceCap() == 4, "budget back to 4 steals down to 4");
  for (int b = 0; b < 4; b++) renderAll();

  // simulated overload: 1015 permille blocks (the block budget is 522,240 cycles)
  const uint32_t kOver = 530000, kCalm = 156672;   // 1015 and 300 permille
  open::reportBlockCycles(kOver);
  printf("overload block: cap=%d sounding=%d cuts=%u\n", open::voiceCap(), open::voicesSounding(), open::governorCuts());
  check(open::voiceCap() == 3 && open::voicesSounding() == 3 && open::governorCuts() == 1, "overrun block cuts one voice (4 -> 3)");
  for (int b = 0; b < 80; b++) { open::reportBlockCycles(kOver); renderAll(); }
  check(open::governorCuts() == 1 && open::voiceCap() == 3, "no second cut within the 250 ms hold-off");
  src3[0].send(MidiEvent::Type::kNoteOn, 0, 50, 100); renderAll();
  check(open::voicesSounding() <= 3, "new notes respect the lowered cap");
  for (int b = 0; b < 40; b++) { open::reportBlockCycles(kOver); renderAll(); }
  printf("after 120 overload blocks: cap=%d sounding=%d cuts=%u\n", open::voiceCap(), open::voicesSounding(), open::governorCuts());
  check(open::governorCuts() >= 2 && open::voiceCap() <= 2, "sustained overload cuts again after the hold-off");
  int cap = open::voiceCap(), steps = 0, blocks = 0; bool stepOk = true;
  for (; blocks < 6000 && open::voiceCap() < 4; blocks++) {
    open::reportBlockCycles(kCalm); renderAll();
    if (open::voiceCap() != cap) { stepOk = stepOk && open::voiceCap() == cap + 1; cap = open::voiceCap(); steps++; printf("  restore at block %d: cap=%d\n", blocks, cap); }
  }
  check(open::voiceCap() == 4 && stepOk && steps >= 2, "calm blocks restore one voice at a time up to the budget (>= 2 s each)");
  check(blocks >= 690 * steps, "each restore needs 690 calm blocks (2 s)");
  for (const auto& n : notes) src3[n[0]].send(MidiEvent::Type::kNoteOn, 0, n[1] + 1, 100);
  check(open::voicesSounding() == 4, "after the restore the pool sounds 4 voices again");
  // Stop resets, offline render switches the governor off
  open::reportBlockCycles(kOver);
  check(open::voiceCap() == 3, "cut again");
  src3[0].send(MidiEvent::Type::kStop, 0, 0, 0);
  check(open::voiceCap() == 4, "transport Stop resets the cap to the budget");
  open::reportBlockCycles(kOver);
  open::setOffline(true);
  const uint32_t cutsOffline = open::governorCuts();
  for (int b = 0; b < 200; b++) open::reportBlockCycles(kOver);
  check(open::voiceCap() == 4 && open::governorCuts() == cutsOffline, "offline render: governor off, cap at the budget");
  open::setOffline(false);
  // project load: cap back at the budget, hold-off cleared (a cut right after), no voice left
  src3[0].send(MidiEvent::Type::kNoteOn, 0, 60, 100); renderAll();
  open::reportBlockCycles(kOver);
  check(open::voiceCap() < 4 && open::voicesSounding() > 0, "overload again: cap cut below the budget, voices sounding");
  open::resetOnProjectLoad();
  check(open::voiceCap() == 4 && open::voicesSounding() == 0, "project load: cap back at the budget, voices cut");
  src3[0].send(MidiEvent::Type::kNoteOn, 0, 60, 100); renderAll();   // inside the old 250 ms hold-off
  open::reportBlockCycles(kOver);
  check(open::voiceCap() < 4, "project load clears the hold-off: next overrun cuts at once");
  open::resetOnProjectLoad();
  // ---- 4 voices per instrument: the cap clamps at 4, one slot can fill the default pool, a fifth note steals inside the slot
  {
    ins3[0]->setVoiceCount(9);
    check(ins3[0]->getVoiceCount() == 4, "Voices above 4 clamp to 4 (was 2)");
    ins3[0]->setVoiceCount(4); ins3[1]->setVoiceCount(4); ins3[2]->setVoiceCount(4);
    const int chord[5] = {48, 55, 60, 64, 67};
    for (int k = 0; k < 4; k++) { src3[0].send(MidiEvent::Type::kNoteOn, 0, chord[k], 100); renderAll(); renderAll(); }
    check(open::voicesSounding() == 4, "4 notes on one slot: 4 voices sound (pool default 4)");
    src3[0].send(MidiEvent::Type::kNoteOn, 0, chord[4], 100); renderAll(); renderAll();
    check(open::voicesSounding() == 4, "a fifth note steals inside the slot, still 4");
    src3[1].send(MidiEvent::Type::kNoteOn, 0, 72, 100); renderAll(); renderAll(); renderAll();
    check(open::voicesSounding() == 4 && slotPk[1] > 1e-3f, "a note on slot 2 takes a voice from the full pool (global victim), still 4");
    open::setVoiceBudget(8);
    for (int k = 0; k < 4; k++) { src3[2].send(MidiEvent::Type::kNoteOn, 0, 50 + 4 * k, 100); renderAll(); }
    src3[1].send(MidiEvent::Type::kNoteOn, 0, 76, 100); src3[1].send(MidiEvent::Type::kNoteOn, 0, 79, 100); src3[1].send(MidiEvent::Type::kNoteOn, 0, 83, 100); renderAll();
    src3[0].send(MidiEvent::Type::kNoteOn, 0, 40, 100); src3[0].send(MidiEvent::Type::kNoteOn, 0, 43, 100); renderAll(); renderAll();
    check(open::voicesSounding() == 8, "budget 8: pool full at 8 with 3 slots asking for 12");
    open::setVoiceBudget(4);
    for (int b = 0; b < 4; b++) renderAll();
    check(open::voicesSounding() == 4, "budget 4 again: down to 4");
    for (int k = 0; k < 3; k++) ins3[k]->setVoiceCount(2);
    open::resetOnProjectLoad();
  }
  // ---- .patch save / load (open_patchio.cpp + OpenPatchStore) on an in-memory file system
  {
    auto s4 = Synth::createSynth([](size_t){return nullptr;});
    auto fsP = std::make_unique<MemFS>(); MemFS* mp = fsP.get();
    auto fsW = std::make_unique<MemFS>(); MemFS* mw = fsW.get();
    s4->addFileSystem(Synth::FileSystemId::kPatches, std::move(fsP)); s4->addFileSystem(Synth::FileSystemId::kWorkspace, std::move(fsW));
    auto ps = s4->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto ws = s4->createPatchStore(Synth::FileSystemId::kWorkspace, false);
    const struct { const char* name; InstrumentTypeId id; } kinds[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, ...) {#NAME, TYPE_ID},
#include "engine_list.h"
#undef SYNTH_ENGINE
    };
    const int nKinds = int(sizeof(kinds) / sizeof(kinds[0]));
    bool allSame = true, allIdem = true, allList = true;
    for (const auto& k : kinds) {
      auto a = ps->createPatch(int(k.id)); const auto& am = a->getMetadata();
      for (size_t i = 0; i < am.params.size(); i++) { const float lo = float(am.params[i].minValue), hi = float(am.params[i].maxValue); a->setParamValue(int(i), lo + (hi - lo) * (0.1f + 0.8f * float((i * 37) % 11) / 10.0f), Patch::ParamChangeSource::kKnob); }
      a->setMacroDisplayName(0, "My Macro"); a->setMacroBipolar(1, true); a->setMacroModifier(2, 0, 3); a->setMacroModifierAmount(2, 0, -0.75f);
      a->setMacroModifier(2, 1, 1); a->setMacroModifierAmount(2, 1, 0.5f);
      if (!ps->savePatch("t", *a)) { allSame = false; continue; }
      const auto& bytes = mp->files[std::string(k.name) + "/t.patch"];
      auto b = ps->createPatch(int(k.id)); const bool loaded = ps->loadPatch("t", *b, true);
      bool same = loaded;
      for (size_t i = 0; i < am.params.size() && same; i++) { same = *a->getParamValue(int(i)) == *b->getParamValue(int(i)); if (!same) printf("  %s param %zu (%s) saved %.9g loaded %.9g range %g..%g\n", k.name, i, am.params[i].displayName.c_str(), *a->getParamValue(int(i)), *b->getParamValue(int(i)), double(am.params[i].minValue), double(am.params[i].maxValue)); }
      const auto& bm = b->getMetadata();
      for (int m = 0; m < int(kMacroCount) && same; m++) {
        same = am.macros[m].displayName == bm.macros[m].displayName && am.macros[m].bipolar == bm.macros[m].bipolar && am.macros[m].modifiers.size() == bm.macros[m].modifiers.size();
        for (size_t t = 0; same && t < am.macros[m].modifiers.size(); t++) same = am.macros[m].modifiers[t].paramId == bm.macros[m].modifiers[t].paramId && am.macros[m].modifiers[t].amount == bm.macros[m].modifiers[t].amount;
      }
      printf("  %-5s .patch %zu bytes, round trip %s\n", k.name, bytes.size(), same ? "equal" : "DIFFERENT");
      allSame = allSame && same && bytes.size() > 1300 && bytes.size() < open::kPatchFileMax;
      ps->savePatch("t2", *b); allIdem = allIdem && mp->files[std::string(k.name) + "/t2.patch"] == bytes;
      auto names = ps->listPatchesByType(int(k.id)); allList = allList && names.size() == 2 && names[0] == "t" && names[1] == "t2";
    }
    check(allSame, "save -> load gives equal params and macros (all engines of this build)");
    check(allIdem, "load -> save writes the identical bytes (all engines of this build)");
    check(allList, "patch browser lists the saved patches, sorted, without the extension");
    // wrong engine, damaged file, missing file: load fails and leaves the patch alone
    const auto& kLast = kinds[nKinds - 1]; const std::string lastName = kLast.name, firstName = kinds[0].name;
    auto fat = ps->createPatch(int(kLast.id)); const float before = *fat->getParamValue(0);
    mp->files[lastName + "/short.patch"] = mp->files[lastName + "/t.patch"]; { // cut inside a field: the first length at which the file no longer decodes
      auto& f = mp->files[lastName + "/short.patch"]; const auto whole = f;
      for (size_t n = 60; n < whole.size(); n++) { open::PatchData tmp; if (!open::decodePatch(whole.data(), n, tmp)) { f.resize(n); break; } } }
    auto& trunc = mp->files[lastName + "/short.patch"];
    if (nKinds > 1) { mp->files[lastName + "/wrong.patch"] = mp->files[firstName + "/t.patch"];
      check(!ps->loadPatch("wrong", *fat, true), "a file of the first engine does not load into a patch of the last engine"); }
    check(!ps->loadPatch("short", *fat, true), "a file cut in the middle of a field is refused");
    check(!ps->loadPatch("missing", *fat, true) && *fat->getParamValue(0) == before, "a missing file fails and the patch is untouched");
    // An engine this build lacks: a .patch of it, its type id from a stock project and its folder must all be harmless
    // (no crash, no load, nothing listed, an Init patch of the first engine instead). PERC (.patch engine 6, Polyend
    // kDrumSynth) is that engine unless it is picked, else it is Shaping (kShapingSynth, never built, .patch engine 0x7E)
    {
#ifdef SYNTH_ENGINE_PERC
      const InstrumentTypeId kAbsent = InstrumentTypeId::kShapingSynth; const int kAbsentEngine = 0x7E; const char* const kAbsentName = "absent";
#else
      const InstrumentTypeId kAbsent = InstrumentTypeId::kDrumSynth; const int kAbsentEngine = 6 /* ot::kSynthEnginePerc */; const char* const kAbsentName = "PERC";
#endif
      auto src = ps->createPatch(kLead); open::PatchData pd; static_cast<open::OpenPatch&>(*src).exportData(pd);
      pd.engine = kAbsentEngine; uint8_t pbuf[4096]; const size_t pn = open::encodePatch(pd, pbuf, sizeof(pbuf));
      bool refused = pn > 0, untouched = true, decoded = pn > 0;
      for (const auto& kd : kinds) {
        const InstrumentTypeId k = kd.id;
        auto t = ps->createPatch(int(k)); const float b0 = *t->getParamValue(0);
        const char* nm = open::engineDefForType(int(k))->name;
        mp->files[std::string(nm) + "/absent.patch"].assign(pbuf, pbuf + pn);
        refused = refused && !ps->loadPatch("absent", *t, true); untouched = untouched && *t->getParamValue(0) == b0;
      }
      open::PatchData back; decoded = decoded && open::decodePatch(pbuf, pn, back) && open::typeForEngineId(back.engine) == -1;
      printf("  absent engine: %s (.patch engine %d)\n", kAbsentName, kAbsentEngine);
      check(decoded && refused && untouched, "a .patch of an engine this build lacks is refused by every engine and leaves the patch untouched");
      auto pp = ps->createPatch(int(kAbsent));   // stale type id from a stock project
      check(pp && pp->getMetadata().instrumentTypeId == kLead && int(pp->getMetadata().params.size()) == open::engineDefForType(kLead)->paramCount,
            "createPatch(absent type) falls back to an Init patch of the first engine");
      check(ps->listPatchesByType(int(kAbsent)).empty() && s4->getInstrumentName(int(kAbsent)).empty(),
            "an absent type lists no patches and has no instrument name");
      check(!ps->loadPatch("absent", *pp, true) && !ws->loadPatch("0", *pp, true), "loading into the fallback patch from an absent engine or missing files fails cleanly");
    }
#ifndef SYNTH_ENGINE_HANDTYPE
    // Hand-Type (stored 15, type id and .patch engine 0x87) is in the source tree but not in the release set: a project or a
    // patch from a build that has it is in the "missing" state (phase 3, open_engine.h missingDef): a placeholder without
    // parameters or engine, silent, whose patch files are never read into it or written over
    {
      const InstrumentTypeId kHand = static_cast<InstrumentTypeId>(0x87);
      auto src = ps->createPatch(kLead); open::PatchData pd; static_cast<open::OpenPatch&>(*src).exportData(pd);
      pd.engine = 0x87; uint8_t pbuf[4096]; const size_t pn = open::encodePatch(pd, pbuf, sizeof(pbuf));
      bool refused = pn > 0, untouched = true;
      for (const auto& kd : kinds) {
        auto t = ps->createPatch(int(kd.id)); const float b0 = *t->getParamValue(0);
        mp->files[std::string(open::engineDefForType(int(kd.id))->name) + "/hand.patch"].assign(pbuf, pbuf + pn);
        refused = refused && !ps->loadPatch("hand", *t, true); untouched = untouched && *t->getParamValue(0) == b0;
      }
      auto hp = ps->createPatch(int(kHand));
      const open::EngineDef* hd = open::engineDefForType(int(kHand));
      check(refused && untouched && open::typeForEngineId(0x87) == 0x87 && hd && !open::engineOf(hd) && hd->paramCount == 0 &&
              std::string(hd->name) == "HANDTYPE" && open::missingDef(15) == hd && open::typeForStored(15) == 0x87 &&
              open::engineUiForStored(15) == -1,
            "Hand-Type is not in this build: its .patch is refused by every engine, its numbers name the missing placeholder");
      mw->files["0.patch"].assign(pbuf, pbuf + pn);
      const auto before = mw->files["0.patch"];
      check(hp && hp->getMetadata().instrumentTypeId == int(kHand) && hp->getMetadata().params.empty() && ps->listPatchesByType(int(kHand)).empty() &&
              ws->loadPatch("0", *hp, true) && ws->savePatch("0", *hp) && mw->files["0.patch"] == before && !mw->files.count("0.patch.tmp"),
            "a Hand-Type part is the placeholder: no parameters, its Workspace file loads as done and is never written over");
      check(open::engineCount() == nKinds && open::engineUiForStored(open::engineAt(nKinds - 1).stored) == nKinds - 1 &&
              open::engineAt(0).builtin == 0, "the engine list is the built-in set, in engine_list.h order");
    }
#endif
    // workspace store: flat N.patch, as Synth::savePatchToWorkspace / loadPatchFromWorkspace use it
    auto w0 = ws->createPatch(kLead); const float w0v = float(w0->getMetadata().params[0].maxValue); w0->setParamValue(0, w0v, Patch::ParamChangeSource::kKnob);
    check(ws->savePatch("0", *w0) && mw->files.count("0.patch") == 1, "workspace store writes Workspace/0.patch (no type folder)");
    auto w1 = ws->createPatch(kLead);
    check(ws->loadPatch("0", *w1, true) && *w1->getParamValue(0) == w0v, "project reload: the edited value comes back from Workspace/0.patch");
    // ---- patch slots and the Patch step FX: <P>.patch is slot 0 (stock opens it), <P>-n.patch is slot n, all in the project
    {
      mw->files.clear();   // a clean Workspace
      // the raw values of the patch: getParamValue gives the edited ones while the FX has replaced them, these tests look at what plays
      auto sameValues = [](Patch& a, Patch& b) { const size_t n = a.getMetadata().params.size(); bool same = n == b.getMetadata().params.size();
        float va[open::kMaxParams], vb[open::kMaxParams];
        if (same) { static_cast<open::OpenPatch&>(a).getValuesRaw(va); static_cast<open::OpenPatch&>(b).getValuesRaw(vb); }
        for (size_t i = 0; i < n && same; i++) same = va[i] == vb[i]; return same; };
      auto fill = [](Patch& p, float t) { const auto& m = p.getMetadata();
        for (size_t i = 0; i < m.params.size(); i++) { const float a = float(m.params[i].minValue), b = float(m.params[i].maxValue); p.setParamValue(int(i), a + (b - a) * t, Patch::ParamChangeSource::kKnob); } };
      auto nameOf = [](int part, int slot) { char n[8]; open::patchSlotName(part, slot, n); return std::string(n); };
      bool names = true, fx = true, back = true, keepsMacros = true, bad = true, missing = true, sel = true, pend = true, edits = true,
           revert = true, engineKeeps = true, engineBack = true, changed = true;
      names = nameOf(0, 0) == "0" && nameOf(1, 0) == "1" && nameOf(0, 1) == "0-1" && nameOf(2, 15) == "2-15";
      int part = 0;
      for (const auto& k : kinds) {
        const int myPart = part++ % open::kPatchBankParts;
        auto base = ws->createPatch(int(k.id)), lo = ws->createPatch(int(k.id)), hi = ws->createPatch(int(k.id));
        fill(*base, 0.5f); fill(*lo, 0.1f); fill(*hi, 0.9f);
        // slot 0 = base, slot 1 = lo, slot 3 = hi, slot 2 has no file
        ws->savePatch(nameOf(myPart, 0), *base); ws->savePatch(nameOf(myPart, 1), *lo); ws->savePatch(nameOf(myPart, 3), *hi);
        const bool files = mw->files.count(nameOf(myPart, 0) + ".patch") && mw->files.count(nameOf(myPart, 1) + ".patch") &&
                           mw->files.count(nameOf(myPart, 3) + ".patch") && !mw->files.count(nameOf(myPart, 2) + ".patch");
        names = names && files;
        auto live = ws->createPatch(int(k.id));
        ws->loadPatch(nameOf(myPart, 0), *live, true); live->setMacroDisplayName(0, "Keep");   // not in any slot file
        changed = changed && !sameValues(*base, *lo) && !sameValues(*lo, *hi) && !sameValues(*base, *hi);
        open::patchBankSetLive(myPart, live.get());
        open::patchBankPreload(myPart, *live, *ws);
        // the FX: slot n copies the values of <P>-n.patch over the live patch, slot 0 (the open one) brings the edited values back
        fx = fx && open::patchBankApply(myPart, 1) && sameValues(*live, *lo);
        fx = fx && open::patchBankApply(myPart, 3) && sameValues(*live, *hi);
        fx = fx && open::patchBankApply(myPart, 0) && sameValues(*live, *base);
        keepsMacros = keepsMacros && live->getMetadata().macros[0].displayName == "Keep";
        // a slot with no file and slots out of range are ignored, the values stay
        open::patchBankApply(myPart, 1);
        for (int ign : {2, 4, 15, 16, 99, -1}) missing = missing && !open::patchBankApply(myPart, ign) && sameValues(*live, *lo);
        // edited, unsaved values of the open slot survive an FX round trip and a restore
        open::patchBankRestore(myPart);
        live->setParamValue(0, float(live->getMetadata().params[0].maxValue), Patch::ParamChangeSource::kKnob);
        auto edited = ws->createPatch(int(k.id)); ws->savePatch("edited", *live); ws->loadPatch("edited", *edited, true);
        open::patchBankApply(myPart, 3);
        back = back && sameValues(*live, *hi);
        open::patchBankRestore(myPart);
        back = back && sameValues(*live, *edited);
        open::patchBankApply(myPart, 3); open::patchBankApply(myPart, 0);
        back = back && sameValues(*live, *edited);
        mw->files.erase("edited.patch");
        ws->loadPatch(nameOf(myPart, 0), *live, true);   // back to the saved slot 0
        // the editor: open slot 1 (file), slot 2 (no file: a copy of the open one, nothing is written), slot 3 again
        sel = sel && open::patchBankCurrent(myPart) == 0 && open::patchBankSelect(myPart, 0, *live, *ws) == open::SlotSelect::kSame;
        sel = sel && open::patchBankSelect(myPart, 1, *live, *ws) == open::SlotSelect::kLoaded && sameValues(*live, *lo) && open::patchBankCurrent(myPart) == 1;
        sel = sel && open::patchBankSelect(myPart, 2, *live, *ws) == open::SlotSelect::kCopied && sameValues(*live, *lo) && open::patchBankCurrent(myPart) == 2;
        pend = pend && !mw->files.count(nameOf(myPart, 2) + ".patch");   // opening an empty slot writes no file
        pend = pend && !open::patchBankApply(myPart, 4) ;               // and the bank does not have it either
        // library preview over the pending copy, then Cancel: back to the slot it began from (slot 1)
        fill(*live, 0.3f);
        revert = revert && open::patchBankRevert(myPart, *live, *ws) && sameValues(*live, *lo);
        // a pending copy passed on: slot 4 is still a copy of slot 1
        revert = revert && open::patchBankSelect(myPart, 4, *live, *ws) == open::SlotSelect::kCopied;
        fill(*live, 0.3f);
        revert = revert && open::patchBankRevert(myPart, *live, *ws) && sameValues(*live, *lo);
        // the user edits slot 4 and the project saves it: the file <P>-4.patch appears and the FX can use it
        fill(*live, 0.7f);
        auto seven = ws->createPatch(int(k.id)); fill(*seven, 0.7f);
        open::patchSaveOpenSlot(myPart, *live, *ws);   // Synth::savePatchToWorkspace
        edits = edits && mw->files.count(nameOf(myPart, 4) + ".patch") == 1;
        edits = edits && open::patchBankSelect(myPart, 3, *live, *ws) == open::SlotSelect::kLoaded && sameValues(*live, *hi);
        edits = edits && open::patchBankApply(myPart, 4) && sameValues(*live, *seven) && open::patchBankApply(myPart, 3) && sameValues(*live, *hi);
        edits = edits && open::patchBankSelect(myPart, 0, *live, *ws) == open::SlotSelect::kLoaded && sameValues(*live, *base);
        // project reload: a fresh bank from the files gives the same slots and the editor is on slot 0 again
        open::patchBankPreload(myPart, *live, *ws);
        edits = edits && open::patchBankCurrent(myPart) == 0 && open::patchBankApply(myPart, 4) && sameValues(*live, *seven) && open::patchBankApply(myPart, 0);
        // engine change: the files of the old engine stay but are ignored by the new engine
        if (nKinds > 1) {
          const auto& other = kinds[(&k == &kinds[0]) ? 1 : 0];
          auto fresh = ws->createPatch(int(other.id));            // the Init patch of the new engine
          const size_t before = mw->files.size();
          open::patchBankSetLive(myPart, fresh.get());
          open::patchBankPreload(myPart, *fresh, *ws);            // what Synth::setInstrumentType does
          bool ignored = true;
          for (int sl = 0; sl < 16; sl++) ignored = ignored && !open::patchBankApply(myPart, sl == 0 ? 1 : sl);
          engineKeeps = engineKeeps && ignored && open::patchBankCurrent(myPart) == 0 && mw->files.size() == before;   // no file lost
          engineKeeps = engineKeeps && open::patchBankSelect(myPart, 1, *fresh, *ws) == open::SlotSelect::kOtherEngine && open::patchBankCurrent(myPart) == 1 - 1;
          engineKeeps = engineKeeps && open::patchBankSelect(myPart, 9, *fresh, *ws) == open::SlotSelect::kCopied;   // a free slot still opens
          // the part gets the first engine back: slot 0 is a new Init patch (as stock does), slots 1..15 come back
          auto init = ws->createPatch(int(k.id));
          ws->savePatch(nameOf(myPart, 0), *init);
          open::patchBankSetLive(myPart, live.get());
          open::patchBankPreload(myPart, *live, *ws);
          engineBack = engineBack && open::patchBankApply(myPart, 1) && sameValues(*live, *lo) && open::patchBankApply(myPart, 3) && sameValues(*live, *hi) && open::patchBankApply(myPart, 4) && sameValues(*live, *seven);
        }
        // a foreign file at a slot: the editor refuses it, the FX never uses it (engine of the file is not the part's)
        if (nKinds > 1) {
          auto foreign = ws->createPatch(int((&k == &kinds[0]) ? kinds[1].id : kinds[0].id)); ws->savePatch(nameOf(myPart, 6), *foreign);
          open::patchBankPreload(myPart, *live, *ws);
          sel = sel && !open::patchBankApply(myPart, 6) && open::patchBankSelect(myPart, 6, *live, *ws) == open::SlotSelect::kOtherEngine;
        }
        open::patchBankSetLive(myPart, nullptr);
        for (int sl = 0; sl < 16; sl++) mw->files.erase(nameOf(myPart, sl) + ".patch");
      }
      check(changed, "slot test patches differ from each other (all engines of this build)");
      check(names, "slot files: slot 0 is <P>.patch, slot n is <P>-n.patch (0-1, 2-15), only slots that were saved exist");
      check(fx, "Patch FX: value n copies slot n's file over the live patch, the open slot (0) brings the saved values back (all engines)");
      check(keepsMacros, "Patch FX leaves the macros of the part alone");
      check(missing, "Patch FX: a slot with no file and values 4, 15, 16, 99, -1 are ignored, the values stay");
      check(back, "Patch FX: edited, unsaved values of the open slot come back after the FX (restore or slot = open slot)");
      check(sel, "editor slot select: loads a slot file, an empty slot starts as a copy of the open one, a foreign file is refused");
      check(pend, "an empty slot opened in the editor writes no file and is not in the FX bank until it is saved");
      check(revert, "Cancel in the library browser reloads the open slot (a pending copy: the slot it began from)");
      check(edits, "a saved slot reaches the bank and the files, a project reload rebuilds the same bank on slot 0");
      check(nKinds < 2 || engineKeeps, "engine change: old slot files stay, are ignored and cannot be opened, empty slots still open");
      check(nKinds < 2 || engineBack, "engine change: switching back brings slots 1..15 of that engine back, slot 0 is the new Init");
    }
    // ---- real files from the device (project "OT Test 07 - VAP Only", one VAP part, saved by 1.1.0): slot 0 and slots 1..3 load,
    // slot 4 (named, never saved) opens as a copy, the names file reads (SMOKE_FIXTURES = synth-open/tools/fixtures)
#if defined(SMOKE_FIXTURES) && defined(SYNTH_ENGINE_VAP)
    {
      mw->files.clear();
      bool readAll = true;
      for (const char* f : {"0.patch", "0-1.patch", "0-2.patch", "0-3.patch", "0.names"}) {
        FILE* fp = std::fopen((std::string(SMOKE_FIXTURES) + "/ot-test-07/" + f).c_str(), "rb");
        readAll = readAll && fp; if (!fp) continue;
        auto& v = mw->files[f]; uint8_t b[4096]; size_t got; while ((got = std::fread(b, 1, sizeof(b), fp)) > 0) v.insert(v.end(), b, b + got);
        std::fclose(fp);
      }
      check(readAll, "device files: fixtures ot-test-07 read");
      bool dec = true;
      for (const char* f : {"0.patch", "0-1.patch", "0-2.patch", "0-3.patch"}) {
        open::PatchData pd; const auto& v = mw->files[f];
        const bool ok = open::decodePatch(v.data(), v.size(), pd) && open::typeForEngineId(pd.engine) == int(InstrumentTypeId::kVASynth);
        printf("  %s: %zu B, engine %d, %d params, %d macros -> %s\n", f, v.size(), int(pd.engine), pd.nParams, pd.nMacros, ok ? "VAP" : "REFUSED");
        dec = dec && ok;
      }
      check(dec, "device files: 0.patch and 0-1..0-3.patch decode as VAP");
      auto live = ws->createPatch(int(InstrumentTypeId::kVASynth));
      bool walk = ws->loadPatch("0", *live, true);
      open::patchBankSetLive(0, live.get());
      open::patchBankToSlotZero(0);
      open::patchBankPreload(0, *live, *ws);
      for (int sl : {1, 2, 3}) walk = walk && open::patchBankApply(0, sl);
      open::patchBankApply(0, 0);
      using SS = open::SlotSelect;
      const struct { int slot; SS want; } steps[] = {{1, SS::kLoaded}, {2, SS::kLoaded}, {3, SS::kLoaded}, {4, SS::kCopied}, {3, SS::kLoaded}, {0, SS::kLoaded}, {2, SS::kLoaded}, {0, SS::kLoaded}};
      auto walkSlots = [&](bool& ok) {
        for (const auto& st : steps) {
          const SS got = open::patchBankSelect(0, st.slot, *live, *ws);
          if (got != st.want) printf("  slot %d: select gave %d, want %d\n", st.slot, int(got), int(st.want));
          ok = ok && got == st.want;
        }
      };
      walkSlots(walk);
      check(walk, "device files: preload has slots 1..3, the editor walks 1 2 3 4 3 0 2 0 with no error");
      // the device error: no 8 KB block left on the heap, every slot select gave kError ("Error loading slot")
      // Patch FX bank populated: slots 1..3 apply and put exactly the values of their file into the live patch, slot 0 puts the edited ones back
      auto* livePatch = static_cast<open::OpenPatch*>(live.get());
      auto bankPopulated = [&](const char* tag) {
        bool ok = true;
        float edited[open::kMaxParams], got[open::kMaxParams], want[open::kMaxParams];
        livePatch->getValuesRaw(edited);
        int differ = 0;
        for (int sl : {1, 2, 3}) {
          open::PatchData pd; const auto& v = mw->files[sl == 1 ? "0-1.patch" : sl == 2 ? "0-2.patch" : "0-3.patch"];
          ok = ok && open::decodePatch(v.data(), v.size(), pd) && open::patchBankApply(0, sl);
          livePatch->getValuesRaw(got); livePatch->valuesFromData(pd, want);
          const bool same = std::memcmp(got, want, size_t(livePatch->paramCount()) * sizeof(float)) == 0;
          if (!same) printf("  %s: slot %d applied values differ from its file\n", tag, sl);
          ok = ok && same;
          differ += std::memcmp(got, edited, size_t(livePatch->paramCount()) * sizeof(float)) != 0;
        }
        ok = ok && open::patchBankApply(0, 0);
        livePatch->getValuesRaw(got);
        ok = ok && std::memcmp(got, edited, size_t(livePatch->paramCount()) * sizeof(float)) == 0 && differ > 0;
        if (!ok) printf("  %s: the Patch FX bank is empty or wrong (applied %d of 3 differ from slot 0)\n", tag, differ);
        return ok;
      };
      gShortHeap = 4096;   // the old 8 KB read buffer does not fit (the FX bank of a VAP part, 17 x 60 floats = 4080 B, still did)
      bool shortWalk = ws->loadPatch("0", *live, true);
      open::patchBankToSlotZero(0);
      open::patchBankPreload(0, *live, *ws);
      for (int sl : {1, 2, 3}) shortWalk = shortWalk && open::patchBankApply(0, sl);
      open::patchBankApply(0, 0);
      if (!shortWalk) printf("  short heap: load or preload failed\n");
      const bool fxPop4k = bankPopulated("4 KB cap");
      shortWalk = shortWalk && fxPop4k;
      walkSlots(shortWalk);
      auto saved = ws->createPatch(int(InstrumentTypeId::kVASynth));
      const bool sv = ws->savePatch("short", *live), ld = ws->loadPatch("short", *saved, true), rv = open::patchBankRevert(0, *live, *ws);
      if (!sv || !ld || !rv) printf("  short heap: save %d load %d revert %d\n", sv, ld, rv);
      shortWalk = shortWalk && sv && ld && rv;
      mw->files.erase("short.patch");
      gShortHeap = 0;
      check(shortWalk, "device files, no heap block over 4 KB: load, preload, walk, save and revert still work");
      check(fxPop4k, "device files, no heap block over 4 KB: Patch FX bank is populated after load (slots 1..3 apply their file values)");
      // the bank must not touch the heap at all: with nothing above 512 B it is still built
      gShortHeap = 512;
      open::patchBankToSlotZero(0);
      open::patchBankPreload(0, *live, *ws);
      check(bankPopulated("512 B cap"), "device files, no heap block over 512 B: Patch FX bank is still populated (rows are in SDRAM)");
      gShortHeap = 0;
      open::SlotNames nm;
      const bool names = open::patchNamesRead(0, *ws, nm) && std::strcmp(nm.name[1], "Brass Tacks") == 0 && std::strcmp(nm.name[2], "M1 Organ") == 0 &&
                         std::strcmp(nm.name[3], "Ratio") == 0 && std::strcmp(nm.name[4], "Ratio") == 0;
      check(names, "device files: 0.names gives Brass Tacks, M1 Organ, Ratio, Ratio for slots 1..4");
      open::patchBankSetLive(0, nullptr);
      mw->files.clear();
    }
#endif
    // ---- slot names side file <P>.names (open_slotnames.h codec, patchNamesRead / patchNamesWrite on the Workspace store)
    {
      mw->files.clear();
      auto bytesOf = [](const char* t) { return std::vector<uint8_t>(t, t + std::strlen(t)); };
      auto decode = [](const std::vector<uint8_t>& v, open::SlotNames& n) { return open::decodeSlotNames(v.data(), v.size(), n); };
      open::SlotNames in; std::snprintf(in.name[1], sizeof(in.name[1]), "Warm Pad"); std::snprintf(in.name[12], sizeof(in.name[12]), "Bass 12");
      std::snprintf(in.name[15], sizeof(in.name[15]), "%s", "0123456789012345678901234567890"); // 31 bytes, the longest
      // name[0] is the backup's name (b= line, tested with the slot 0 backup below), empty here: no line
      uint8_t buf[open::kSlotNamesFileMax]; const size_t n = open::encodeSlotNames(in, buf, sizeof(buf));
      const std::string text(reinterpret_cast<char*>(buf), n);
      check(text == "tffnames 1\n1=Warm Pad\n12=Bass 12\n15=0123456789012345678901234567890\n", "slot names file: text header, one slot=name line per named slot 1..15, no b= line without a backup");
      open::SlotNames out; const bool dec = open::decodeSlotNames(buf, n, out);
      bool same = dec; for (int sl = 1; sl < 16; sl++) same = same && std::strcmp(in.name[sl], out.name[sl]) == 0;
      check(same && out.name[0][0] == 0, "slot names file: encode then decode gives the same names (all 15 slots compared)");
      open::SlotNames full; for (int sl = 1; sl < 16; sl++) std::memset(full.name[sl], 'x', open::kSlotNameMax);
      check(open::encodeSlotNames(full, buf, sizeof(buf)) <= open::kSlotNamesFileMax && open::encodeSlotNames(full, buf, 5) == 0 && open::encodeSlotNames(full, buf, 40) == 0,
            "slot names file: the biggest file fits kSlotNamesFileMax, a too small buffer writes nothing");
      open::SlotNames cut; std::memset(cut.name[2], 'y', open::kSlotNameMax);
      const size_t cn = open::encodeSlotNames(cut, buf, sizeof(buf));
      check(cn == 11 + 2 + 31 + 1, "slot names file: a 31 byte name is written whole");
      // bounded reads of damaged files: nothing crashes, nothing past a limit is taken, good lines still count
      bool bad = true; open::SlotNames d;
      bad = bad && !decode(bytesOf(""), d) && !open::anySlotName(d);
      bad = bad && !decode(bytesOf("garbage\n1=Name\n"), d) && !open::anySlotName(d);                       // no header: the file is ignored whole
      bad = bad && !decode(bytesOf("tffnames 2\n1=Name\n"), d) && !open::anySlotName(d);                    // other version
      bad = bad && decode(bytesOf("tffnames 1\n"), d) && !open::anySlotName(d);                              // header only
      bad = bad && decode(bytesOf("tffnames 1\n0=zero\n16=sixteen\n99=x\n-1=neg\n1x=bad\n=none\n7=\n8\n"), d) && !open::anySlotName(d); // slot 0, 16.., malformed, empty name
      bad = bad && decode(bytesOf("tffnames 1\n3=0123456789012345678901234567890X\n"), d) && !open::anySlotName(d);         // 32 bytes: too long, skipped
      bad = bad && decode(bytesOf("tffnames 1\n4=ab\x01" "cd\n5=ok\n"), d) && d.name[4][0] == 0 && std::strcmp(d.name[5], "ok") == 0;   // control character: that line is skipped
      bad = bad && decode(bytesOf("tffnames 1\r\n2=Crlf\r\n"), d) == false;                                   // the header is exact: a CRLF file is not ours
      bad = bad && decode(bytesOf("tffnames 1\n2=Crlf\r\n6=Last"), d) && std::strcmp(d.name[2], "Crlf") == 0 && std::strcmp(d.name[6], "Last") == 0; // CR tolerated, no final newline
      bad = bad && decode(bytesOf("tffnames 1\n9=first\n9=second\n"), d) && std::strcmp(d.name[9], "second") == 0;           // duplicate: the last one
      { std::vector<uint8_t> big = bytesOf("tffnames 1\n"); while (big.size() < 5000) { auto l = bytesOf("1=a\n"); big.insert(big.end(), l.begin(), l.end()); }
        big.insert(big.end(), 3000, 'Z'); bad = bad && decode(big, d) && std::strcmp(d.name[1], "a") == 0; }                  // 8000 bytes: read stops at kSlotNamesFileMax
      { uint32_t seed = 12345; for (int round = 0; round < 3000; round++) { std::vector<uint8_t> r(size_t(seed % 700)); for (auto& b : r) { seed = seed * 1664525u + 1013904223u; b = uint8_t(seed >> 24); }
          if (round % 2 == 0 && r.size() > 11) std::memcpy(r.data(), "tffnames 1\n", 11);
          decode(r, d); for (int sl = 0; sl < 16; sl++) bad = bad && std::strlen(d.name[sl]) <= open::kSlotNameMax; } }          // random bytes, half of them behind a valid header
      check(bad, "slot names file: missing, empty, foreign, truncated and random files give no name or only the valid lines, names never pass 31 bytes");
      // the Workspace store: Workspace/<P>.names
      mw->files.clear(); mw->files["0.patch"] = bytesOf("stand-in patch bytes"); mw->files["0-1.patch"] = bytesOf("stand-in slot 1");
      const auto patchesBefore = mw->files;
      open::SlotNames back;
      bool io = !open::patchNamesRead(0, *ws, back) && !open::anySlotName(back);                                 // no file: unnamed, no crash
      io = io && open::patchNamesWrite(1, *ws, open::SlotNames()) && mw->files.count("1.names") == 0;           // nothing named and no file: nothing written
      io = io && open::patchNamesWrite(0, *ws, in) && mw->files.count("0.names") == 1 && mw->files.count("1.names") == 0;
      io = io && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Warm Pad") == 0 && std::strcmp(back.name[12], "Bass 12") == 0 && std::strcmp(back.name[15], in.name[15]) == 0 && back.name[2][0] == 0;
      io = io && !open::patchNamesRead(1, *ws, back) && !open::anySlotName(back);                                // another part has its own file
      open::SlotNames one; std::snprintf(one.name[1], sizeof(one.name[1]), "Renamed");
      io = io && open::patchNamesWrite(0, *ws, one) && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Renamed") == 0 && back.name[12][0] == 0; // a rewrite replaces the whole file
      io = io && open::patchNamesWrite(0, *ws, open::SlotNames()) && mw->files["0.names"] == bytesOf("tffnames 1\n") && open::patchNamesRead(0, *ws, back) && !open::anySlotName(back); // all names cleared: the file is corrected, not left stale
      mw->files["0.names"] = bytesOf("tffnames 1\n1=Fine\n\xff\xfe\x00\x01 junk"); io = io && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Fine") == 0;
      mw->files["0.names"] = std::vector<uint8_t>(100000, 'q'); io = io && !open::patchNamesRead(0, *ws, back) && !open::anySlotName(back);   // a huge foreign file is read up to the cap and refused
      mw->files["0.names"].clear(); io = io && !open::patchNamesRead(0, *ws, back);
      bool untouched = true; for (const auto& [k, v] : patchesBefore) untouched = untouched && mw->files.count(k) && mw->files[k] == v;
      check(io, "slot names: Workspace/<P>.names is written and read per part, a missing, empty or damaged file reads as no names, an unnamed part gets no file");
      check(untouched && mw->files.count("0.names") == 1, "slot names: the side file never touches a .patch file (stock files stay byte identical)");
      mw->files.clear();
    }
    // ---- full project cycle, as on the device: 3 parts with every slot 0..15 saved and named (31 byte names), Workspace -> project
    // folder (save) -> fresh Workspace (open), copied and pruned like Synth::copyWorkspacePatches, then the load path of
    // WorkspaceProjectLoad (bank preload twice per part as setInstrumentType + loadPatchFromWorkspace, names read). Every open is closed.
    {
      mw->files.clear(); mw->openNow = 0;
      const auto& k = kinds[0];
      auto nameOf = [](int part, int slot) { char n[8]; open::patchSlotName(part, slot, n); return std::string(n); };
      std::vector<std::unique_ptr<Patch>> slotPatch[open::kPatchBankParts];
      for (int pt = 0; pt < open::kPatchBankParts; pt++) {
        open::SlotNames nm;
        for (int sl = 0; sl < open::kPatchBankSlots; sl++) {
          auto p = ws->createPatch(int(k.id));
          const auto& m = p->getMetadata();
          for (size_t i = 0; i < m.params.size(); i++) { const float a = float(m.params[i].minValue), b = float(m.params[i].maxValue);
            p->setParamValue(int(i), a + (b - a) * float((sl * 7 + pt * 3 + int(i)) % 16) / 15.0f, Patch::ParamChangeSource::kKnob); }
          ws->savePatch(nameOf(pt, sl), *p); slotPatch[pt].push_back(std::move(p));
          if (sl > 0) std::snprintf(nm.name[sl], sizeof(nm.name[sl]), "P%d slot %02d name 0123456789abcd", pt, sl); // 31 bytes
        }
        open::patchNamesWrite(pt, *ws, nm);
      }
      // save: Workspace -> project, open: project -> a Workspace that still holds another project's files (slot 15 and names pruned)
      std::map<std::string, std::vector<uint8_t>> project = mw->files;
      mw->files.clear(); mw->files["0-15.patch"] = {1, 2, 3}; mw->files["1.names"] = {'x'};
      MemBankOps bankOps{ &project, &mw->files, "" };
      bool cyc = true;
      for (int pt = 0; pt < open::kPatchBankParts; pt++) cyc = cyc && open::copyPartBank(bankOps, pt, "S", "D", true);   // Synth::copyWorkspacePatches
      cyc = cyc && mw->files == project;
      std::unique_ptr<Patch> live[open::kPatchBankParts];
      for (int pt = 0; pt < open::kPatchBankParts; pt++) {
        live[pt] = ws->createPatch(int(k.id));
        open::patchBankSetLive(pt, live[pt].get());
        open::patchBankPreload(pt, *live[pt], *ws);                       // Synth::setInstrumentType
        cyc = cyc && ws->loadPatch(nameOf(pt, 0), *live[pt], true);       // Synth::loadWorkspacePatch
        open::patchBankPreload(pt, *live[pt], *ws);                       // Synth::reloadPatchBank
        open::SlotNames back; cyc = cyc && open::patchNamesRead(pt, *ws, back);
        for (int sl = 1; sl < open::kPatchBankSlots; sl++) { char want[40]; std::snprintf(want, sizeof(want), "P%d slot %02d name 0123456789abcd", pt, sl);
          cyc = cyc && std::strcmp(back.name[sl], want) == 0; }
        for (int sl = 15; sl >= 0; sl--) {
          cyc = cyc && open::patchBankApply(pt, sl);
          const auto& m = live[pt]->getMetadata();
          float got[open::kMaxParams], want[open::kMaxParams];   // what plays: getParamValue gives the edited values while the FX has replaced them
          static_cast<open::OpenPatch&>(*live[pt]).getValuesRaw(got); static_cast<open::OpenPatch&>(*slotPatch[pt][sl]).getValuesRaw(want);
          for (size_t i = 0; i < m.params.size() && cyc; i++) cyc = got[i] == want[i];
        }
        cyc = cyc && open::patchBankCurrent(pt) == 0;
      }
      check(cyc, "project cycle: 3 parts x 16 named slots save, copy and prune into a used Workspace, load, preload twice, every slot and name comes back");
      check(mw->openNow == 0, "project cycle: every file opened on the load and save path is closed again");
      bool zero = open::patchBankSelect(0, 5, *live[0], *ws) == open::SlotSelect::kLoaded && open::patchBankCurrent(0) == 5 && open::patchBankApply(0, 9);
      open::patchBankToSlotZero(0);   // Synth::loadPatchFromWorkspace, before slot 0 is read
      zero = zero && open::patchBankCurrent(0) == 0 && open::patchBankSelect(0, 0, *live[0], *ws) == open::SlotSelect::kSame;
      check(zero, "project load: the editor is back on slot 0 before slot 0 is read, a fallback save never writes a slot of the old project");
      for (int pt = 0; pt < open::kPatchBankParts; pt++) open::patchBankSetLive(pt, nullptr);
      mw->files.clear();
    }
    // ---- slot 0 backup of an engine change (<P>-b.patch): the file functions of the shim, then (below) the whole change with real files
    {
      mw->files.clear(); mw->openNow = 0;
      const int tA = int(kinds[0].id);
      auto nameOf = [](int part, int slot) { char n[8]; open::patchSlotName(part, slot, n); return std::string(n); };
      auto live = ws->createPatch(tA);
      open::patchBankSetLive(0, live.get());
      open::patchBankToSlotZero(0);
      open::patchBankPreload(0, *live, *ws);
      bool ok = nameOf(0, open::kBackupSlot) == "0-b" && nameOf(2, open::kBackupSlot) == "2-b" && nameOf(1, 15) == "1-15";
      using BK = open::Backup;
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSkipped && mw->files.count("0-b.patch") == 0;           // no slot 0 file yet: nothing to keep
      ws->savePatch("0", *live);
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSkipped && mw->files.count("0-b.patch") == 0;           // untouched Init: no backup
      const auto& m0 = live->getMetadata(); const float v0 = *live->getParamValue(0), hi = float(m0.params[0].maxValue), lo = float(m0.params[0].minValue);
      live->setParamValue(0, v0 == hi ? lo : hi, Patch::ParamChangeSource::kKnob);
      ws->savePatch("0", *live);
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSaved && mw->files["0-b.patch"] == mw->files["0.patch"];  // edited: byte copy
      const auto kept = mw->files["0-b.patch"];
      live->setParamValue(0, v0, Patch::ParamChangeSource::kKnob); ws->savePatch("0", *live);                          // slot 0 back to the Init values
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSkipped && mw->files["0-b.patch"] == kept;                // an older backup stays
      mw->files["0.patch"] = {1, 2, 3, 4};
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSkipped && mw->files["0-b.patch"] == kept;                // damaged slot 0 file: nothing to keep
      mw->files["0.patch"].assign(9000, 7);
      ok = ok && open::patchBackupSave(0, *live, *ws) == BK::kSkipped && mw->files["0-b.patch"] == kept;               // over kPatchFileMax
      check(ok, "slot 0 backup: an untouched Init, a damaged or missing slot 0 file leave the backup alone, an edited slot 0 is copied byte for byte");
      // restore: the backup is of this engine -> slot 0 file, live patch and bank; foreign engine, damaged or missing -> nothing changes
      ws->savePatch("0", *live);
      auto fresh = ws->createPatch(tA);
      bool rest = open::patchBackupRestore(0, *fresh, *ws) && mw->files["0.patch"] == kept && *fresh->getParamValue(0) == (v0 == hi ? lo : hi);
      open::patchBankSetLive(0, fresh.get()); open::patchBankPreload(0, *fresh, *ws);
      rest = rest && open::patchBankApply(0, 0);
      if (nKinds > 1) {
        const int tB = int(kinds[1].id); auto other = ws->createPatch(tB); const auto before = mw->files["0.patch"];
        rest = rest && !open::patchBackupRestore(0, *other, *ws) && mw->files["0.patch"] == before;                  // the backup is of engine A
      }
      mw->files["0-b.patch"] = {9, 9, 9}; auto f2 = ws->createPatch(tA); const auto before2 = mw->files["0.patch"];
      rest = rest && !open::patchBackupRestore(0, *f2, *ws) && mw->files["0.patch"] == before2;
      mw->files.erase("0-b.patch");
      rest = rest && !open::patchBackupRestore(0, *f2, *ws) && mw->files["0.patch"] == before2;
      check(rest, "slot 0 backup: restore needs a backup of the engine (the file's engine field decides), else slot 0 and the live patch stay");
      // no heap block over 512 B: backup and restore work through the SDRAM scratch
      mw->files["0.patch"] = kept; mw->files.erase("0-b.patch");
      gShortHeap = 512;
      const bool small = open::patchBackupSave(0, *live, *ws) == BK::kSaved && open::patchBackupRestore(0, *fresh, *ws);
      gShortHeap = 0;
      check(small && mw->files["0-b.patch"] == kept, "slot 0 backup: with no heap block over 512 B backup and restore still work (scratch in SDRAM)");
      check(mw->openNow == 0, "slot 0 backup: every file opened is closed again");
      // names file: the backup's name is the line b=<name>
      open::SlotNames in; std::snprintf(in.name[0], sizeof(in.name[0]), "Old Pad"); std::snprintf(in.name[3], sizeof(in.name[3]), "Three");
      uint8_t buf[open::kSlotNamesFileMax]; const size_t n = open::encodeSlotNames(in, buf, sizeof(buf));
      const std::string text(buf, buf + n);
      open::SlotNames out; const bool dec = open::decodeSlotNames(buf, n, out);
      open::SlotNames only; std::snprintf(only.name[0], sizeof(only.name[0]), "x");
      open::SlotNames d2;
      bool cod = text == "tffnames 1\nb=Old Pad\n3=Three\n" && dec && std::strcmp(out.name[0], "Old Pad") == 0 && std::strcmp(out.name[3], "Three") == 0 && open::anySlotName(only);
      const std::string junk = "tffnames 1\nb=\nb\nbb=x\n0=zero\nb=ok\n";
      cod = cod && open::decodeSlotNames(reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), d2) && std::strcmp(d2.name[0], "ok") == 0 && d2.name[1][0] == 0;
      cod = cod && open::encodeSlotNames(in, buf, 16) == 0;                                                     // too small for the b= line
      std::memset(in.name[0], 'z', open::kSlotNameMax); for (int sl = 1; sl < 16; sl++) std::memset(in.name[sl], 'x', open::kSlotNameMax);
      cod = cod && open::encodeSlotNames(in, buf, sizeof(buf)) <= open::kSlotNamesFileMax && open::encodeSlotNames(in, buf, sizeof(buf)) > 500;
      check(cod, "slot names file: b=<name> carries the backup's name, slot 0 lines stay refused, the biggest file with a backup name still fits");
      open::patchBankSetLive(0, nullptr);
      mw->files.clear();
    }
#if defined(SMOKE_FIXTURES) && defined(SYNTH_ENGINE_VAP)
    // ---- the whole engine change with the real files of the device (project "OT Test 07 - VAP Only": VAP slot 0 and slots 1..3 named
    // Brass Tacks, M1 Organ, Ratio). The Model below mirrors, step for step, what Synth::backupSlotZero / setInstrumentType /
    // reloadPatchBank(keepNames) / restoreSlotZeroBackup / persistSlotNames / selectPatchSlot do around the shim calls (core Synth.cpp
    // is not compiled here): same calls in the same order, the names kept in the same places.
    {
      const int tV = int(InstrumentTypeId::kVASynth);
      int others[2] = { -1, -1 }, no = 0;
      for (const auto& kd : kinds) if (int(kd.id) != tV && no < 2) others[no++] = int(kd.id);
      auto nameOf = [](int part, int slot) { char n[8]; open::patchSlotName(part, slot, n); return std::string(n); };
      std::map<std::string, std::vector<uint8_t>> fixture;
      bool read = true;
      for (const char* f : {"0.patch", "0-1.patch", "0-2.patch", "0-3.patch", "0.names"}) {
        FILE* fp = std::fopen((std::string(SMOKE_FIXTURES) + "/ot-test-07/" + f).c_str(), "rb"); read = read && fp; if (!fp) continue;
        uint8_t b[4096]; size_t got; while ((got = std::fread(b, 1, sizeof(b), fp)) > 0) fixture[f].insert(fixture[f].end(), b, b + got);
        std::fclose(fp);
      }
      check(read && no == 2, "engine change: fixtures read and the build has two engines besides VAP");
      struct Model {
        std::unique_ptr<Patch> live; int type = 0; std::string name, projName, slotNames[16], backupName; bool dirty = false;
      } m;
      auto savePart = [&]() {                                           // Synth::savePatchToWorkspace
        const bool saved = open::patchSaveOpenSlot(0, *m.live, *ws); open::settleDirtyAfterSave(saved, m.dirty); return saved; };
      auto persist = [&]() {                                            // Synth::persistSlotNames
        open::SlotNames nm; std::snprintf(nm.name[0], sizeof(nm.name[0]), "%s", m.backupName.c_str());
        const int cur = open::patchBankCurrent(0);
        for (int sl = 1; sl < 16; sl++) { const std::string& s = sl == cur ? m.name : m.slotNames[sl]; if (s != "Unnamed") std::snprintf(nm.name[sl], sizeof(nm.name[sl]), "%s", s.c_str()); }
        open::patchNamesWrite(0, *ws, nm); };
      auto openProject = [&](const std::map<std::string, std::vector<uint8_t>>& project, int type, const char* slot0Name) {   // project open: copyWorkspacePatches, load path
        for (int sl = 0; sl <= open::kBackupSlot; sl++) { const std::string f = nameOf(0, sl) + ".patch";
          if (project.count(f)) mw->files[f] = project.at(f); else if (sl > 0) mw->files.erase(f); }
        if (project.count("0.names")) mw->files["0.names"] = project.at("0.names"); else mw->files.erase("0.names");
        m.type = type; m.live = ws->createPatch(type); open::patchBankSetLive(0, m.live.get());
        for (auto& s : m.slotNames) s.clear(); m.backupName.clear();
        open::patchBankPreload(0, *m.live, *ws);                                    // Synth::setInstrumentType
        open::patchBankToSlotZero(0);                                               // Synth::loadPatchFromWorkspace
        const bool loaded = ws->loadPatch("0", *m.live, true);
        open::patchBankPreload(0, *m.live, *ws);                                    // Synth::reloadPatchBank
        open::SlotNames nm; open::patchNamesRead(0, *ws, nm);                       // Synth::loadSlotNames
        for (int sl = 1; sl < 16; sl++) m.slotNames[sl] = nm.name[sl];
        m.backupName = nm.name[0]; m.name = m.projName = slot0Name; m.dirty = false; return loaded; };
      auto selectSlot = [&](int slot) {                                 // Synth::selectPatchSlot
        const int prev = open::patchBankCurrent(0);
        if (m.dirty) savePart();
        const auto r = open::patchBankSelect(0, slot, *m.live, *ws);
        if (r == open::SlotSelect::kLoaded || r == open::SlotSelect::kCopied) {
          m.slotNames[prev] = m.name;
          if (slot == 0) m.name = m.slotNames[0].empty() ? "Init" : m.slotNames[0];
          else if (r == open::SlotSelect::kLoaded) m.name = m.slotNames[slot].empty() ? "Unnamed" : m.slotNames[slot];
          m.dirty = false; persist(); }
        return r; };
      auto changeEngine = [&](int type) {                               // SynthEditor::changeSynthModel
        if (m.dirty && open::patchBankCurrent(0) > 0 && !savePart()) return false;   // Synth::flushPatchSlot
        if (m.dirty && !savePart()) return false;                       // Synth::backupSlotZero
        const auto bk = open::patchBackupSave(0, *m.live, *ws);
        if (!open::backupAllowsEngineChange(bk)) return false;          // a failed backup: the engine stays
        if (bk == open::Backup::kSaved) { m.backupName = m.projName; persist(); }
        m.type = type; auto fresh = ws->createPatch(type);              // Synth::setInstrumentType
        open::patchBankSetLive(0, fresh.get()); m.live = std::move(fresh); m.name = "Init";
        open::patchBankPreload(0, *m.live, *ws); m.slotNames[0].clear();
        if (open::patchBackupRestore(0, *m.live, *ws)) { m.name = m.backupName.empty() ? "Unnamed" : m.backupName; m.dirty = false; }   // Synth::restoreSlotZeroBackup
        else { open::patchSaveOpenSlot(0, *m.live, *ws); m.dirty = false; }                        // initCurrentPatch -> initPatch
        m.projName = m.name;
        persist(); return true; };                                       // the autosave that follows
      auto sameAsFile = [&](const std::vector<uint8_t>& file) {          // the live patch holds the values of that file
        open::PatchData pd; auto& op = static_cast<open::OpenPatch&>(*m.live);
        if (!open::decodePatch(file.data(), file.size(), pd) || open::typeForEngineId(pd.engine) != m.type) return false;
        float want[open::kMaxParams], got[open::kMaxParams]; op.valuesFromData(pd, want); op.getValuesRaw(got);
        return std::memcmp(want, got, size_t(op.paramCount()) * sizeof(float)) == 0; };
      auto engineOf = [&](const std::string& f) { open::PatchData pd; const auto& v = mw->files[f]; return open::decodePatch(v.data(), v.size(), pd) ? open::typeForEngineId(pd.engine) : -2; };
      auto namesFile = [&]() { open::SlotNames nm; open::patchNamesRead(0, *ws, nm); return nm; };
      auto start = [&]() { mw->files.clear(); mw->openNow = 0; return openProject(fixture, tV, "Fixture Zero"); };
      auto isInit = [&](int type) {                                     // slot 0 is an untouched Init of that engine: same bytes as a new patch saved
        auto p = ws->createPatch(type); open::PatchData pd; static_cast<open::OpenPatch&>(*p).exportData(pd);
        uint8_t b[4096]; const size_t n = open::encodePatch(pd, b, sizeof(b)); return mw->files["0.patch"] == std::vector<uint8_t>(b, b + n); };
      auto namesKept = [&]() { auto nm = namesFile();
        return std::strcmp(nm.name[1], "Brass Tacks") == 0 && std::strcmp(nm.name[2], "M1 Organ") == 0 && std::strcmp(nm.name[3], "Ratio") == 0 &&
               m.slotNames[1] == "Brass Tacks" && m.slotNames[2] == "M1 Organ" && m.slotNames[3] == "Ratio"; };

      // A -> B -> A
      bool ok = start() && sameAsFile(fixture["0.patch"]) && namesKept();
      changeEngine(others[0]);
      ok = ok && mw->files["0-b.patch"] == fixture["0.patch"] && engineOf("0.patch") == others[0] && isInit(others[0]) && m.name == "Init" && namesKept()
              && std::strcmp(namesFile().name[0], "Fixture Zero") == 0;
      check(ok, "engine change A to B: slot 0 file is backed up byte for byte, B starts with Init, the names of slots 1..3 stay in memory and in <P>.names");
      ok = open::patchBankSelect(0, 1, *m.live, *ws) == open::SlotSelect::kOtherEngine && open::patchBankCurrent(0) == 0;   // B cannot open the VAP slot files
      changeEngine(tV);
      ok = ok && mw->files["0.patch"] == fixture["0.patch"] && sameAsFile(fixture["0.patch"]) && m.name == "Fixture Zero" && m.projName == "Fixture Zero" && namesKept()
              && open::patchBankCurrent(0) == 0;
      for (int sl : {1, 2, 3}) ok = ok && open::patchBankApply(0, sl);      // the Patch FX bank has the three slots again
      open::patchBankApply(0, 0);
      ok = ok && selectSlot(2) == open::SlotSelect::kLoaded && sameAsFile(fixture["0-2.patch"]) && m.name == "M1 Organ" && selectSlot(0) == open::SlotSelect::kLoaded && m.name == "Fixture Zero";
      check(ok, "engine change A to B and back: slot 0 sound and name come back, slots 1..3 play and keep their names (memory and file)");

      // A -> B -> C -> A
      ok = start();
      changeEngine(others[0]); changeEngine(others[1]);
      ok = ok && mw->files["0-b.patch"] == fixture["0.patch"] && engineOf("0.patch") == others[1] && isInit(others[1]) && namesKept();
      changeEngine(tV);
      ok = ok && mw->files["0.patch"] == fixture["0.patch"] && sameAsFile(fixture["0.patch"]) && m.name == "Fixture Zero" && namesKept();
      changeEngine(others[1]); changeEngine(others[0]); changeEngine(tV);                   // and again, over a longer chain
      ok = ok && mw->files["0.patch"] == fixture["0.patch"] && m.name == "Fixture Zero" && namesKept();
      check(ok, "engine change A to B to C to A: an untouched Init of B does not push out the backup, A comes back with its sound, name and slot names");

      // slot 0 edited on B (documented trade-off): the backup is B's now, A gets Init
      ok = start(); changeEngine(others[0]);
      { const auto& md = m.live->getMetadata(); const float v = *m.live->getParamValue(0), hi = float(md.params[0].maxValue), lo = float(md.params[0].minValue);
        m.live->setParamValue(0, v == hi ? lo : hi, Patch::ParamChangeSource::kKnob); m.dirty = true; }
      changeEngine(tV);
      ok = ok && engineOf("0.patch") == tV && isInit(tV) && m.name == "Init" && engineOf("0-b.patch") == others[0] && namesKept();
      check(ok, "engine change: slot 0 edited on B, then B to A gives an Init for A (the one backup was replaced by B's edited patch), names 1..3 stay");

      // on slot 2: the backup is slot 0's file, not slot 2's (also with slot 2 edited and not yet saved)
      ok = start() && selectSlot(2) == open::SlotSelect::kLoaded && open::patchBankCurrent(0) == 2;
      { const float v = *m.live->getParamValue(0), hi = float(m.live->getMetadata().params[0].maxValue), lo = float(m.live->getMetadata().params[0].minValue);
        m.live->setParamValue(0, v == hi ? lo : hi, Patch::ParamChangeSource::kKnob); m.dirty = true; }
      changeEngine(others[0]);
      ok = ok && mw->files["0-b.patch"] == fixture["0.patch"] && mw->files["0-b.patch"] != fixture["0-2.patch"] && mw->files["0-2.patch"] != fixture["0-2.patch"]
              && engineOf("0.patch") == others[0] && open::patchBankCurrent(0) == 0 && m.name == "Init" && namesKept();
      changeEngine(tV);
      ok = ok && mw->files["0.patch"] == fixture["0.patch"] && open::patchBankCurrent(0) == 0 && m.name == "Fixture Zero" && open::patchBankSelect(0, 2, *m.live, *ws) == open::SlotSelect::kLoaded;
      check(ok, "engine change while on slot 2: slot 0's file is backed up (not slot 2's), the edit of slot 2 is saved, back on A slot 0 is restored");

      // save to the project and reopen: the backup, its name and the slot names are carried
      ok = start(); changeEngine(others[0]); persist();                                     // WorkspaceProjectSave -> saveDirtyWorkspacePatches
      const auto project = mw->files; const std::string projName = m.projName; const int projType = m.type;
      ok = ok && project.count("0-b.patch") == 1 && project.at("0-b.patch") == fixture["0.patch"];
      mw->files.clear(); mw->files["0-b.patch"] = {5, 5, 5}; mw->files["0-2.patch"] = {1}; mw->files["0.names"] = {'x'};   // another project's Workspace
      ok = ok && openProject(project, projType, projName.c_str()) && mw->files["0-b.patch"] == fixture["0.patch"] && std::strcmp(namesFile().name[0], "Fixture Zero") == 0 && m.backupName == "Fixture Zero" && namesKept();
      changeEngine(tV);                                                                      // B to A after the reopen (B's slot 0 is an untouched Init)
      ok = ok && mw->files["0.patch"] == fixture["0.patch"] && sameAsFile(fixture["0.patch"]) && m.name == "Fixture Zero" && namesKept();
      check(ok, "backup survives project save and reopen: <P>-b.patch and its name are carried, B back to A restores slot 0 in the reopened project");
      // a project without a backup prunes the Workspace's stale one: nothing is restored from another project
      auto noBackup = project; noBackup.erase("0-b.patch"); noBackup["0.names"] = fixture["0.names"];
      mw->files.clear(); mw->files["0-b.patch"] = mw->files.count("x") ? std::vector<uint8_t>() : fixture["0.patch"];
      ok = openProject(noBackup, projType, projName.c_str()) && mw->files.count("0-b.patch") == 0;
      changeEngine(tV);
      ok = ok && engineOf("0.patch") == tV && isInit(tV) && m.name == "Init";
      check(ok, "reopen of a project without a backup removes the Workspace's old backup: the engine change gives Init");
      check(mw->openNow == 0, "engine change: every file opened is closed again");
      open::patchBankSetLive(0, nullptr);
      mw->files.clear();
    }
#endif
    // ---- review blockers: edits while the Patch FX plays, the atomic save, a replaced patch, failed saves and copies, card read errors
    {
      mw->files.clear(); mw->openNow = 0; mw->healthy();
      const int T = kLead;
      auto nameOf = [](int part, int slot) { char n[8]; open::patchSlotName(part, slot, n); return std::string(n); };
      auto raw = [](Patch& p) { std::vector<float> v(open::kMaxParams); static_cast<open::OpenPatch&>(p).getValuesRaw(v.data()); v.resize(p.getMetadata().params.size()); return v; };
      auto fill = [](Patch& p, float t) { const auto& m = p.getMetadata();
        for (size_t i = 0; i < m.params.size(); i++) { const float a = float(m.params[i].minValue), b = float(m.params[i].maxValue); p.setParamValue(int(i), a + (b - a) * t, Patch::ParamChangeSource::kKnob); } };
      auto base = ws->createPatch(T), lo = ws->createPatch(T), hi = ws->createPatch(T), init = ws->createPatch(T);
      fill(*base, 0.5f); fill(*lo, 0.1f); fill(*hi, 0.9f);
      const auto baseV = raw(*base), loV = raw(*lo), hiV = raw(*hi), initV = raw(*init);
      const float p0 = baseV[0], edit0 = p0 == float(base->getMetadata().params[0].maxValue) ? float(base->getMetadata().params[0].minValue) : float(base->getMetadata().params[0].maxValue);
      auto editedV = baseV; editedV[0] = edit0;
      check(baseV != loV && loV != hiV && baseV != initV && edit0 != p0, "review blockers: the test slots differ from each other and from Init");
      // slot 0 = base, 1 = lo, 3 = hi on the card, the part's live patch loaded from slot 0, the bank built
      auto setup = [&]() {
        mw->files.clear(); mw->healthy(); ws->savePatch("0", *base); ws->savePatch("0-1", *lo); ws->savePatch("0-3", *hi);
        auto live = ws->createPatch(T); ws->loadPatch("0", *live, true);
        open::patchBankSetLive(0, live.get()); open::patchBankToSlotZero(0); open::patchBankPreload(0, *live, *ws); return live; };
      auto fileValues = [&](const char* name) { auto p = ws->createPatch(T); return ws->loadPatch(name, *p, true) ? raw(*p) : std::vector<float>(); };
      // B1: an edit made while the FX has slot 1 playing is not lost to the next restore, FX round trip or save
      {
        auto live = setup();
        open::patchBankApply(0, 1);
        bool ok = raw(*live) == loV && *live->getParamValue(0) == p0 && *live->getParamValueEditMapped(0) == *base->getParamValueEditMapped(0);   // reads: the edited value
        live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);                  // the knob (the caller holds the audio lock)
        ok = ok && raw(*live) == editedV && *live->getParamValue(0) == edit0;            // the edit is on the edited values, the FX values are gone from the part
        open::patchBankApply(0, 1); ok = ok && raw(*live) == loV;                        // the FX plays slot 1 again
        open::patchBankApply(0, 0); ok = ok && raw(*live) == editedV;                    // and goes back to the open slot: the edit is there
        open::patchBankApply(0, 3); open::patchBankRestore(0); ok = ok && raw(*live) == editedV;   // a restore keeps it too
        open::patchBankApply(0, 3);
        ok = ok && open::patchSaveOpenSlot(0, *live, *ws) && fileValues("0") == editedV;   // a save writes it
        check(ok, "B1: a knob edit made while the Patch FX plays another slot survives the FX, a restore and a save");
      }
      // B2: the file is one patch and the bank row is what was written, whatever the FX does before and while the file is written
      {
        auto live = setup();
        live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);
        open::patchBankApply(0, 1);                                                      // an FX step just before the save
        int fxRuns = 0; mw->onWrite = [&](const std::string&) { fxRuns++; open::patchBankApply(0, 3); };   // and one while the file is written
        bool ok = open::patchSaveOpenSlot(0, *live, *ws); mw->healthy();
        ok = ok && fxRuns > 0 && fileValues("0") == editedV;                             // neither slot 1 nor slot 3 reached the file, no mixture
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kLoaded && raw(*live) == loV;
        ok = ok && open::patchBankApply(0, 0) && raw(*live) == editedV;                  // the bank row of slot 0 is what was written
        check(ok, "B2: a save under FX steps writes the edited open slot and the bank follows the written values");
      }
      // B3: a replaced or loaded patch leaves no shadow of the previous one
      {
        auto live = setup();
        open::patchBankApply(0, 1);                                                      // shadow = slot 0's values, FX values live
        auto fresh = ws->createPatch(T);
        open::patchBankSetLive(0, fresh.get());                                          // Synth::setPatch: Init
        bool ok = open::patchBankApply(0, 0) && raw(*fresh) == initV;                    // the FX to the open slot does not write the old shadow
        ok = ok && open::patchSaveOpenSlot(0, *fresh, *ws) && fileValues("0") == initV;  // Init is saved as Init
        check(ok, "B3: Init after the FX ran saves the Init values, never the sound of the previous patch");
        live = setup();
        mw->onRead = [&](const std::string& path) { if (path == "0-1.patch") open::patchBankApply(0, 3); };   // the FX runs while slot 1 is read
        ok = open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kLoaded; mw->healthy();
        ok = ok && raw(*live) == loV;
        open::patchBankRestore(0); ok = ok && raw(*live) == loV;                         // the next restore does not overwrite the loaded slot
        live = setup(); live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);
        mw->onRead = [&](const std::string& path) { if (path == "0-2.patch") open::patchBankApply(0, 3); };   // slot 2 has no file: a copy of the open one
        ok = ok && open::patchBankSelect(0, 2, *live, *ws) == open::SlotSelect::kCopied; mw->healthy();
        ok = ok && raw(*live) == editedV && open::patchBankCurrent(0) == 2;              // the copy has the edited values, not slot 3's
        live = setup(); open::patchBankApply(0, 1);
        ok = ok && ws->loadPatch("0-3", *live, true); open::patchBankRestore(0); ok = ok && raw(*live) == hiV;   // a library load replaces the shadow too
        check(ok, "B3: a slot loaded or copied while the FX runs, and a library load, are not undone by the next restore");
      }
      // B4: a failed save is reported, the old file stays, nothing is published to the bank, and a slot switch is refused
      {
        auto live = setup();
        live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);
        const auto before = mw->files["0.patch"];
        bool ok = true;
        for (int mode = 0; mode < 3; mode++) {
          mw->healthy(); mw->failWrite = mode == 0; mw->failOpenWrite = mode == 1; mw->failReplace = mode == 2;
          ok = ok && !open::patchSaveOpenSlot(0, *live, *ws) && mw->files["0.patch"] == before;
          for (const auto& kv : mw->files) ok = ok && kv.first.find(".tmp") == std::string::npos;   // no half file left behind
        }
        mw->healthy();
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kLoaded && open::patchBankApply(0, 0) && raw(*live) == baseV;   // the bank row is the old file's
        check(ok, "B4: a save that fails (card full, open or rename fails) keeps the old file and the old bank row");
        live = setup(); live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);
        bool dirty = true;
        auto saveOpen = [&]() { if (!open::patchSaveOpenSlot(0, *live, *ws)) return false; dirty = false; return true; };       // Synth::savePatchToWorkspace
        auto selectSlot = [&](int slot) { if (dirty && !saveOpen()) return open::SlotSelect::kSaveFailed;                      // Synth::selectPatchSlot
                                          return open::patchBankSelect(0, slot, *live, *ws); };
        mw->failWrite = true;
        ok = selectSlot(1) == open::SlotSelect::kSaveFailed && dirty && open::patchBankCurrent(0) == 0 && raw(*live) == editedV && mw->files["0.patch"] == before;
        mw->healthy();
        ok = ok && selectSlot(1) == open::SlotSelect::kLoaded && !dirty && fileValues("0") == editedV;        // the retry saved the edits, then switched
        check(ok, "B4: with the card failing the edited slot stays open and dirty, after the failure clears the switch saves it first");
        const auto keep = mw->files["0-1.patch"]; mw->failWrite = true;
        ok = !ws->savePatch("0-1", *base) && mw->files["0-1.patch"] == keep && !ws->savePatch("newname", *base) && !mw->files.count("newname.patch");
        mw->healthy();
        check(ok, "B4: a library or Workspace save that fails keeps the old file and makes no new one");
      }
      // B5: the bank copy replaces a file only with a complete copy, deletes nothing before every copy worked, and fails loudly
      // N2: and removes a destination file only when the card said the source has none, never on a failed lookup
      {
        MemBankOps::Files S, D;
        auto reset = [&]() { S.clear(); D.clear();
          S["0.patch"] = {1}; S["0-1.patch"] = {2}; S["0-2.patch"] = {3}; S["0.names"] = {9};
          D["0.patch"] = {10}; D["0-1.patch"] = {11}; D["0-2.patch"] = {12}; D["0-5.patch"] = {15}; D["0-b.patch"] = {16}; D["0.names"] = {19}; };
        reset(); MemBankOps ops{ &S, &D, "0-2.patch" };
        bool ok = !open::copyPartBank(ops, 0, "S", "D", true) && D["0-2.patch"] == std::vector<uint8_t>{12} && D.count("0-5.patch") && D.count("0-b.patch") && ops.removed == 0;   // a failed copy: no file lost
        reset(); ops.failCopy = ""; ops.failRemove = true;
        ok = ok && !open::copyPartBank(ops, 0, "S", "D", true) && D["0-1.patch"] == std::vector<uint8_t>{2};                           // a remove that fails is reported
        reset(); ops.failRemove = false;
        ok = ok && open::copyPartBank(ops, 0, "S", "D", true) && D["0-2.patch"] == std::vector<uint8_t>{3} && !D.count("0-5.patch") && !D.count("0-b.patch") && D["0.names"] == std::vector<uint8_t>{9};
        reset(); S.erase("0.patch"); S.erase("0.names");
        ok = ok && open::copyPartBank(ops, 0, "S", "D", true) && D["0.patch"] == std::vector<uint8_t>{10} && !D.count("0.names");       // slot 0 is never removed, the names file is
        check(ok, "B5: a failed bank copy reports it and deletes nothing, a good one replaces the files and prunes the slots the source lacks");
        // N2: a source file that exists but whose lookup fails is not "absent": nothing is pruned, the saved destination file stays, the copy reports it
        reset(); S["0-7.patch"] = {7}; D["0-7.patch"] = {77}; ops.removed = 0; ops.statErr = { "S\\0-7.patch" };
        ok = !open::copyPartBank(ops, 0, "S", "D", true) && D["0-7.patch"] == std::vector<uint8_t>{77} && D.count("0-5.patch") && D.count("0-b.patch") && ops.removed == 0;
        reset(); ops.statErr = { "S\\0-5.patch" };                                  // the lookup of a slot the source does not have fails: not proof either
        ok = ok && !open::copyPartBank(ops, 0, "S", "D", true) && D.count("0-5.patch") && D.count("0-b.patch") && ops.removed == 0;
        reset(); ops.statErr = { "S\\0-5.patch.bak" };                              // so does the lookup of its .bak
        ok = ok && !open::copyPartBank(ops, 0, "S", "D", true) && D.count("0-5.patch") && ops.removed == 0;
        reset(); ops.statErr = { "D\\0-5.patch" };                                  // the destination's state unknown: not removed, not reported as pruned
        ok = ok && !open::copyPartBank(ops, 0, "S", "D", true) && D.count("0-5.patch");
        reset(); ops.statErr.clear();
        check(ok, "N2: a failed lookup of a source file, its .bak or the destination file prunes nothing and fails the copy");
        // N1: a source file that an interrupted replace left at <file>.bak is copied from there, not taken for absent and pruned
        S["0-5.patch.bak"] = {55}; S["0-2.patch.bak"] = {33}; S.erase("0-2.patch"); D["0-6.patch.bak"] = {66}; D["0-6.patch"] = {6};
        ok = open::copyPartBank(ops, 0, "S", "D", true) && D["0-5.patch"] == std::vector<uint8_t>{55} && D["0-2.patch"] == std::vector<uint8_t>{33}
             && !D.count("0-6.patch") && !D.count("0-6.patch.bak") && !D.count("0-b.patch");   // a pruned slot takes its .bak with it, the others are not pruned
        check(ok, "N1: the bank copy takes a file from <file>.bak when the canonical one is gone, and prunes a slot together with its .bak");
      }
      // R6: the project save copy (Workspace -> Projects, prune false) deletes nothing in Projects, whatever the Workspace lacks
      {
        MemBankOps::Files W, P;   // "S" the Workspace, "D" Projects
        W["0.patch"] = {1}; W["0-1.patch"] = {2};   // the Workspace lacks slot 5, the backup, the names and slot 1's neighbour
        P["0.patch"] = {10}; P["0-1.patch"] = {11}; P["0-5.patch"] = {15}; P["0-5.patch.bak"] = {16}; P["0-b.patch"] = {17}; P["0.names"] = {19}; P["0-7.patch.bak"] = {77};
        const MemBankOps::Files before = P;
        MemBankOps ops{ &W, &P, "" };
        bool ok = open::copyPartBank(ops, 0, "S", "D", false) && ops.removed == 0;
        ok = ok && P["0.patch"] == std::vector<uint8_t>{1} && P["0-1.patch"] == std::vector<uint8_t>{2};   // what the Workspace has is copied
        P["0.patch"] = before.at("0.patch"); P["0-1.patch"] = before.at("0-1.patch");
        ok = ok && P == before;                                                       // every other file and .bak of Projects is as it was
        W.clear(); W["0.patch"] = {1};                                                // a Workspace with one file: all the slot files of Projects stay
        ok = ok && open::copyPartBank(ops, 0, "S", "D", false) && ops.removed == 0 && P.size() == before.size();
        ops.statErr = { "S\\0-5.patch" };                                              // a source lookup error still fails the copy and deletes nothing
        ok = ok && !open::copyPartBank(ops, 0, "S", "D", false) && ops.removed == 0 && P.size() == before.size();
        ops.statErr.clear(); ops.failRemove = true;                                   // and the save never calls remove at all
        ok = ok && open::copyPartBank(ops, 0, "S", "D", false) && ops.removed == 0;
        check(ok, "R6: a project save copy only copies the Workspace files that exist and deletes nothing in Projects");
        // the project open copy (prune true) clears the stale files of the previous project in the Workspace
        MemBankOps::Files O;                                                          // "S" Projects (the project has slot 0 only), "D" the Workspace
        O["0.patch"] = {1};
        MemBankOps::Files Ws; Ws["0.patch"] = {9}; Ws["0-5.patch"] = {5}; Ws["0-5.patch.bak"] = {6}; Ws["0.names"] = {7};
        MemBankOps oops{ &O, &Ws, "" };
        check(open::copyPartBank(oops, 0, "S", "D", true) && Ws["0.patch"] == std::vector<uint8_t>{1} && !Ws.count("0-5.patch") && !Ws.count("0-5.patch.bak") && !Ws.count("0.names"),
              "R6: a project open copy removes the slot files of the previous project from the Workspace");
      }
      // R3-B1: project open (ProjectOpen::openProjectFinish): a bank copy that fails twice loads the requested project like stock, it never
      // falls into the new empty project (that deletes the copied Workspace), and the copy gets exactly one retry
      {
        auto flow = [](int copyOk, bool workspaceOpens, int& copies, int& created, int& opens, int& settled) {
          copies = created = opens = settled = 0;
          open::openProjectAfterBankCopy([&]() { return ++copies > copyOk; },       // the first `copyOk` calls fail
                                         [&]() { settled++; },
                                         [&]() { opens++; return workspaceOpens || created > 0; },
                                         [&]() { created++; });
        };
        int copies, created, opens, settled;
        flow(2, true, copies, created, opens, settled);                                            // fails twice: loads the requested project
        bool ok = copies == 2 && created == 0 && opens == 1 && settled == 1;
        flow(1, true, copies, created, opens, settled);                                            // a hiccup, the retry works
        ok = ok && copies == 2 && created == 0 && opens == 1;
        flow(0, true, copies, created, opens, settled);                                            // no error: one copy only
        ok = ok && copies == 1 && created == 0 && opens == 1;
        flow(2, false, copies, created, opens, settled);                                           // a Workspace that cannot be opened keeps the stock fallback
        ok = ok && created == 1 && opens == 2;
        check(ok, "R3-B1: a bank copy that fails twice still loads the requested project, the empty project stays for a Workspace that cannot open");
      }
      // R3-B3: the destination paths of the FileManager go up to PATCH_SIZE 255 (254 characters): replace and recovery work for them,
      // the old 128 B buffer refused every path of 124 characters or more
      {
        bool ok = true;
        for (size_t len : { size_t(123), size_t(124), size_t(160), size_t(250), size_t(254) }) {
          std::string to = std::string(len - 11, 'd') + "\\0-15.patch";                    // len characters, ends like a slot file
          ok = ok && to.size() == len;
          RenameOps card; card.f[to] = "old"; card.f[to + ".tmp"] = "new";
          ok = ok && open::replaceFileOps(card, (to + ".tmp").c_str(), to.c_str()) && card.f.size() == 1 && card.f[to] == "new";   // replaced, no .bak left
          RenameOps cut; cut.f[to + ".bak"] = "old";                                                                                // an interrupted replace
          ok = ok && open::recoverBackupOps(cut, to.c_str()) == open::Presence::kPresent && cut.f.size() == 1 && cut.f[to] == "old";
        }
        std::string over(300, 'x');                                                            // above anything the FileManager makes: refused, nothing touched
        RenameOps card; card.f[over] = "old"; card.f[over + ".tmp"] = "new";
        ok = ok && !open::replaceFileOps(card, (over + ".tmp").c_str(), over.c_str()) && card.f[over] == "old" && card.f.size() == 2;
        check(ok, "R3-B3: replace and recovery work for destination paths up to 254 characters");
      }
      // N1: replaceFileOps over a card with FatFs rename rules, any rename failing and the power going at any step. The old file is never lost:
      // after the reader's recovery the file is the old one or the new one, and a replace that said true leaves the new one
      {
        bool ok = true; int stranded = 0, scenarios = 0;
        for (int mask = 0; mask < 8; mask++) for (int power = -1; power < 8; power++) {
          RenameOps card; card.f["D\\0.patch"] = "old"; card.f["D\\0.patch.tmp"] = "new"; card.failMask = mask; card.powerAfter = power;
          const bool done = open::replaceFileOps(card, "D\\0.patch.tmp", "D\\0.patch");
          if (!card.f.count("D\\0.patch")) stranded++;                  // the state the loaders used to leave alone
          card.failMask = 0; card.powerAfter = -1; card.dead = false;     // the next start, a healthy card
          const auto rec = open::recoverBackupOps(card, "D\\0.patch");
          const std::string now = card.f.count("D\\0.patch") ? card.f["D\\0.patch"] : std::string("MISSING");
          ok = ok && rec == open::Presence::kPresent && (now == "old" || now == "new") && (!done || now == "new");
          if (done && power < 0) ok = ok && card.f.size() == 1;           // no .bak left behind after a clean replace
          scenarios++;
        }
        ok = ok && stranded > 0 && scenarios == 72;
        RenameOps card; card.f["D\\0.patch.bak"] = "old"; card.f["D\\0.patch.tmp"] = "half";                 // power lost between the two renames: .bak and a leftover .tmp
        ok = ok && open::recoverBackupOps(card, "D\\0.patch") == open::Presence::kPresent && card.f["D\\0.patch"] == "old" && card.f.count("D\\0.patch.tmp"); // .tmp is never promoted
        RenameOps lost; ok = ok && open::recoverBackupOps(lost, "D\\0.patch") == open::Presence::kAbsent;   // nothing there, nothing to restore
        RenameOps both; both.f["D\\0.patch"] = "new"; both.f["D\\0.patch.bak"] = "old"; ok = ok && open::recoverBackupOps(both, "D\\0.patch") == open::Presence::kPresent && both.f["D\\0.patch"] == "new";   // a present file is never replaced
        RenameOps first; first.f["D\\0.patch.tmp"] = "new";                                                  // no old file: the new one is installed, an orphan .bak goes
        first.f["D\\0.patch.bak"] = "older"; ok = ok && open::replaceFileOps(first, "D\\0.patch.tmp", "D\\0.patch") && first.f["D\\0.patch"] == "new" && !first.f.count("D\\0.patch.bak");
        RenameOps unk; unk.f["D\\0.patch"] = "old"; unk.f["D\\0.patch.tmp"] = "new"; unk.powerAfter = 0;      // the card cannot say whether there is an old file: nothing is touched
        ok = ok && !open::replaceFileOps(unk, "D\\0.patch.tmp", "D\\0.patch") && unk.f["D\\0.patch"] == "old" && unk.f["D\\0.patch.tmp"] == "new";
        check(ok, "N1: a replace that fails or loses power at any step leaves the old or the new file after the reader's recovery, never none");
      }
      // N1: the readers (patch, names, backup, slot select, bank) move <file>.bak back before they take a file for missing, and nothing falls back to Init
      {
        auto live = setup();
        auto strand = [&](const char* name) { const std::string f = std::string(name); mw->files[f + ".bak"] = mw->files[f]; mw->files.erase(f); mw->files[f + ".tmp"] = {1, 2, 3}; };
        strand("0.patch");
        auto probe = ws->createPatch(T);
        bool ok = ws->loadPatch("0", *probe, true) && raw(*probe) == baseV && mw->files.count("0.patch") && !mw->files.count("0.patch.bak");   // slot 0 loads the old file, not Init
        strand("0-1.patch");
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kLoaded && raw(*live) == loV && mw->files.count("0-1.patch");   // a slot select, not an empty copy
        strand("0-3.patch"); open::patchBankPreload(0, *live, *ws); ok = ok && open::patchBankApply(0, 3) && raw(*live) == hiV;                // the bank counts the slot
        open::SlotNames nm; std::snprintf(nm.name[2], sizeof(nm.name[2]), "Pad"); ok = ok && open::patchNamesWrite(0, *ws, nm);
        strand("0.names"); open::SlotNames back; ok = ok && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[2], "Pad") == 0 && mw->files.count("0.names");
        mw->files["0-b.patch"] = mw->files["0-3.patch"]; strand("0-b.patch");   // a backup of this engine at <P>-b.patch.bak
        { auto l2 = ws->createPatch(T); open::patchBankSetLive(0, l2.get()); ok = ok && open::patchBackupRestore(0, *l2, *ws) && raw(*l2) == hiV && fileValues("0") == hiV; open::patchBankSetLive(0, live.get()); }
        // the card cannot say, or the move back fails: an error, never "missing"
        live = setup(); strand("0-1.patch"); mw->statError = { "0-1.patch.bak" };
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kError && open::patchBankCurrent(0) == 0;
        mw->healthy(); mw->failReplace = true;                                                                     // the move back fails
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kError && !mw->files.count("0-1.patch") && mw->files.count("0-1.patch.bak");
        mw->healthy(); ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kLoaded && raw(*live) == loV;
        check(ok, "N1: every reader moves <file>.bak back when the canonical file is gone, a failed move back or lookup is an error, not an empty slot");
      }
      // S4: a file that is there but does not open is an error, not an empty slot (an edit of the "empty" copy would overwrite the real slot)
      {
        auto live = setup(); mw->failOpenRead = { "0-1.patch" };
        auto probe = ws->createPatch(T);
        bool ok = open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kError && open::patchBankCurrent(0) == 0 && !ws->loadPatch("0-1", *probe, true);
        mw->healthy(); mw->failOpenRead = { "0-2.patch" };                                    // slot 2 has no file: confirmed absent, a copy as before
        ok = ok && open::patchBankSelect(0, 2, *live, *ws) == open::SlotSelect::kCopied;
        mw->healthy(); open::SlotNames nm; std::snprintf(nm.name[1], sizeof(nm.name[1]), "Kept"); ok = ok && open::patchNamesWrite(0, *ws, nm);
        const auto names = mw->files["0.names"]; open::SlotNames back;
        mw->failOpenRead = { "0.names" };                                                     // the names file does not open: unread, not empty
        ok = ok && !open::patchNamesRead(0, *ws, back) && !open::patchNamesWrite(0, *ws, open::SlotNames()) && mw->files["0.names"] == names;
        mw->healthy(); ok = ok && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Kept") == 0;
        check(ok, "S4: an existing file that fails to open is an error for the slot select and the names (nothing is overwritten), a missing one is an empty slot");
      }
      // N3: the engine change stops when the backup of slot 0 failed, and goes on when there was nothing to keep. N4: a failed save keeps the part dirty
      {
        auto live = setup();                                                                  // slot 0 = base (not an Init): worth a backup
        const auto kept = mw->files["0.patch"];
        mw->failReplace = true;
        const auto fail = open::patchBackupSave(0, *live, *ws);                              // the backup cannot be written
        mw->healthy(); mw->readFailAt = 20;
        const auto readFail = open::patchBackupSave(0, *live, *ws);                          // slot 0 cannot be read
        mw->healthy(); mw->failOpenRead = { "0.patch" };
        const auto openFail = open::patchBackupSave(0, *live, *ws);
        mw->healthy();
        bool ok = fail == open::Backup::kFailed && readFail == open::Backup::kFailed && openFail == open::Backup::kFailed && !open::backupAllowsEngineChange(fail)
                  && !open::backupAllowsEngineChange(readFail) && !open::backupAllowsEngineChange(openFail) && !mw->files.count("0-b.patch") && mw->files["0.patch"] == kept;
        const auto saved = open::patchBackupSave(0, *live, *ws);
        ok = ok && saved == open::Backup::kSaved && open::backupAllowsEngineChange(saved) && mw->files["0-b.patch"] == kept;
        mw->files.erase("0.patch");                                                          // no slot 0 file at all: nothing to keep, the change goes on
        const auto none = open::patchBackupSave(0, *live, *ws);
        mw->files["0.patch"] = {0xFF, 0xFF, 0xFF};                                           // a file that is no patch: nothing worth keeping either
        const auto junk = open::patchBackupSave(0, *live, *ws);
        ok = ok && none == open::Backup::kSkipped && junk == open::Backup::kSkipped && open::backupAllowsEngineChange(none) && open::backupAllowsEngineChange(junk);
        ws->savePatch("0", *init);                                                           // an untouched Init is skipped
        ok = ok && open::patchBackupSave(0, *live, *ws) == open::Backup::kSkipped && mw->files["0-b.patch"] == kept;
        check(ok, "N3: a backup that cannot be written or read ends in kFailed and stops the engine change, no file or an Init is a skip");
        // the engine change of the Model: a failed backup leaves the engine, the live patch and slot 0's file alone
        live = setup(); mw->failReplace = true;
        bool dirty = false; const auto before = mw->files["0.patch"];
        auto changeEngine = [&]() { const auto bk = open::patchBackupSave(0, *live, *ws); return open::backupAllowsEngineChange(bk); };   // Synth::backupSlotZero's verdict
        ok = !changeEngine() && mw->files["0.patch"] == before && raw(*live) == baseV;
        mw->healthy(); ok = ok && changeEngine() && mw->files["0-b.patch"] == before;
        check(ok, "N3: the engine change is refused while the backup fails and goes through when it works");
        // N4: accept a library patch on a clean slot, its Workspace save fails: the part is dirty, a slot select does not discard it, a retry saves it
        live = setup(); dirty = false;
        live->setParamValue(0, edit0, Patch::ParamChangeSource::kKnob);                      // the accepted library patch (not dirty: loading never sets it)
        auto saveOpen = [&]() { const bool s = open::patchSaveOpenSlot(0, *live, *ws); open::settleDirtyAfterSave(s, dirty); return s; };   // Synth::savePatchToWorkspace
        auto selectSlot = [&](int slot) { if (dirty && !saveOpen()) return open::SlotSelect::kSaveFailed; return open::patchBankSelect(0, slot, *live, *ws); };   // Synth::selectPatchSlot
        mw->failWrite = true; ok = !saveOpen() && dirty;                                    // the accept's save fails
        ok = ok && selectSlot(1) == open::SlotSelect::kSaveFailed && open::patchBankCurrent(0) == 0 && raw(*live) == editedV;   // the slot select does not discard it
        mw->healthy(); ok = ok && saveOpen() && !dirty && fileValues("0") == editedV;
        dirty = true; mw->failReplace = true; ok = ok && !saveOpen() && dirty; mw->healthy(); ok = ok && saveOpen() && !dirty;   // a retry after another failure
        check(ok, "N4: a failed save keeps the part dirty on every path, also for a clean slot, so a slot select retries the save instead of discarding the patch");
      }
      // S6: the data of a new patch comes from the engine definition, same bytes as a fresh OpenPatch gives, for every engine of the build
      {
        bool ok = true;
        for (const auto& kd : kinds) {
          auto p = ws->createPatch(int(kd.id)); auto& op = static_cast<open::OpenPatch&>(*p);
          auto a = std::make_unique<open::PatchData>(), b = std::make_unique<open::PatchData>();
          op.exportData(*a); open::initPatchData(*op.getDef(), *b);
          std::vector<uint8_t> ea(open::kPatchFileMax), eb(open::kPatchFileMax);
          const size_t na = open::encodePatch(*a, ea.data(), ea.size()), nb = open::encodePatch(*b, eb.data(), eb.size());
          const bool same = na > 0 && na == nb && std::memcmp(ea.data(), eb.data(), na) == 0;
          if (!same) printf("  S6: %s init data differs (%zu vs %zu bytes)\n", kd.name, na, nb);
          ok = ok && same;
        }
        check(ok, "S6: initPatchData (no OpenPatch, no heap) encodes to the same bytes as a new patch of every engine");
      }
      // S1: a card read error is not the end of the file
      {
        auto live = setup(); auto probe = ws->createPatch(T);
        const auto file = mw->files["0-1.patch"]; open::PatchData pd; size_t cut = 0;
        for (size_t k = 1; k < file.size() && !cut; k++) if (open::decodePatch(file.data(), k, pd) && open::typeForEngineId(pd.engine) == T) cut = k;   // a prefix that would pass as a whole patch
        mw->readFailAt = int(cut);
        bool ok = cut > 0 && !ws->loadPatch("0-1", *probe, true) && raw(*probe) == initV;
        ok = ok && open::patchBankSelect(0, 1, *live, *ws) == open::SlotSelect::kError && open::patchBankCurrent(0) == 0 && raw(*live) == baseV;
        mw->healthy();
        open::SlotNames nm; std::snprintf(nm.name[1], sizeof(nm.name[1]), "Lead one"); ok = ok && open::patchNamesWrite(0, *ws, nm);
        const auto names = mw->files["0.names"]; open::SlotNames back;
        mw->readFailAt = 4;
        ok = ok && !open::patchNamesRead(0, *ws, back) && !open::patchNamesWrite(0, *ws, open::SlotNames()) && mw->files["0.names"] == names;   // the unread names are not overwritten by empty ones
        mw->healthy();
        ok = ok && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Lead one") == 0 && open::patchNamesWrite(0, *ws, back);
        check(ok, "S1: a read error ends a patch or names read as an error, no prefix is taken for the file and empty names do not replace unread ones");
      }
      // R3-B2: one card error while the project load reads the names must not fail every later project save. The save reads the names again
      // (Synth::persistSlotNames), keeps the names on the card, merges the edits made since, and when the file still cannot be read it
      // leaves the file alone and does not fail the save
      {
        mw->healthy();
        open::SlotNames nm; std::snprintf(nm.name[1], sizeof(nm.name[1]), "Lead one"); std::snprintf(nm.name[3], sizeof(nm.name[3]), "Pad");
        bool ok = open::patchNamesWrite(0, *ws, nm);
        const auto names = mw->files["0.names"]; open::SlotNames back;
        mw->failOpenRead = { "0.names" };                                                       // the load: the names file does not open once
        ok = ok && !open::patchNamesRead(0, *ws, back);
        open::SlotNames mem; std::snprintf(mem.name[2], sizeof(mem.name[2]), "Mine");           // what the part has in memory: all empty but an edit
        ok = ok && open::patchNamesPersist(0, *ws, mem) && mw->files["0.names"] == names && !mem.name[1][0];   // still failing: saved, file untouched
        mw->healthy();                                                                          // the card recovers
        ok = ok && open::patchNamesPersist(0, *ws, mem) && std::strcmp(mem.name[1], "Lead one") == 0;          // the save succeeds, merged set handed back
        ok = ok && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Lead one") == 0 && std::strcmp(back.name[2], "Mine") == 0 && std::strcmp(back.name[3], "Pad") == 0;
        mw->failOpenRead = { "0.names" };                                                       // a second hiccup, edit of a slot the card also names
        ok = ok && !open::patchNamesRead(0, *ws, back);
        open::SlotNames mem2; std::snprintf(mem2.name[1], sizeof(mem2.name[1]), "Edited");
        mw->healthy();
        ok = ok && open::patchNamesPersist(0, *ws, mem2) && open::patchNamesRead(0, *ws, back) && std::strcmp(back.name[1], "Edited") == 0 && std::strcmp(back.name[2], "Mine") == 0 && std::strcmp(back.name[3], "Pad") == 0;   // the memory wins, the rest stays
        check(ok, "R3-B2: a names file that was unreadable at load is read again at save: names kept, edits merged, an unreadable file does not fail the save");
      }
      check(mw->openNow == 0, "review blockers: every file opened is closed again");
      open::patchBankSetLive(0, nullptr);
      mw->files.clear();
    }
#ifdef OT_ORACLE
    // OpenTracker's own codec as oracle: its Init patches load here and our files decode and re-encode there byte for byte
    bool oraOk = true; int engIds[4] = { ot::kSynthEngineAcd, ot::kSynthEngineFat, ot::kSynthEngineVap, ot::kSynthEngineWtfm };
    for (int e = 0; e < 4; e++) {
      ot::SynthPatch sp; ot::patchInit(engIds[e], &sp); std::vector<uint8_t> ref(8192); size_t n = 0; ot::encodePatch(sp, ref.data(), ref.size(), &n); ref.resize(n);
      mp->files[std::string(kinds[e].name) + "/ref.patch"] = ref;
      auto p = ps->createPatch(int(kinds[e].id)); const bool l = ps->loadPatch("ref", *p, true);
      ps->savePatch("ref2", *p); const auto& mine = mp->files[std::string(kinds[e].name) + "/ref2.patch"];
      ot::SynthPatch back; const bool dec = ot::decodePatch(mine.data(), mine.size(), &back) == ot::PatchStatus::kOk;
      std::vector<uint8_t> re(8192); size_t rn = 0; if (dec) ot::encodePatch(back, re.data(), re.size(), &rn); re.resize(rn);
      printf("  oracle %-5s ref %zu bytes, ours %zu bytes: %s, OT re-encodes ours %s\n", kinds[e].name, ref.size(), mine.size(), mine == ref ? "identical" : "DIFFERENT", re == mine ? "identically" : "DIFFERENTLY");
      oraOk = oraOk && l && dec && re == mine && mine == ref;
    }
    check(oraOk, "OpenTracker oracle: its Init patches round-trip through ours byte for byte");
#endif
  }
#ifdef SYNTH_ENGINE_ACD
  // ---- pitch bend (MIDI kPitchBendChange, also what the Micro-tune FX sends): +-1 semitone at +-8191/-8192
  {
    auto s5 = Synth::createSynth([](size_t){return nullptr;});
    auto st5 = s5->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto p5 = std::shared_ptr<Patch>(st5->createPatch(int(InstrumentTypeId::kSimpleAnalogSynth)));
    for (auto [id, v] : { std::pair{17, 1.0f}, {14, 0.0f}, {9, 20000.0f} }) p5->setParamValue(id, v, Patch::ParamChangeSource::kKnob);   // steady: sustain 1, no filter env
    Src src5; auto i5 = s5->createInstrument(0); i5->assignPatch(p5); i5->connect(src5); i5->setVoiceCount(1); i5->setMidiChannel(0);
    std::shared_ptr<Instrument> a5[1] = { i5 };
    s5->setup(std::span<std::shared_ptr<Instrument>>(a5,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto bend = [&](int v) { MidiEvent ev{}; ev.type = MidiEvent::Type::kPitchBendChange; ev.channel = 0; ev.data.pitchBendChange.value = int16_t(v); (*src5.cb.lock())(ev, 0); };
    auto crossings = [&](int blocks) { int zc = 0; float prev = 0; for (int b = 0; b < blocks; b++) { s5->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); for (float v : L) { if (v > 0 && prev <= 0) zc++; prev = v; } } return zc; };
    src5.send(MidiEvent::Type::kNoteOn, 0, 57, 100); crossings(10);
    const int base = crossings(200);
    bend(8191); crossings(2); const int up = crossings(200);
    bend(-8192); crossings(2); const int down = crossings(200);
    bend(0); crossings(2); const int back = crossings(200);
    printf("  pitch bend: rising crossings per 200 blocks base=%d up=%d down=%d back=%d (expect x1.059 and x0.944)\n", base, up, down, back);
    check(std::fabs(float(up) / base - 1.0595f) < 0.02f, "bend +8191 raises a held note by about one semitone");
    check(std::fabs(float(down) / base - 0.9439f) < 0.02f, "bend -8192 lowers it by about one semitone");
    check(std::abs(back - base) <= 2, "bend 0 returns to the note pitch");
    bend(8191); src5.send(MidiEvent::Type::kStop, 0, 0, 0); src5.send(MidiEvent::Type::kNoteOn, 0, 57, 100); crossings(10);
    check(std::abs(crossings(200) - base) <= 2, "transport Stop resets the bend");
  }
#endif
#ifdef SYNTH_ENGINE_CHIP
  // ---- CHIP: pulse model, steady tone, bend and glide. The hosted voice must follow the shim's bend and report silence
  {
    auto s6 = Synth::createSynth([](size_t){return nullptr;});
    auto st6 = s6->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto p6 = std::shared_ptr<Patch>(st6->createPatch(int(InstrumentTypeId::kChiptuneSynth)));
    // Model 2A03 pulse, no PWM, no sub, open filter, no crush, sustain 1
    for (auto [id, v] : { std::pair{0, 0.0f}, {5, 0.0f}, {6, 0.0f}, {17, 18000.0f}, {19, 0.0f}, {20, 16.0f}, {15, 1.0f} })
      p6->setParamValue(id, v, Patch::ParamChangeSource::kKnob);
    Src src6; auto i6 = s6->createInstrument(0); i6->assignPatch(p6); i6->connect(src6); i6->setVoiceCount(1); i6->setMidiChannel(0);
    std::shared_ptr<Instrument> a6[1] = { i6 };
    s6->setup(std::span<std::shared_ptr<Instrument>>(a6,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto bend = [&](int v) { MidiEvent ev{}; ev.type = MidiEvent::Type::kPitchBendChange; ev.channel = 0; ev.data.pitchBendChange.value = int16_t(v); (*src6.cb.lock())(ev, 0); };
    auto crossings = [&](int blocks) { int zc = 0; float prev = 0; for (int b = 0; b < blocks; b++) { s6->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); for (float v : L) { if (v > 0 && prev <= 0) zc++; prev = v; } } return zc; };
    src6.send(MidiEvent::Type::kNoteOn, 0, 57, 100); crossings(10);
    const int base = crossings(200);
    bend(8191); crossings(2); const int up = crossings(200);
    bend(0); crossings(2); const int back = crossings(200);
    printf("  CHIP pulse: rising crossings per 200 blocks base=%d up=%d back=%d (expect x1.059)\n", base, up, back);
    check(base > 100, "CHIP pulse at A3 sounds with the expected cycle count");
    check(std::fabs(float(up) / base - 1.0595f) < 0.03f, "CHIP follows the shim's pitch bend (+1 semitone)");
    check(std::abs(back - base) <= 2, "CHIP bend 0 returns to the note pitch");
    src6.send(MidiEvent::Type::kNoteOff, 0, 57, 0);
    float rel = 1; for (int b = 0; b < 400; b++) { s6->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); rel = peakOf(L); }
    check(rel < 1e-4f, "CHIP releases to silence after note off");
    // two voices on one slot, then steal: each voice object plays its own note
    i6->setVoiceCount(2);
    src6.send(MidiEvent::Type::kNoteOn, 0, 57, 100); src6.send(MidiEvent::Type::kNoteOn, 0, 64, 100); src6.send(MidiEvent::Type::kNoteOn, 0, 69, 100);
    float pk2 = 0; for (int b = 0; b < 40; b++) { s6->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); pk2 = std::max(pk2, peakOf(L)); }
    check(std::isfinite(pk2) && pk2 > 1e-3f, "CHIP: three notes on a 2 voice slot (one steal) stay finite and audible");
  }
#endif
#ifdef SYNTH_ENGINE_JU6
  // ---- JU6: the chorus (slotPost) runs on the slot's own bus: stereo when on, mono when off, with a tail after note off
  {
    auto s7 = Synth::createSynth([](size_t){return nullptr;});
    auto st7 = s7->createPatchStore(Synth::FileSystemId::kPatches, true);
    Src src7[2]; std::shared_ptr<Patch> p7[2]; std::shared_ptr<Instrument> i7[2];
    for (int k = 0; k < 2; k++) {   // slot 0 = JU6 with chorus II, slot 1 = JU6 chorus off: no bleed between slots
      p7[k] = std::shared_ptr<Patch>(st7->createPatch(0x81));
      p7[k]->setParamValue(22, k == 0 ? 2.0f : 0.0f, Patch::ParamChangeSource::kKnob);
      i7[k] = s7->createInstrument(k); i7[k]->assignPatch(p7[k]); i7[k]->connect(src7[k]); i7[k]->setVoiceCount(1); i7[k]->setMidiChannel(0);
    }
    std::shared_ptr<Instrument> a7[2] = { i7[0], i7[1] };
    s7->setup(std::span<std::shared_ptr<Instrument>>(a7,2), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto diff = [&](int slot, int blocks, float& pk) { double d = 0; pk = 0; for (int b = 0; b < blocks; b++) { s7->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), slot); /* mono mix in this call */ } return d; };
    (void)diff;
    // render both slots through the main (summing) path to look at L and R separately
    auto mainPath = [&](int blocks, double& dLR, float& pk) { dLR = 0; pk = 0; for (int b = 0; b < blocks; b++) { s7->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}); for (int i = 0; i < 128; i++) { dLR += std::fabs(L[i] - R[i]); pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); } } };
    double dLR; float pk;
    src7[1].send(MidiEvent::Type::kNoteOn, 0, 57, 100);   // chorus off only
    mainPath(120, dLR, pk);
    check(pk > 1e-3f && dLR < 1e-6, "JU6 chorus off: sounds, L == R");
    src7[0].send(MidiEvent::Type::kNoteOn, 0, 57, 100);   // chorus II too
    mainPath(120, dLR, pk);
    check(std::isfinite(pk) && dLR > 1e-3, "JU6 chorus II on slot 0: the mix is stereo");
    src7[1].send(MidiEvent::Type::kNoteOff, 0, 57, 0); src7[0].send(MidiEvent::Type::kNoteOff, 0, 57, 0);
    mainPath(40, dLR, pk);
    float tailPk = 0; for (int b = 0; b < 3; b++) { mainPath(1, dLR, pk); tailPk = std::max(tailPk, pk); }
    mainPath(1200, dLR, pk);   // pk is the maximum over these blocks, so look at a few more afterwards
    mainPath(10, dLR, pk);
    printf("  JU6 after release: pk=%g\n", pk);
    for (int k = 0; k < 2; k++) { s7->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), k); printf("    slot %d alone: pk=%g\n", k, peakOf(L)); }
    check(std::isfinite(pk) && pk < 1e-4f, "JU6 released: voices and chorus tail end in silence");
  }
#endif
#ifdef SYNTH_ENGINE_PHZ
  // ---- PHZ: every model x character is finite and sounds, X and Y of the phase distortion change the spectrum, the
  // filter Off path and a filtered type both stay finite, Reset restarts the cycle
  {
    using namespace ot::audio::phz;
    auto s8 = Synth::createSynth([](size_t){return nullptr;});
    auto st8 = s8->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto p8 = std::shared_ptr<Patch>(st8->createPatch(0x82));
    Src src8; auto i8 = s8->createInstrument(0); i8->assignPatch(p8); i8->connect(src8); i8->setVoiceCount(1); i8->setMidiChannel(0);
    std::shared_ptr<Instrument> a8[1] = { i8 };
    s8->setup(std::span<std::shared_ptr<Instrument>>(a8,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto blk8 = [&]() { s8->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); return peakOf(L); };
    auto set = [&](int id, float v) { p8->setParamValue(id, v, Patch::ParamChangeSource::kKnob); };
    // steady, dry, one oscillator, filter Off
    set(kMix, 0.0f); set(kDetune, 0.0f); set(kAmpA, 0.0f); set(kAmpS, 1.0f); set(kCutoff, 20000.0f); set(kFilterEnvAmt, 0.0f);
    constexpr int kBlocks = 64;   // 8192 samples
    const double f0 = 220.0, fs = 44117.647;
    // Spectrum of the held note: harmonics 1..24 (Goertzel, Hann window). Returns false when not finite
    auto spectrum = [&](double* mag, float& peak) {
      src8.send(MidiEvent::Type::kNoteOff, 0, 57, 0); for (int b = 0; b < 300; b++) blk8();
      src8.send(MidiEvent::Type::kNoteOn, 0, 57, 100); for (int b = 0; b < 12; b++) blk8();
      static float x[kBlocks * 128]; peak = 0;
      for (int b = 0; b < kBlocks; b++) { blk8(); for (int i = 0; i < 128; i++) { x[b * 128 + i] = L[i]; peak = std::max(peak, std::fabs(L[i])); if (!std::isfinite(L[i])) peak = INFINITY; } }
      const int n = kBlocks * 128;
      for (int h = 1; h <= 24; h++) {
        double re = 0, im = 0; const double w = 2 * M_PI * h * f0 / fs;
        for (int i = 0; i < n; i++) { const double hann = 0.5 - 0.5 * std::cos(2 * M_PI * i / n); re += hann * x[i] * std::cos(w * i); im -= hann * x[i] * std::sin(w * i); }
        mag[h - 1] = std::sqrt(re * re + im * im) / n * 4;
      }
    };
    auto brightness = [](const double* m) { double hi = 0, all = 1e-12; for (int h = 0; h < 24; h++) { all += m[h] * m[h]; if (h >= 1) hi += m[h] * m[h]; } return hi / all; };
    auto dist = [](const double* a, const double* b) { double d = 0; for (int h = 0; h < 24; h++) d += (a[h] - b[h]) * (a[h] - b[h]); return std::sqrt(d); };
    double ref[24], m[24]; float pk;
    bool allOk = true;
    for (int model = 0; model <= 5; model++) {
      for (int ch : { 0, 1, 2, 3, 6, 8 }) {
        set(kModel, float(model)); set(kChar1, float(ch)); set(kX1, 0.7f); set(kY1, 0.4f); set(kShape1, 0.6f);
        spectrum(m, pk);
        const bool ok = std::isfinite(pk) && pk > 0.05f && pk < 1.5f;
        if (!ok) { printf("  PHZ model %d character %d: peak %g FAIL\n", model, ch, pk); allOk = false; }
      }
    }
    check(allOk, "PHZ: all 6 models x 6 characters finite, non-silent, peak below 1.5");
    // X changes the spectrum: OG Saw from sine (X 0) to saw (X 1)
    set(kModel, 0.0f); set(kChar1, 0.0f); set(kShape1, 0.0f); set(kY1, 0.5f);
    set(kX1, 0.0f); spectrum(ref, pk); const double bSine = brightness(ref);
    set(kX1, 0.9f); spectrum(m, pk); const double bSaw = brightness(m);
    printf("  PHZ OG Saw: brightness X=0 %.4f, X=0.9 %.4f, spectrum distance %.3f\n", bSine, bSaw, dist(ref, m));
    check(bSine < 0.01 && bSaw > 0.15 && dist(ref, m) > 0.1, "PHZ: X moves OG Saw from a sine to a bright saw");
    // Y changes the spectrum for the models it is meant for
    for (int model : { 0, 4 }) {
      set(kModel, float(model)); set(kX1, 0.35f);
      set(kY1, 0.5f); spectrum(ref, pk);
      set(kY1, 0.9f); spectrum(m, pk);
      printf("  PHZ model %d: Y 0.5 -> 0.9 spectrum distance %.3f\n", model, dist(ref, m));
      check(dist(ref, m) > 0.03, model == 0 ? "PHZ OG Saw: Y changes the spectrum" : "PHZ Linear: Y changes the spectrum");
    }
    // Shape moves along the Character row
    set(kModel, 0.0f); set(kX1, 0.5f); set(kY1, 0.5f); set(kChar1, 6.0f);
    set(kShape1, 0.0f); spectrum(ref, pk); set(kShape1, 1.0f); spectrum(m, pk);
    check(dist(ref, m) > 0.05, "PHZ: Shape changes the spectrum (Add 5)");
    // Filtered type and Reset: finite, quieter than the unfiltered note, and two notes with Reset On are identical
    set(kShape1, 0.0f); set(kChar1, 0.0f); set(kX1, 0.8f); set(kCutoff, 800.0f); set(kResonance, 0.6f); set(kFilterType, 1.0f);
    spectrum(m, pk); check(std::isfinite(pk) && pk > 0.01f, "PHZ: LP OB 24 dB at 800 Hz sounds and is finite");
    set(kCutoff, 20000.0f); set(kReset1, 1.0f); set(kAmpD, 0.0f);
    spectrum(ref, pk); spectrum(m, pk);
    check(dist(ref, m) < 1e-3, "PHZ: Reset On starts every note on the same phase");
    // Model and Character names reach the editor as text
    const auto& md8 = p8->getMetadata();
    check(md8.params[kModel].valueFormatter(*p8, 5.0f) == "Hermite FM" && md8.params[kChar2].valueFormatter(*p8, 8.0f) == "Add 11" && md8.params[kX1].valueFormatter(*p8, 0.25f) == "0.25",
          "PHZ: value text for Model, Character and XMod");
    src8.send(MidiEvent::Type::kNoteOff, 0, 57, 0);
  }
#endif
  // ---- ids: the canonical stored numbers and the .patch engine numbers of the picked engines are unique and map back
  {
    struct Row { const char* name; int stored; int type; int patchEngine; } rows[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, PATCH_ENGINE, ...) {#NAME, STORED, int(TYPE_ID), PATCH_ENGINE},
#include "engine_list.h"
#undef SYNTH_ENGINE
    };
    bool uniq = true, back = true;
    for (size_t a = 0; a < sizeof(rows) / sizeof(rows[0]); a++) {
      back = back && open::engineIdForType(rows[a].type) == rows[a].patchEngine && open::typeForEngineId(rows[a].patchEngine) == rows[a].type;
      for (size_t b = a + 1; b < sizeof(rows) / sizeof(rows[0]); b++)
        uniq = uniq && rows[a].stored != rows[b].stored && rows[a].type != rows[b].type && rows[a].patchEngine != rows[b].patchEngine;
      printf("  %-5s stored %d, type id %d, .patch engine %d\n", rows[a].name, rows[a].stored, rows[a].type, rows[a].patchEngine);
    }
    check(uniq, "stored numbers, type ids and .patch engine numbers are unique");
    check(back, ".patch engine number <-> type id round trip for every picked engine");
  }
#ifdef SYNTH_ENGINE_PERC
  // ---- PERC: one hit per drum sounds and is finite, the kit overlaps inside one voice, velocity scales the hit, the
  // unmapped notes (61 63 68 70) are silent, everything dies out
  {
    auto s9 = Synth::createSynth([](size_t){return nullptr;});
    auto st9 = s9->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto p9 = std::shared_ptr<Patch>(st9->createPatch(int(InstrumentTypeId::kDrumSynth)));
    Src src9; auto i9 = s9->createInstrument(0); i9->assignPatch(p9); i9->connect(src9); i9->setVoiceCount(2); i9->setMidiChannel(0);
    std::shared_ptr<Instrument> a9[1] = { i9 };
    s9->setup(std::span<std::shared_ptr<Instrument>>(a9,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto blk9 = [&]() { s9->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); return peakOf(L); };
    check(i9->getVoiceCount() == 2 && p9->getMetadata().maxVoices == 1, "PERC: the patch metadata caps it at 1 voice");
    // Hit one note, return peak and energy of the next n blocks; wait until the kit is silent first
    auto hit = [&](int note, int vel, int n, double& rms) {
      src9.send(MidiEvent::Type::kStop, 0, 0, 0); for (int b = 0; b < 1500; b++) blk9();   // the drums of an earlier hit ring on after Stop
      src9.send(MidiEvent::Type::kNoteOn, 0, note, vel);
      float pk = 0; double en = 0; for (int b = 0; b < n; b++) { pk = std::max(pk, blk9()); for (int i = 0; i < 128; i++) en += double(L[i]) * L[i]; }
      rms = std::sqrt(en / (n * 128.0)); return pk;
    };
    const struct { const char* name; int note; } drums[] = { {"kick", 60}, {"snare", 62}, {"closed hat", 64}, {"open hat", 67}, {"cymbal", 69}, {"perc", 71}, {"tom", 48}, {"tom high", 84} };
    bool all = true;
    for (const auto& d : drums) {
      double r; const float pk = hit(d.note, 100, 40, r);
      printf("  PERC %-10s note %3d peak=%.4f rms=%.4f\n", d.name, d.note, pk, r);
      all = all && std::isfinite(pk) && pk > 1e-3f && pk < 4.0f;
    }
    check(all, "PERC: kick, snare, both hats, cymbal, perc and two toms sound, finite and below 4.0");
    bool silent = true; for (int note : {61, 63, 68, 70}) { double r; const float pk = hit(note, 100, 20, r); silent = silent && pk < 1e-4f; }
    check(silent, "PERC: notes 61 63 68 70 play nothing");
    double rHi, rLo; hit(60, 127, 20, rHi); hit(60, 40, 20, rLo);
    printf("  PERC kick rms vel 127 %.4f, vel 40 %.4f\n", rHi, rLo);
    check(rLo < 0.6 * rHi && rLo > 0.1 * rHi, "PERC: velocity scales the hit (vel 40 is clearly quieter than 127)");
    // overlap: kick, then snare 20 blocks later: the kick still rings under the snare (peak above the snare alone)
    double r; const float snareOnly = hit(62, 100, 20, r);
    src9.send(MidiEvent::Type::kStop, 0, 0, 0); for (int b = 0; b < 1500; b++) blk9();
    src9.send(MidiEvent::Type::kNoteOn, 0, 60, 100); for (int b = 0; b < 20; b++) blk9();
    src9.send(MidiEvent::Type::kNoteOn, 0, 62, 100); float both = 0; for (int b = 0; b < 20; b++) both = std::max(both, blk9());
    printf("  PERC snare alone %.4f, kick then snare %.4f\n", snareOnly, both);
    check(std::isfinite(both) && both > snareOnly * 0.9f, "PERC: a second drum does not cut the first (one voice, drums overlap)");
    // all notes at once and then release: finite, and the kit ends in silence
    for (int note : {60, 62, 64, 69, 71, 48}) src9.send(MidiEvent::Type::kNoteOn, 0, note, 127);
    float pkAll = 0; for (int b = 0; b < 100; b++) pkAll = std::max(pkAll, blk9());
    for (int note : {60, 62, 64, 69, 71, 48}) src9.send(MidiEvent::Type::kNoteOff, 0, note, 0);
    float tail = 0; for (int b = 0; b < 1800; b++) blk9(); for (int b = 0; b < 4; b++) tail = std::max(tail, blk9());
    printf("  PERC six drums at once peak=%.4f, after release+5 s peak=%.6f\n", pkAll, tail);
    check(std::isfinite(pkAll) && pkAll > 1e-3f && tail < 1e-4f, "PERC: six drums together stay finite and the kit dies out");
  }
#endif

  // ---- every picked engine: ids, parameter sweep, velocity, polyphony (and the T-Type note map)
  {
    struct Id { const char* name; int stored, typeId, patchId; } ids[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, PATCH_ENGINE, ...) {#NAME, STORED, int(TYPE_ID), PATCH_ENGINE},
#include "engine_list.h"
#undef SYNTH_ENGINE
    };
    const int nIds = int(sizeof(ids) / sizeof(ids[0]));
    bool uniq = true;
    for (int i = 0; i < nIds; i++) for (int j = i + 1; j < nIds; j++)
      if (ids[i].stored == ids[j].stored || ids[i].typeId == ids[j].typeId || ids[i].patchId == ids[j].patchId || !strcmp(ids[i].name, ids[j].name)) {
        printf("  clash: %s / %s\n", ids[i].name, ids[j].name); uniq = false; }
    check(uniq, "engine names, stored numbers, type ids and .patch ids are unique");
    // the numbers the poly-editor stores (patches saved by the editor load on the device): pinned
    static const struct { const char* name; int stored, patchId; } kPin[] = {
      {"ACD",0,1},{"FAT",1,2},{"VAP",2,4},{"WTFM",3,5},{"PERC",4,6},{"CHIP",6,8},{"JU6",7,0x81},{"PHZ",8,0x82},{"FM4",9,0x80},{"DTYPE",10,0xA2},
      {"CSTYPE",11,0x83},{"ATYPE",12,0x84},{"TTYPE",13,0x85},{"8TYPE",14,0x86},{"HANDTYPE",15,0x87},{"BTYPE",16,0x88},{"ARCTYPE",17,0x89} };
    bool pinned = true, roundTrip = true;
    for (const auto& id : ids) {
      bool found = false;
      for (const auto& pin : kPin) if (!strcmp(pin.name, id.name)) { found = true; pinned = pinned && pin.stored == id.stored && pin.patchId == id.patchId && (id.patchId < 0x80 || id.patchId == id.typeId); }
      pinned = pinned && found;
      roundTrip = roundTrip && open::engineIdForType(id.typeId) == id.patchId && open::typeForEngineId(id.patchId) == id.typeId;
    }
    check(pinned, "stored numbers, .patch ids and OpenTracker type ids match the poly-editor table (D-Type 0xA2, PHZ 0x82)");
    check(roundTrip, ".patch engine id <-> type id round trips for every engine");

    // One note on a fresh synth with the default patch, one parameter changed. out = 40 blocks of L then R.
    auto renderCase = [&](int typeId, int note, int vel, int pidx, float pval, std::vector<float>& out) {
      auto sx = Synth::createSynth([](size_t){return nullptr;});
      auto stx = sx->createPatchStore(Synth::FileSystemId::kPatches, true);
      auto px = std::shared_ptr<Patch>(stx->createPatch(typeId));
      if (pidx >= 0) px->setParamValue(pidx, pval, Patch::ParamChangeSource::kKnob);
      Src sr; auto ix = sx->createInstrument(0); ix->assignPatch(px); ix->connect(sr); ix->setVoiceCount(2); ix->setMidiChannel(0);
      std::shared_ptr<Instrument> ax[1] = { ix };
      sx->setup(std::span<std::shared_ptr<Instrument>>(ax,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
      std::array<float,128> l, r, a1, a2; std::array<AudioBufferRef,2> au{AudioBufferRef(a1),AudioBufferRef(a2)};
      sr.send(MidiEvent::Type::kNoteOn, 0, note, vel);
      out.clear(); float pk = 0;
      for (int b = 0; b < 40; b++) { sx->renderAudioBuffer({AudioBufferRef(l),AudioBufferRef(r)}, std::span<AudioBufferRef>(au), 0); pk = std::max(pk, peakOf(l)); out.insert(out.end(), l.begin(), l.end()); out.insert(out.end(), r.begin(), r.end()); }
      return pk;
    };
    auto energy = [](const std::vector<float>& v) { double e = 0; for (float x : v) e += double(x) * x; return std::sqrt(e / double(v.size())); };
    for (const auto& id : ids) {
      const open::EngineDef& d = *open::engineDefForType(id.typeId);
      const int note = (id.typeId == int(InstrumentTypeId::kDrumSynth) || id.typeId == 0x85) ? 60 : 57;
      std::vector<float> base, alt;
      const float pkBase = renderCase(id.typeId, note, 100, -1, 0, base);
      // a kit engine (PERC, T-Type) has one parameter group per drum: the sweep plays one note per drum there
      std::vector<int> sweepNotes = { note };
      if (d.maxVoices == 1) sweepNotes = { 60, 61, 62, 63, 64, 66, 68, 69, 70, 48, 52, 56 };
      std::vector<std::vector<float>> bases(sweepNotes.size());
      for (size_t k = 0; k < sweepNotes.size(); k++) renderCase(id.typeId, sweepNotes[k], 100, -1, 0, bases[k]);
      int changed = 0; std::string still;
      for (int i = 0; i < d.paramCount; i++) {
        const open::ParamDef& pd = d.params[i];
        const float v = pd.def < pd.max ? pd.max : pd.min;
        bool diff = false;
        for (size_t k = 0; k < sweepNotes.size() && !diff; k++) {
          const float pk = renderCase(id.typeId, sweepNotes[k], 100, i, v, alt);
          if (!std::isfinite(pk)) { printf("  %s param %d (%s) = %g: NOT FINITE\n", id.name, i, pd.name, v); bad++; diff = true; }
          for (size_t q = 0; q < alt.size() && !diff; q++) diff = std::fabs(bases[k][q] - alt[q]) > 1e-6f;
        }
        changed += diff; if (!diff) still += (still.empty() ? "" : ",") + std::to_string(i);
      }
      std::vector<float> lo, hi; const float pkLo = renderCase(id.typeId, note, 32, -1, 0, lo); const float pkHi = renderCase(id.typeId, note, 127, -1, 0, hi);
      const double eLo = energy(lo), eHi = energy(hi);
      // polyphony: 4 voice instrument: 2 notes -> min(2, maxVoices) voices, 4 notes -> min(4, maxVoices), a fifth steals
      auto sp = Synth::createSynth([](size_t){return nullptr;});
      auto stp = sp->createPatchStore(Synth::FileSystemId::kPatches, true);
      auto pp = std::shared_ptr<Patch>(stp->createPatch(id.typeId));
      Src spr; auto ip = sp->createInstrument(0); ip->assignPatch(pp); ip->connect(spr); ip->setVoiceCount(4); ip->setMidiChannel(0);
      std::shared_ptr<Instrument> ap[1] = { ip };
      sp->setup(std::span<std::shared_ptr<Instrument>>(ap,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
      std::array<float,128> l, r, a1, a2; std::array<AudioBufferRef,2> au{AudioBufferRef(a1),AudioBufferRef(a2)};
      auto blkp = [&]() { sp->renderAudioBuffer({AudioBufferRef(l),AudioBufferRef(r)}, std::span<AudioBufferRef>(au), 0); return peakOf(l); };
      const int n1 = note == 60 ? 60 : 57, n2 = note == 60 ? 62 : 64, n3 = note == 60 ? 64 : 72;
      spr.send(MidiEvent::Type::kNoteOn, 0, n1, 100); for (int b = 0; b < 4; b++) blkp();
      spr.send(MidiEvent::Type::kNoteOn, 0, n2, 100); for (int b = 0; b < 4; b++) blkp();
      const int v2 = open::voicesSounding();
      spr.send(MidiEvent::Type::kNoteOn, 0, n3, 100); for (int b = 0; b < 4; b++) blkp();
      spr.send(MidiEvent::Type::kNoteOn, 0, n3 + 3, 100); for (int b = 0; b < 4; b++) blkp();
      const int v4 = open::voicesSounding();
      spr.send(MidiEvent::Type::kNoteOn, 0, n3 + 7, 100); float pkP = 0; for (int b = 0; b < 20; b++) pkP = std::max(pkP, blkp());
      const int v3 = open::voicesSounding();   // after the 5th note
      const int want = d.maxVoices < 4 ? d.maxVoices : 4;
      printf("%-8s sweep: %d of %d params change the output%s%s | velocity 32 -> 127: peak %.3f -> %.3f, rms %.3f -> %.3f | voices after 2 / 4 / 5 notes: %d / %d / %d (max %d)\n",
             id.name, changed, d.paramCount, still.empty() ? "" : ", unchanged: ", still.size() > 150 ? (still.substr(0, 150) + "..").c_str() : still.c_str(), pkLo, pkHi, eLo, eHi, v2, v4, v3, d.maxVoices);
      check(std::isfinite(pkBase) && pkBase > 1e-4f && changed >= 3 && changed * 5 >= d.paramCount, (std::string(id.name) + ": default patch sounds, parameter changes alter the output").c_str());
      check(std::isfinite(pkLo) && std::isfinite(pkHi) && pkLo > 1e-5f && pkHi > 1e-4f, (std::string(id.name) + ": velocity 32 and 127 both sound, finite").c_str());
      check(v2 == (d.maxVoices < 2 ? d.maxVoices : 2) && v4 == want && v3 == want && std::isfinite(pkP) && pkP > 1e-4f, (std::string(id.name) + ": voices follow the cap (4, kit engines 1), a fifth note steals").c_str());
      if (id.typeId == 0x85) {
        // T-Type note map (ttype.cpp drumForNote): 60 kick, 61 rim, 62 snare, 63 and 71 clap, 64..67 hat closed .. open, 68 cowbell,
        // 69 crash, 70 ride, 48..59 the three toms (4 semitones each), below 48 the low tom, 72 and up wraps onto 60..71
        bool all = true; std::vector<float> tmp;
        for (int nn : {36, 47, 48, 52, 56, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 84}) {
          const float pk = renderCase(id.typeId, nn, 100, -1, 0, tmp);
          if (!std::isfinite(pk) || pk < 1e-3f) { printf("  T-Type note %d: peak %g FAIL\n", nn, pk); all = false; }
        }
        check(all, "T-Type: every note of the map (36..72, 84) plays a drum, finite");
        renderCase(id.typeId, 62, 100, -1, 0, tmp); const double snareE = energy(tmp);
        std::vector<float> kick; renderCase(id.typeId, 60, 100, -1, 0, kick); const double kickE = energy(kick);
        std::vector<float> hit; renderCase(id.typeId, 60, 40, -1, 0, hit);
        check(kickE > 1e-4 && snareE > 1e-4 && energy(hit) < 0.9 * kickE && energy(hit) > 0.05 * kickE, "T-Type: velocity (Accent) scales a hit, kick and snare differ");
        // kit semantics: kick, then a snare 20 blocks later: the kick still rings under it (one voice, drums overlap)
        auto s9 = Synth::createSynth([](size_t){return nullptr;});
        auto st9 = s9->createPatchStore(Synth::FileSystemId::kPatches, true);
        auto p9 = std::shared_ptr<Patch>(st9->createPatch(id.typeId));
        Src src9; auto i9 = s9->createInstrument(0); i9->assignPatch(p9); i9->connect(src9); i9->setVoiceCount(2); i9->setMidiChannel(0);
        std::shared_ptr<Instrument> a9[1] = { i9 };
        s9->setup(std::span<std::shared_ptr<Instrument>>(a9,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
        auto blk9 = [&]() { s9->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); return peakOf(L); };
        float snareOnly = 0; src9.send(MidiEvent::Type::kNoteOn, 0, 62, 100); for (int b = 0; b < 20; b++) snareOnly = std::max(snareOnly, blk9());
        src9.send(MidiEvent::Type::kStop, 0, 0, 0); for (int b = 0; b < 1500; b++) blk9();
        src9.send(MidiEvent::Type::kNoteOn, 0, 60, 100); for (int b = 0; b < 20; b++) blk9();
        src9.send(MidiEvent::Type::kNoteOn, 0, 62, 100); float both = 0; for (int b = 0; b < 20; b++) both = std::max(both, blk9());
        printf("  T-Type snare alone %.4f, kick then snare %.4f\n", snareOnly, both);
        check(std::isfinite(both) && both > snareOnly * 0.9f, "T-Type: a second drum does not cut the first (one voice, drums overlap)");
        for (int nn : {60, 62, 64, 66, 69, 70, 48, 52}) src9.send(MidiEvent::Type::kNoteOn, 0, nn, 127);
        float pkAll = 0; for (int b = 0; b < 100; b++) pkAll = std::max(pkAll, blk9());
        for (int b = 0; b < 3000; b++) blk9(); float tail = 0; for (int b = 0; b < 4; b++) tail = std::max(tail, blk9());
        printf("  T-Type eight drums at once peak=%.4f, after 8.7 s peak=%.6f\n", pkAll, tail);
        check(std::isfinite(pkAll) && pkAll > 1e-3f && tail < 1e-4f, "T-Type: eight drums together stay finite and the kit dies out");
      }
    }
  }
  // ---- idle part: open::partIdle is what PolySynth::update asks to skip a part that renders nothing (no blocks sent at all)
  {
    auto sI = Synth::createSynth([](size_t){return nullptr;});
    auto stI = sI->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto pI = std::shared_ptr<Patch>(stI->createPatch(kLead));
    Src srcI; auto iI = sI->createInstrument(0); iI->assignPatch(pI); iI->connect(srcI); iI->setVoiceCount(1); iI->setMidiChannel(0);
    std::shared_ptr<Instrument> aI[1] = { iI };
    sI->setup(std::span<std::shared_ptr<Instrument>>(aI,1), {}, nullptr, []{ return tiny::optional<float>(120.0f); });
    auto blkI = [&]() { sI->renderAudioBuffer({AudioBufferRef(L),AudioBufferRef(R)}, std::span<AudioBufferRef>(aux), 0); return peakOf(L); };
    check(open::partIdle(0), "idle part: 1 voice allowed, nothing sounding -> partIdle");
    check(!open::partIdle(1) && !open::partIdle(-1), "a part that does not exist is never idle (old full path)");
    srcI.send(MidiEvent::Type::kNoteOn, 0, 57, 100);
    check(!open::partIdle(0), "note on: the part is not idle (checked before the first block renders)");
    float pkI = 0; for (int b = 0; b < 40; b++) pkI = std::max(pkI, blkI());
    check(!open::partIdle(0) && pkI > 1e-3f, "note held: not idle, sounds");
    srcI.send(MidiEvent::Type::kNoteOff, 0, 57, 0);
    check(!open::partIdle(0), "note off: the release still sounds, not idle");
    for (int b = 0; b < 3000 && !open::partIdle(0); b++) blkI();
    float tailI = 0; for (int b = 0; b < 4; b++) tailI = std::max(tailI, blkI());
    check(open::partIdle(0) && tailI < 1e-4f, "voice ended: idle again and the block it would have sent is silent");
    srcI.send(MidiEvent::Type::kNoteOn, 0, 57, 100);
    iI->setVoiceCount(0);
    check(open::partIdle(0), "Voices set to 0: idle");
  }
  printf(bad ? "SMOKE TEST FAILED\n" : "voice pool, governor, patch files and pitch bend OK\n");
  return bad ? 1 : 0;
}
