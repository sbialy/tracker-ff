// The project state store: project.mtff, the Tracker FF settings of a project that project.mt has no room for (the codec and the file
// rules are mtff_codec.h). The file manager calls it through ff_hooks.h on every project load, Workspace save, open and Save As, in any
// build with FEATURE_PROJECT_STORE, plugin host or not. The plugin host keeps plugins.txt (plug_host.cpp) and shares the card, the path
// buffers and the file operations of projectStoreIo.h. See projectSidecar.h.
#ifdef FEATURE_PROJECT_STORE

#include "projectSidecar.h"
#include "projectStoreIo.h"
#include "ff_config.h"
#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
#define MTFF_SYNTH_AUTO 1   // Tracker+ and Mini: the Synth Automation slots are synth.N.auto.K lines (mtff_codec.h), the original Tracker's are in its patches
#include "partAuto.h"
#endif
#include "mtff_codec.h"
#include "log.h"
#include "mtSequencer.h"
#include "mtNoteName.h"
#include "mtMidi.h"
#include "stereoPairs.h"
#include "mtFollowChord.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#endif
#include "ff_config.h"
#ifdef FEATURE_TABLES
#include "tables.h"   // the table pool (mtSequencer.cpp), a plugin host build feature today (ff_config.h FEATURE_TABLES)
#endif
#ifdef FEATURE_SYNTH_OPEN
#include "open_patch.h"
#endif
#include "fx34_io.h"
#include "strum_depth.h"

#include "FileSystemAdapter.h"
#include "Platform.h"
#include "mtAudioEngine.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"
#include "scales.h"
#include "cr_section_macros.h"

#include <stdio.h>
#include <string.h>

