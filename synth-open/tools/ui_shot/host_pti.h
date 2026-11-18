// The unit's .pti files on the Mac (host only, NOT firmware). See host_pti.cpp.
#pragma once

#include "Platform.h"

#include <stddef.h>
#include <stdint.h>

namespace hostp
{
// The FileSystem the host platform hands out: the card folder, and for a .pti of the unit (device layout) the host's view of it.
// `inner` is the plain host file system, kept as is.
FileSystem* ptiFileSystem(FileSystem* inner);
// true: the console's file commands (get, put, hash, stat, ls) and the host tools see the card's bytes, not the view
void ptiRaw(bool on);
bool ptiIsRaw();

// The translation, public for the tests. dev is the 392 byte head of a device .pti (header, instrument, CRC), host the head of the host's
// strInstrumentFile (sizeof(strInstrumentFile) bytes, ptiHostHead()).
size_t ptiHostHead();
bool ptiIsDeviceHead(const uint8_t* head16, size_t available);   // 'TI', type 1, version 8 or later, size field 372
bool ptiIsHostHead(const uint8_t* head16, size_t available);     // the same with the size of the host's strInstrument
void ptiDevToHost(const uint8_t* dev, uint8_t* host);
// prevDev: the device bytes the file had (keeps the padding bytes and the stale pointer), nullptr for a new file
void ptiHostToDev(const uint8_t* host, const uint8_t* prevDev, uint8_t* dev);
// the device bytes (372) of a host strInstrument, the stale pointer 0 and the padding 0 (what the dumps and tests compare)
void ptiInstrumentToDev(const void* hostInstrument, uint8_t* dev372);
}   // namespace hostp
