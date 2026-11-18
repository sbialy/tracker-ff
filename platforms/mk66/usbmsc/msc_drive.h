// tracker-ff USB drive (USB_MSC=1, -DFEATURE_USB_MSC): the SD card as a USB mass storage device in Drive mode.
// Design: ref/usb-mass-storage-feasibility.md (decisions of 2026-11-12 in its section 9).
//
// Who owns the card: the firmware, except between enter() (Config > USB > Storage, Interface::handleFileTransferMode(1)
// through UsbImpl::setClassState(kStorage, true)) and FileSystemImpl::remount() (the mode's Exit, or the card pulled). In that
// window FileSystemImpl refuses every call (the card gate) and only the drive reads and writes blocks.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Boot-time switch (Config > Features > USB drive): the EEPROM flag usb_init reads (byte 7, 0x5A, only with a valid config id),
// its writer, and the state this session started with (the mass storage interface is enumerated).
int ffUsbMscBootFlag(void);
void ffUsbMscSetBootFlag(int on);
int ffUsbMscActive(void);

// FileSystemImpl::remount: the computer loses the card (medium not present from its next command on) and the command in
// flight is finished or dropped, so nothing of the drive touches the card after this returns.
void ffUsbMscTakeBack(void);

#ifdef __cplusplus
}

namespace ff
{
namespace mscdrive
{
// Startup (PlatformImpl::init): with the drive enumerated, start answering the computer ("medium not present"). No-op otherwise.
void init();
// Main loop: lend the card to the computer (FileSystemImpl::lendToHost, then the medium is presented). False: no card, or
// the drive is not enumerated this session (feature off at start).
bool enter();
// The computer owns the card.
bool active();
// Main loop pump (PlatformImpl::update): block I/O of the drive.
void update();
} // namespace mscdrive
} // namespace ff
#endif