namespace projstore
{
#ifndef FEATURE_SYNTH_OPEN
// cardStat without the open synth stack (projectStoreIo.h): FileSystemAdapter::statPath's FatFs call. Tracker+ and Mini run FatFs too
}   // namespace projstore
extern "C" unsigned f_stat(const char* path, void* fileInfo);   // FatFs, FR_OK 0, FR_NO_FILE 4
namespace projstore
{
Presence cardStat(const char* p)
{
    constexpr unsigned kFrOk = 0;
    constexpr unsigned kFrNoFile = 4;
    const unsigned r = f_stat(p, nullptr);
    return r == kFrOk ? Presence::kPresent : r == kFrNoFile ? Presence::kAbsent : Presence::kUnknown;
}
#endif
using namespace plug;

// the card and the path buffers of both project files (projectStoreIo.h). Off the stack, which the audio interrupt shares
FileSystem* g_fs;
__NOINIT(EXTERNAL_RAM) char g_pathA[kPathMax];
__NOINIT(EXTERNAL_RAM) char g_pathB[kPathMax];
__NOINIT(EXTERNAL_RAM) char g_pathScratch[kPathBuf];

namespace
{
char (&s_pathA)[kPathMax] = g_pathA;
char (&s_pathB)[kPathMax] = g_pathB;
// project.mtff (mtff_codec.h): the file as read or written (unknown lines are kept from it), what it holds, what the load found
__NOINIT(EXTERNAL_RAM) char s_mtffRaw[kMtffMax];
__NOINIT(EXTERNAL_RAM) char s_mtffTmp[kMtffMax];   // the new text of a save, the copy buffer of open and save, the park scratch
int s_mtffRawN;
Mtff s_mtff;
SidecarState s_mtffState;
bool s_mtffOpenStale;   // the last project open could not copy the project's project.mtff to the Workspace
MtStamp s_stamp;        // the project.mt last read or written (projectStamp)
MtStamp s_openStamp;    // the project.mt of the project folder an open read, before the Workspace copy got its name and path (openStampKeep)
const char* s_buildId;   // BUILD_ID of the running image (the git sha7), kept by boot() for the header of project.mtff
bool s_mtffUserOpen;     // sidecarOpen ran: the next project load is a user's open (File screen, USB console), not a Workspace reload or autosave
uint8_t s_mtffNotice;    // what that open found for the screen (plug_host.h takeOpenNotice): 0 none, 1 a newer writer of this format, 2 a newer format
uint32_t s_mtffNoticeFf; // writer of the file, packed as mtffWriterVersion
uint16_t s_mtffNoticeFormat;
char s_mtffNote[160];   // the one status line of the last load: parked lines, a file of a newer format, a card error (host.txt)
void (*s_statusWriter)();   // the plugin host's host.txt, which names the state and the note of project.mtff (null without the host)
}   // namespace

// ---- project.mtff (mtff_codec.h). The same hooks as plugins.txt, the same file layer (.tmp, read back, .bak swap), the same path rules
namespace
{
// The settings of s_mtff into the sequencer, the audio engine and the Follow chord and Parts state: a load (the project's file, or
// the defaults of a missing one). One entry per feature, in the order the load applies them. The compat rule is structural: a project
// without the feature's key reads as the cleared field (mtffClear, all zero), and each apply below turns zero into what a firmware
// without the feature does (synth-open/tools/plug_mtff_test.cpp "reset" proves the read side for every key). A new key adds one apply
// here, its parse and emit in mtff_codec.h and one row in that test. The Parts lines apply in mtffLoaded (stale and slot rules)
void applyStrum() { strumramp::setDepth(s_mtff.depth); }   // strum.ramp, 0 = ramp off
void applySeed() { sequencer.randomSeed = s_mtff.seed; }    // seed.take, 0 = Off (unseeded random takes)
void applySnap()   // track.N.snap, 0 = every track Off. Scale snap (scales.h) is a plugin host build feature today
{
#ifdef FEATURE_FF_CORE
    static_assert(kSnapTracks == kScaleSnapTracks && sizeof(scaleSnapMode) == sizeof(s_mtff.snap), "scale snap keys and table");
    memcpy(scaleSnapMode, s_mtff.snap, sizeof(scaleSnapMode));
#endif
}
void applyGroove()   // track.N.groove, 0 = straight
{
    sequencer.clearTrackGrooves();
    for (int t = 0; t < kMtffTracks; t++) sequencer.setTrackGroove(uint8_t(t), s_mtff.groove[t]);
}
void applyFollow()   // track.N.follow, 0 = Off
{
    static_assert(followchord::kTracks == kMtffTracks, "follow chord keys");
    for (int t = 0; t < kMtffTracks; t++)
    {
        followchord::setting.mode[t] = s_mtff.followMode[t];
        followchord::setting.source[t] = s_mtff.followSource[t];
    }
    followchord::recalc();
    followchord::resetLive();
}
void applyDuck()   // duck.send, source 0 = ducking off
{
#ifdef FEATURE_SEND_DUCK
    engine.setSendDuck(s_mtff.duck.source ? s_mtff.duck : kDuckDefaults);
#endif
}
void applyPairs() { engine.setStereoPairs(s_mtff.pairs); }   // track.N.pair, 0 = every track mono (the pairs link the voices, stereoPairs.h)
void applyFxSlots() { fx34io::applyProjectSwitch(s_mtff.fxSlots == 4); }   // fx.slots, 0 = FX3 and FX4 off
void applySynthAuto()   // synth.N.auto.K, every slot Off when the project has none (partauto::clearSlots also marks the store ready)
{
#ifdef MTFF_SYNTH_AUTO
    static_assert(kMtffAutoParts == partauto::kStoreParts && kMtffAutoSlots == partauto::kSlots, "synth automation keys and store");
    partauto::clearSlots();
    for (int p = 0; p < kMtffAutoParts; p++)
    {
        for (int k = 0; k < kMtffAutoSlots; k++)
        {
            const MtffAutoSlot& a = s_mtff.synthAuto[p][k];
            if (a.type < 1 || a.type > 2)
                continue;
            partauto::Slot r = partauto::slot(p, k);   // the Off defaults of clearSlots, then the stored fields
            r.type = polysynth::open::ModType(a.type);
            r.kind = polysynth::open::ModKind::kPart;
            r.shape = a.shape;
            r.speed = a.speed;
            r.dest = a.dest;
            r.amount = a.amount;
            r.sustain = a.sustain;
            r.attack = a.attack;
            r.decay = a.decay;
            r.release = a.release;
            partauto::setSlot(p, k, r);
        }
    }
#endif
}
void applyMacro()   // macro.mode, 0 = each patch as it always was
{
#ifdef FEATURE_SYNTH_OPEN
    polysynth::open::setProjectMacroMode(s_mtff.macroMode);
#endif
}
void applyTune()   // tune.*, absent = 12 notes of 1200 cents at key 60 / 261.6256 Hz: the project sounds as it always did
{
    tuning::set(s_mtff.tuneN ? s_mtff.tuneN : 12, s_mtff.tunePeriodMc ? s_mtff.tunePeriodMc : tuning::kDefPeriodMc,
                s_mtff.tuneRoot1 ? s_mtff.tuneRoot1 - 1 : int(tuning::kDefRoot), s_mtff.tuneHzE4 ? s_mtff.tuneHzE4 : tuning::kDefHzE4);
    tuning::cfg.names = s_mtff.tuneNames;   // tune.names ud (phase 3)
    tuning::cfg.file = s_mtff.tuneScl ? 1 : 0;   // a Scala tuning: its degrees are the live table the load just read (tune.cents, mtff_codec.h), else N equal steps
    tuningChanged();
}
void applyMpe()   // mpe.out, absent = Off: the MIDI out is what it always was
{
    mpeOutSet(s_mtff.mpeOut);
}
// The load order. Plain calls, not a table of pointers: the compiler inlines them into the one caller (a pointer table cost +112 B)
void mtffApply()
{
    applyStrum();
    applySeed();
    applySnap();
    applyGroove();
    applyFollow();
    applyDuck();
    applyPairs();
    applyFxSlots();
    applySynthAuto();
    applyMacro();
    applyTune();
    applyMpe();
}

// This build as project.mtff names it in the header (mtff_codec.h): the version of mtStructs.h and the git sha of the image. Every load and
// save sets it first (a clear keeps it)
void mtffIdent()
{
    MtffId& id = s_mtff.self;
    id.major = CORE_VER_1;
    id.minor = CORE_VER_2;
    id.patch = CORE_VER_3;
    id.valid = true;
    int b = 0;
    for (const char* q = s_buildId; q && *q && b < kMtffBuildLen; q++)
    {
        const char c = *q;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) break;
        id.build[b++] = c;
    }
    id.build[b] = 0;
}

// The live state of the sequencer and the Follow chord setting into s_mtff (the Parts list is the live one, s_mtff.parts)
void mtffCollect()
{
    mtffIdent();
    s_mtff.depth = uint8_t(strumramp::depthPct());
    s_mtff.seed = sequencer.randomSeed;
    s_mtff.fxSlots = fx34::enabled() ? 4 : 0;
#ifdef FEATURE_SYNTH_OPEN
    s_mtff.macroMode = polysynth::open::projectMacroMode();
#endif
#ifdef FEATURE_FF_CORE   // without the table the snap keys stay as the load read them: a save does not drop them
    memcpy(s_mtff.snap, scaleSnapMode, sizeof(s_mtff.snap));   // the Project Settings > Scale snap rows
#endif
    for (int t = 0; t < kMtffTracks; t++)
    {
        s_mtff.groove[t] = sequencer.getTrackGroove(uint8_t(t));
        s_mtff.followMode[t] = followchord::setting.mode[t];
        s_mtff.followSource[t] = followchord::setting.source[t];
    }
    // only what differs from the default is stored (a field equal to the old behaviour is never written)
    s_mtff.mpeOut = mpeOutMembers();
    s_mtff.tuneN = tuning::cfg.n != 12 ? tuning::cfg.n : 0;
    s_mtff.tuneRoot1 = tuning::cfg.root != tuning::kDefRoot ? uint8_t(tuning::cfg.root + 1) : 0;
    s_mtff.tunePeriodMc = tuning::cfg.periodMc != tuning::kDefPeriodMc ? tuning::cfg.periodMc : 0;
    s_mtff.tuneHzE4 = tuning::cfg.hzE4 != tuning::kDefHzE4 ? tuning::cfg.hzE4 : 0;
    s_mtff.tuneDeg = tuning::degMc;
    s_mtff.tuneName = tuning::fileName;
    s_mtff.tuneNames = tuning::cfg.names;
    s_mtff.tuneScl = tuning::cfg.file;   // the degrees and the name are the live tables, written only for a Scala tuning
#ifndef PLATFORM_IMX   // stereo pairs are an MK66 feature (audioEngine::setStereoPairs does nothing on IMX): the links stay as the load read them
    s_mtff.pairs = stereopairs::g_mask;
#endif
#ifdef MTFF_SYNTH_AUTO   // a slot that is on and has a part destination, the rest is Off (a slot without a destination yet is not saved)
    for (int p = 0; p < kMtffAutoParts; p++)
    {
        for (int k = 0; k < kMtffAutoSlots; k++)
        {
            const partauto::Slot& r = partauto::slot(p, k);
            MtffAutoSlot& a = s_mtff.synthAuto[p][k];
            a = {};
            if (r.type == polysynth::open::ModType::kOff || r.kind != polysynth::open::ModKind::kPart || r.dest < 0 || r.dest >= 4)
                continue;
            a.type = uint8_t(r.type);
            a.shape = r.shape;
            a.speed = r.speed;
            a.dest = uint8_t(r.dest);
            a.amount = r.amount;
            a.sustain = r.sustain;
            a.attack = r.attack;
            a.decay = r.decay;
            a.release = r.release;
        }
    }
#endif
    s_mtff.hasStamp = s_stamp.valid;
    s_mtff.crc = s_stamp.crc;
    s_mtff.size = s_stamp.size;
}

// Workspace load: the file, the stale check against the project.mt just read, the settings applied (a project without the file or
// the key plays with the ramp off, a new project starts at 100)
bool mtffSaved(const char* dir);

void mtffLoaded(const char* dir, bool newProject)
{
    s_mtffNote[0] = 0;
    const bool userOpen = s_mtffUserOpen;   // one open, one notice: a Workspace reload or an autosave never has the flag
    s_mtffUserOpen = false;
    s_mtffNotice = 0;
#ifdef FEATURE_PARTS
    s_mtff.parts = &parts::list();   // the live Parts list: a load fills it in place, a save writes it
#endif
#ifdef FEATURE_TABLES
    s_mtff.tables = &tables::pool();   // the live table pool, the same way (a project without tbl. lines has every table empty)
#endif
    mtffIdent();
    s_mtff.tuneDeg = tuning::degMc;   // the live Scala degrees and file name: a load fills them in place (mtffClear zeroes them first)
    s_mtff.tuneName = tuning::fileName;
    mtffClear(s_mtff);
    s_mtffRawN = 0;
    if (newProject)
    {   // an empty Workspace: no file. The default is written by the project.mtff save that follows the first project.mt save
        s_mtffOpenStale = false;
        s_mtffState = SidecarState::kNone;
        s_mtff.depth = uint8_t(strumramp::kDefaultNewProject);
        mtffApply();
        return;
    }
    SdOps fs;
    // An open writes the project's own name and path into the Workspace project.mt before it is loaded, so a project folder copied or renamed
    // by hand (or uploaded under another name) reads back with another CRC than the one project.mtff was written with: the project's file
    // is the one to compare with, else the table bindings of every such project are parked as a rename
    const MtStamp openCmp = s_openStamp;
    const MtStamp& stamp = userOpen && openCmp.valid ? openCmp : s_stamp;
    s_openStamp.valid = false;
    if (sidecarPath(s_pathA, kPathMax, dir, kMtffName))
        s_mtffState = mtffLoadStep(fs, s_pathA, s_mtffRaw, kMtffMax, s_mtff, s_mtffRawN, s_mtffOpenStale, stamp, s_mtffTmp, kMtffMax);
    else
    {
        LOGEF("project.mtff path too long: %s", dir);
        s_mtffState = SidecarState::kUnreadable;
        s_mtffOpenStale = false;
    }
#ifdef FEATURE_PARTS
    int blanked = 0;
    uint8_t gone = 0;   // the first pattern found gone, for the status line
    bool more = false;  // and another one
    if (s_mtff.stale && s_mtff.parts)
    {   // project.mt written by another firmware: the Parts apply, a cell whose pattern it deleted is blanked (mtff_codec.h)
        blanked = mtffBlankMissing(
            *s_mtff.parts,
            [](uint8_t pat) {   // s_pathB: free after the load (the main stack stays as it was)
                snprintf(s_pathB, sizeof(s_pathB), cWorkspacePatternFileFormat, int(pat));   // what Parts read (readWorkspacePatternTo)
                return cardStat(s_pathB) != Presence::kAbsent;   // a card error blanks nothing
            },
            [&gone, &more](int part, int track, uint8_t pat) {   (void)part; (void)track;   // the log line is compiled out on IMX (LOGEF)
                if (!gone) gone = pat;
                more = more || pat != gone;
                LOGEF("Part %d track %d: pattern %u is gone, cell blanked", part + 1, track + 1, unsigned(pat));
            });
    }
#endif
    mtffApply();   // every setting at its old behaviour for every state but a file that holds the key
    // the one status line (mtff_codec.h, Versions): a file this build must not touch, then the older kinds of trouble, then a newer writer
    const bool newerWriter = s_mtffState == SidecarState::kLoaded && mtffWriterVersion(s_mtff) > mtffSelfVersion(s_mtff);
    if (sidecarBlocked(s_mtffState))
    {
        if (s_mtff.newer)
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s is of a newer format (%u, FF %u.%u.%u): settings not applied, file kept", kMtffName,
                     unsigned(s_mtff.format), unsigned(s_mtff.writer.major), unsigned(s_mtff.writer.minor), unsigned(s_mtff.writer.patch));
        else
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s not read (card error or over %d bytes): settings not applied, file kept", kMtffName,
                     kMtffMax);
    }
    else if (s_mtff.parked)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "FF settings of %u line%s parked: project.mt was changed outside FF", unsigned(s_mtff.parked),
                 s_mtff.parked == 1 ? "" : "s");
    else if (s_mtff.stale)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "project.mt was changed outside FF, settings applied");
    else if (newerWriter)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "%s was written by FF %u.%u.%u: settings this build does not know are kept", kMtffName,
                 unsigned(s_mtff.writer.major), unsigned(s_mtff.writer.minor), unsigned(s_mtff.writer.patch));
