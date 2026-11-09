#pragma once

#ifdef FEATURE_SYNTH_OPEN
#include <stdint.h>

// One SDRAM scratch buffer for the main loop's large transient buffers. MK66 has no fixed stack: Profiler::init sizes
// the heap to SP minus STACK_MARGIN, which leaves 8,440 B between them, and the audio interrupt with a deep synth
// plugin and an insert takes about 6.4 KB of that on top of whatever the main loop holds. As locals these buffers gave
// 3 to 5 KB frames that ran past the line. Users (one at a time):
//   cDisplay::update (image load chunk), cDisplay::readImgFromSd (JPEG header), cDisplay::doScreenShot (rows),
//   cFileManager::saveSampleDataToWorkspace and ProjectSave::copyInstruments (streamed sample copy chunk),
//   cFileManager::exportSamplesStart (streamed export chunks), cFileManager::saveProjectFileToWorkspace and
//   ProjectOpen::loadProjectFileFromSd (project file image).
// Rules for every user: main loop only, never from an interrupt, the contents are dead when the function that filled
// them returns, and nothing that runs while the buffer is live may reach another user (each one calls only FileSystem,
// FT812 SPI and struct conversion code while it holds the buffer). Defined in display.cpp, 32 B aligned (SDHC ADMA2
// needs 4 B), NOINIT so no startup code touches the SDRAM before it is up.
constexpr uint32_t kMainLoopScratchSize = 5000;
extern uint8_t g_mainLoopScratch[kMainLoopScratchSize];
#endif
