#pragma once

// ff_config.h: the derived feature flags of tracker-ff and the legal combinations of the build flags, in one place.
//
// The build sets the base flags (config/ff_release.mk lists the release ones, core/makefile and platforms/mk66/makefile turn the make
// variables into -D defines, the simulator reads the same list). This header only derives flags from them and refuses the combinations
// that cannot work, so a flavour that is wrong fails at the first file that includes it, with the reason. It defines no flag the build
// sets and holds no code: a stock build (no FF flag) gets the stock values below and nothing else.
//
// synth-open/tools/ff_flags_check.sh compiles this header against every flavour (release, stock, the simulator flavours, Tracker+ and Mini)
// and against each illegal combination below, which must fail.

// ---- derived flags -------------------------------------------------------------------------------------------------------------------

// Skip-back capture (core/source/skipback.h): the EXT_HOOKS feature set on the original Tracker. The release image leaves it out
// (FEATURE_NO_SKIPBACK, Capture needs the SDRAM, user decision 2026-11-15), the code stays for 1.5.
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(FEATURE_NO_SKIPBACK)
#define FEATURE_SKIPBACK 1
#endif

// REC on the Synth play page (core/source/synthPlayRec.h): every build that has the page, the open synth stack on the original Tracker and
// the stock synth parts of Tracker+ and Mini (FEATURE_SYNTH_PATCHFX). REC records the master output block that ffHookAudioBlockOut gets
// (Tracker.cpp updateAudioBuffers, the same 128 frames on the IMX), it needs no synth part tap
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
#define FEATURE_SYNTH_REC 1
#endif

// Table lite (core/source/tables.h, tables_run.inc, the TBL step FX, the tbl. keys of project.mtff): every plugin host build. On Tracker+
// and Mini (PLATFORM_IMX, the stock synth parts without the open stack) a synth part takes the row's transpose as pitch bend inside the
// stock parts' fixed bend range and the row's pan, sends and CC A-F, not its volume (tables_run.inc tableSynthTranspose)
#if defined(FEATURE_PLUG_HOST)
#define FEATURE_TABLES 1
#endif

// Part destinations of the synth automation (core/AudioEngine/partAuto.h: Pan, Gain, Reverb send, Delay send of a synth part, the kAuto source
// of audioEngine::updateSynthAuto): the open synth stack of the original Tracker feeds them, on Tracker+ and Mini the stock synth parts take them
// through the same setSynthParam path from the partauto store (its writer, the Synth Automation page, is IMX port round 3)
#if defined(FEATURE_SYNTH) && (defined(FEATURE_SYNTH_OPEN) || defined(PLATFORM_IMX))
#define FEATURE_PART_AUTO 1
#endif

// The Table page of the instrument editor (core/interface/instrumentEditor/TablePage.cpp): 1 or 0 (not undefined), the editor tests it with #if.
#ifdef FEATURE_TABLES
#define GUI_TABLES 1
#else
#define GUI_TABLES 0
#endif

// Which engine icons are built in (core/libs/NewDisplay/displayStructs.h). PERC and CHIP are lettered logos that the OpenTracker engines
// borrow (T-Type wears PERC, 8-Type Hand-Type and B-Type wear CHIP, synth-open/engine_list.h), so they are kept when any of those engines is
// picked. FW_ICON_PERC: PERC icons are always built in without FW_TRIM. FW_ICON_CHIP: CHIP icons are always built in outside SYNTH_OPEN.
#if !defined(FW_TRIM) || defined(SYNTH_ENGINE_PERC) || defined(SYNTH_ENGINE_TTYPE)
#define FW_ICON_PERC 1
#else
#define FW_ICON_PERC 0
#endif
#if !defined(FEATURE_SYNTH_OPEN) || defined(SYNTH_ENGINE_CHIP) || defined(SYNTH_ENGINE_8TYPE) || defined(SYNTH_ENGINE_HANDTYPE) || \
    defined(SYNTH_ENGINE_BTYPE)
#define FW_ICON_CHIP 1
#else
#define FW_ICON_CHIP 0
#endif

// ---- legal combinations --------------------------------------------------------------------------------------------------------------
// One line per rule, each with the reason. Preprocessor checks, so they also hold in a C file and in every flavour of the simulator.

#if defined(FEATURE_SYNTH_OPEN) && !defined(FEATURE_SYNTH)
#error "ff_config: FEATURE_SYNTH_OPEN needs FEATURE_SYNTH (the open stack replaces the engines behind the stock synth parts)"
#endif
#if defined(FEATURE_PLUG_HOST) && !defined(FEATURE_SYNTH_OPEN) && !defined(PLATFORM_IMX)
#error "ff_config: PLUG_HOST=1 needs SYNTH_OPEN=1 (the sidecar hooks live in the open synth paths)"
#endif
// Tracker+ and Mini (IMX port, ref/ff-imx-port-plan.md R2.1): the plugin host there is the master insert host plus the plugin synth engines
// next to the closed libPolySynth (no FlexNVM bank, the image has no open synth stack, the simulator's non-pin plus build keeps it), so
// PLUG_HOST comes without SYNTH_OPEN and needs the project store (project.mtff) and the FF core features
#if defined(FEATURE_PLUG_HOST) && defined(PLATFORM_IMX) && (!defined(FEATURE_PROJECT_STORE) || !defined(FEATURE_FF_CORE))
#error "ff_config: the IMX plugin host needs FEATURE_PROJECT_STORE and FEATURE_FF_CORE (core/makefile PLATFORM=IMX)"
#endif
#if defined(FEATURE_SEND_DUCK) && (!defined(FEATURE_PLUG_HOST) || !defined(FEATURE_SYNTH))
#error "ff_config: FEATURE_SEND_DUCK keeps its settings in the plugin sidecar and takes synth parts as sources (PLUG_HOST=1)"
#endif
#if defined(FEATURE_PARTS) && !defined(FEATURE_PROJECT_STORE)
#error "ff_config: FEATURE_PARTS needs FEATURE_PROJECT_STORE (the Parts list is stored in project.mtff)"
#endif
#if defined(FEATURE_MODIT_MODULE) && (!defined(FEATURE_EXT_HOOKS) || !defined(FEATURE_PLUG_HOST))
#error "ff_config: FEATURE_MODIT_MODULE needs FEATURE_EXT_HOOKS and FEATURE_PLUG_HOST (the card module runs in the plugin slot)"
#endif