#ifdef FEATURE_PARTS
    if (blanked)
    {
        const size_t at = strlen(s_mtffNote);
        snprintf(s_mtffNote + at, sizeof(s_mtffNote) - at, "%s%d Part cell%s blanked: pattern %u%s gone", at ? ", " : "", blanked, blanked == 1 ? "" : "s",
                 unsigned(gone), more ? " and others" : "");
    }
#endif
    if (fx34io::parkedCount())
    {   // after the other line, if there is one
        const size_t at = strlen(s_mtffNote);
        snprintf(s_mtffNote + at, sizeof(s_mtffNote) - at, "%sFX3/FX4 of the pattern on screen parked: the pattern file was changed outside FF",
                 at ? ", " : "");
    }
    if (s_mtffState == SidecarState::kLoaded || s_mtff.newer) {   // braces: LOGEF is empty on IMX
        LOGEF("mtff format %u ff %lx build %s -> this build format %d ff %lx", unsigned(s_mtff.format), (unsigned long)mtffWriterVersion(s_mtff),
              s_mtff.writer.build, kMtffMajor, (unsigned long)mtffSelfVersion(s_mtff)); }
    if (s_mtffNote[0]) { LOGEF("%s", s_mtffNote); }
    if (&stamp == &openCmp && s_mtffState == SidecarState::kLoaded && !s_mtff.stale && (openCmp.crc != s_stamp.crc || openCmp.size != s_stamp.size))
        mtffSaved(dir);   // the Workspace project.mt has the project's name now: the Workspace file takes its stamp, or the next start parks the bindings
    if (userOpen && (newerWriter || (sidecarBlocked(s_mtffState) && s_mtff.newer)))
    {   // for the screen (takeOpenNotice): a refused newer format first, then a newer writer. The cap and card errors stay in host.txt
        s_mtffNotice = sidecarBlocked(s_mtffState) ? 2 : 1;
        s_mtffNoticeFf = mtffWriterVersion(s_mtff);
        s_mtffNoticeFormat = uint16_t(s_mtff.format);
    }
}

