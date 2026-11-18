// The unit's .pti instrument file, byte for byte (ARM, 32 bit pointers). NOT firmware, host (ui_shot) only, no includes: constants.
//
// A .pti of the unit is  16 byte header | strInstrument (372 bytes) | CRC-32 (4 bytes) | sample data.  The Mac's strInstrument is larger
// (64 bit pointer, 8 byte alignment), so the host cannot read the file as it is. host_pti.cpp translates field by field with these
// offsets. Two checks keep them honest:
//   host_pti.cpp           static_asserts its mirror struct of the device instrument against these numbers
//   tools/mirror/test/pti_arm_check.sh   compiles core/source/mtStructs.h with arm-none-eabi-g++ and static_asserts the real
//                                        strInstrument against the same numbers (so a firmware change that moves a field fails there)
#pragma once

#include <stddef.h>

namespace ptidev
{
constexpr size_t kHeader = 16;      // 'T' 'I', type 1, fwVersion[4], fileStructureVersion[4], size (u16 = 372), 2 pad
constexpr size_t kInstrument = 372;
constexpr size_t kCrc = 4;
constexpr size_t kFile = kHeader + kInstrument + kCrc;   // 392, where the sample data starts
constexpr size_t kSizeField = 12;   // offset of the size u16 in the header

// strInstrument, offsets inside the 372 bytes
constexpr size_t oIsActive = 0;
constexpr size_t oSample = 4;                 // strSampleBankSlot, 52 bytes
constexpr size_t oSampleType = 0;             //   relative to the slot
constexpr size_t oSampleFileName = 1;         //   32 bytes
constexpr size_t oSampleAddress = 36;         //   u32, the 32 bit pointer
constexpr size_t oSampleLength = 40;
constexpr size_t oSampleWindowSize = 44;      //   u16
constexpr size_t oSampleWindowNumber = 48;
constexpr size_t kSampleSize = 52;
constexpr size_t oName = 56;                  // 4 bytes
constexpr size_t oPlayMode = 60;
constexpr size_t oStartPoint = 62;
constexpr size_t oLoopPoint1 = 64;
constexpr size_t oLoopPoint2 = 66;
constexpr size_t oEndPoint = 68;
constexpr size_t oWavetableCurrentWindow = 72;
constexpr size_t oEnvelope = 76;              // 6 x strEnv (20 bytes)
constexpr size_t kEnvSize = 20;
constexpr size_t oEnvAmount = 0, oEnvDelay = 4, oEnvAttack = 6, oEnvHold = 8, oEnvDecay = 10, oEnvSustain = 12, oEnvRelease = 16,
                 oEnvLoop = 18, oEnvEnable = 19;
constexpr size_t oLfo = 196;                  // 6 x strEnvBasedLfo (8 bytes)
constexpr size_t kLfoSize = 8;
constexpr size_t oLfoAmount = 4;
constexpr size_t oCutOff = 244;
constexpr size_t oResonance = 248;
constexpr size_t oFilterType = 252;
constexpr size_t oFilterEnable = 253;
constexpr size_t oTune = 254;
constexpr size_t oFineTune = 255;
constexpr size_t oVolume = 256;
constexpr size_t oGlide = 258;
constexpr size_t oPanning = 260;
constexpr size_t oDelaySend = 262;
constexpr size_t oSlicePlayThru = 263;
constexpr size_t oSlices = 264;               // 48 x u16
constexpr size_t oSliceNumber = 360;
constexpr size_t oSelectedSlice = 361;
constexpr size_t oGranular = 362;             // 6 bytes
constexpr size_t oGranGrainLength = 0, oGranCurrentPosition = 2, oGranShape = 4, oGranType = 5;
constexpr size_t kGranularSize = 6;
constexpr size_t oReverbSend = 368;
constexpr size_t oOverdrive = 369;
constexpr size_t oBitDepth = 370;
constexpr size_t oTuneResample = 371;
constexpr int kEnvMax = 6;
constexpr int kSlices = 48;
}   // namespace ptidev