// Workspace save: the file when a setting differs from the old behaviour or a line is kept, none otherwise
bool mtffSaved(const char* dir)
{
    SdOps fs;
    mtffCollect();
    if (!sidecarPath(s_pathA, kPathMax, dir, kMtffName))
    {
        LOGEF("project.mtff path too long: %s", dir);
        return mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) == 0;
    }
    const bool ok = mtffSave(fs, s_pathA, s_mtff, s_mtffState, s_mtffRaw, s_mtffRawN, kMtffMax, s_mtffTmp, kMtffMax);
    if (ok && s_mtffState == SidecarState::kUnreadable && s_mtff.newer && mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) != 0)
    {   // a file of a newer format stays as it is (Workspace, project folder, Save As copy): what this build holds for it is dropped, and
        // never reaches the Workspace file, so a reload cannot bring it back. Said once per load (host.txt) and in the log on every save
        LOGEF("%s is of a newer format: FF settings changed in this build are not saved, file kept", kMtffName);
        if (!strstr(s_mtffNote, "not saved"))
        {
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s is of a newer format (%u): FF settings changed here are not saved, file kept", kMtffName,
                     unsigned(s_mtff.format));
            if (s_statusWriter) s_statusWriter();   // host.txt of the plugin host
        }
    }
    return ok;
}

bool mtffOpened(const char* projectDir, const char* workspaceDir)
{
    SdOps fs;
    const bool wsOk = sidecarPath(s_pathB, kPathMax, workspaceDir, kMtffName);
    if (!sidecarPath(s_pathA, kPathMax, projectDir, kMtffName) || !wsOk)
    {
        LOGEF("project.mtff path too long: %s", projectDir);
        s_mtffOpenStale = true;
        if (wsOk) sidecarRemove(fs, s_pathB);
        return false;
    }
    return sidecarOpenStep(fs, s_pathA, s_pathB, s_mtffTmp, kMtffMax, s_mtffOpenStale);
}

bool mtffToProject(const char* workspaceDir, const char* projectDir)
{
    SdOps fs;
    mtffCollect();
    const bool has = mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) != 0;
    if (!sidecarPath(s_pathA, kPathMax, workspaceDir, kMtffName) || !sidecarPath(s_pathB, kPathMax, projectDir, kMtffName))
    {
        LOGEF("project.mtff path too long: %s", projectDir);
        return !has;
    }
    return mtffProjectStep(fs, s_pathA, s_pathB, has, s_mtffState, s_mtffTmp, kMtffMax);
}
}   // namespace

bool writeText(const char* path, const char* text, int n)
{
    SdOps ops;
    return sidecarReplace(ops, path, text, n);
}

int readText(const char* path, char* buf, int cap)
{
    SdOps ops;
    int n = 0;
    return sidecarFetch(ops, path, buf, cap, n) == Presence::kPresent ? n : -1;
}

bool removeFile(const char* path)
{
    SdOps ops;
    return sidecarRemove(ops, path);
}

SidecarState pluginsLoadStep(const char* wsPath, char* buf, int cap, Sidecar& s, bool& stale)
{
    SdOps ops;
    return sidecarLoadStep(ops, wsPath, buf, cap, s, stale);
}

bool pluginsSave(const char* path, const Sidecar& s, SidecarState& st, char* buf, int cap)
{
    SdOps ops;
    return sidecarSave(ops, path, s, st, buf, cap);
}

bool pluginsOpenStep(const char* projectPath, const char* wsPath, char* buf, int cap, bool& stale)
{
    SdOps ops;
    return sidecarOpenStep(ops, projectPath, wsPath, buf, cap, stale);
}

bool pluginsProjectStep(const char* wsPath, const char* projectPath, const Sidecar& s, SidecarState st, char* buf, int cap)
{
    SdOps ops;
    return sidecarProjectStep(ops, wsPath, projectPath, s, st, buf, cap);
}

void boot(Platform* platform)
{
    g_fs = platform->getFileSystem();
    s_buildId = platform->getSystemInfo()->buildId;   // the git sha7 of the image, named in the header of project.mtff
}

void useFs(FileSystem* fs)
{
    g_fs = fs;
}

void setStatusWriter(void (*writer)())
{
    s_statusWriter = writer;
}

const char* note()
{
    return s_mtffNote;
}

int state()
{
    return int(s_mtffState);
}

const DuckParams& duck()
{
    return s_mtff.duck;
}

void duckStore(const DuckParams& p)
{
    s_mtff.duck = p;
}

void loaded(const char* dir, bool newProject)
{
    if (!g_fs) return;
    mtffLoaded(dir, newProject);
}

bool save(const char* dir)
{
    if (!g_fs) return true;
    return mtffSaved(dir);
}

bool open(const char* projectDir, const char* workspaceDir)
{
    if (!g_fs) return true;
    s_mtffUserOpen = true;   // the project load that follows is a user's open (loaded reads and clears it)
    return mtffOpened(projectDir, workspaceDir);
}

bool toProject(const char* workspaceDir, const char* projectDir)
{
    if (!g_fs) return true;
    return mtffToProject(workspaceDir, projectDir);
}

void newProjectSaved(const char* dir)
{
    if (g_fs) mtffSaved(dir);   // the new project's project.mtff (Strum ramp 100) is stamped with the project.mt that was just written
}

bool takeOpenNotice(char* line1, int cap1, char* line2, int cap2)
{
    if (!s_mtffNotice) return false;
    const unsigned a = (s_mtffNoticeFf >> 16) & 0xFF, b = (s_mtffNoticeFf >> 8) & 0xFF, c = s_mtffNoticeFf & 0xFF;
    if (s_mtffNotice == 1)
    {   // the popup is 400 px wide: two short lines
        snprintf(line1, size_t(cap1), "Saved by newer FF %u.%u.%u", a, b, c);
        snprintf(line2, size_t(cap2), "Some settings ignored");
    }
    else
    {
        snprintf(line1, size_t(cap1), "Project from newer FF (format %u)", unsigned(s_mtffNoticeFormat));
        snprintf(line2, size_t(cap2), "FF settings not loaded");
    }
    s_mtffNotice = 0;
    return true;
}

uint32_t writerVersion()
{
    return plug::mtffWriterVersion(s_mtff);
}

int formatVersion()
{
    return plug::mtffFormatVersion(s_mtff);
}

void stamp(uint32_t crc, uint32_t size)
{
    s_stamp.crc = crc;
    s_stamp.size = size;
    s_stamp.valid = true;
}

void openStampKeep()
{
    s_openStamp = s_stamp;
}

int strumRampDepth()
{
    return strumramp::depthPct();
}

void setStrumRampDepth(int pct)
{
    strumramp::setDepth(pct);
}

int macroMode()
{
#ifdef FEATURE_SYNTH_OPEN
    return polysynth::open::projectMacroMode();
#else
    return 0;
#endif
}

void setMacroMode(int mode)
{
#ifdef FEATURE_SYNTH_OPEN
    polysynth::open::setProjectMacroMode(uint8_t(mode));
#else
    (void)mode;
#endif
}

void projectClosing()
{
    setMacroMode(0);   // a patch the load adds (an Init fallback) is a patch of no project yet: project.mtff sets the mode after the load
}

// The links of linked stereo pairs (track.N.pair of project.mtff, stereoPairs.h) BEFORE the instruments of a project load: a project that holds
// one loads both channels of its stereo .pti files (stereoSampleMode). projectLoaded reads the whole file after the instruments, so this reads
// it once more, into the save scratch (nothing of it is kept). 0 for no file, a failed read or a file of a newer format (as projectLoaded)
uint8_t pairsPreload(const char* dir)
{
    if (!g_fs || s_mtffOpenStale) return 0;
    static Mtff pre;
    SdOps fs;
    int n = 0;
    if (!sidecarPath(s_pathA, kPathMax, dir, kMtffName) || sidecarFetch(fs, s_pathA, s_mtffTmp, kMtffMax, n) != Presence::kPresent || n <= 0) return 0;
    return mtffRead(s_mtffTmp, n, pre) && !pre.newer ? pre.pairs : 0;
}

}   // namespace projstore

#endif   // FEATURE_PROJECT_STORE
