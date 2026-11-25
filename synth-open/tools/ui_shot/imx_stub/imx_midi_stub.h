// imx_midi_stub.h: the vendor SDK surface below the REAL Tracker+ / Mini MIDI driver files in the plus and mini sims
// (platforms/imx/usb/usb_midi.c, source/UsbMidiImpl.cpp, source/UartMidiImpl.cpp). Everything here is what the NXP USB stack and the LPUART driver
// would provide: types, the two endpoint calls (USB_DeviceMidiSend / USB_DeviceMidiRecv) and the interrupt mask calls. host_imxmidi.cpp implements
// the calls, the one-line forwarding headers of this directory (usb.h, board.h, fsl_lpuart.h, ...) name the headers usb_midi.c and MidiUart.h include.
#ifndef IMX_MIDI_STUB_H_
#define IMX_MIDI_STUB_H_
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int usb_status_t;
enum { kStatus_USB_Success = 0 };
typedef void* usb_device_handle;
typedef void* class_handle_t;
#define USB_DMA_NONINIT_DATA_ALIGN(x)
#define USB_DATA_ALIGN_SIZE 4
#define HS_MIDI_BULK_OUT_PACKET_SIZE 512
#define USB_MIDI_BULK_IN_ENDPOINT 2
#define USB_MIDI_BULK_OUT_ENDPOINT 1

typedef struct
{
    uint32_t length;
    uint8_t* buffer;
    uint8_t isSetup;
} usb_device_endpoint_callback_message_struct_t;
enum { kUSB_DeviceMidiEventSendResponse = 1, kUSB_DeviceMidiEventRecvResponse = 2 };
typedef struct { uint16_t maxPacketSize; } usb_device_endpoint_struct_t;
typedef struct { struct { volatile bool isBusy; } bulkIn; } usb_device_midi_struct_t;
typedef struct { struct { usb_device_handle deviceHandle; class_handle_t midiHandle; } midi; } usb_device_composite_struct_t;

#ifdef __cplusplus
extern "C"
{
#endif
    extern usb_device_endpoint_struct_t g_MidiEndpoints[];
    usb_status_t USB_DeviceMidiSend(class_handle_t handle, uint8_t ep, uint8_t* buffer, uint32_t length);
    usb_status_t USB_DeviceMidiRecv(class_handle_t handle, uint8_t ep, uint8_t* buffer, uint32_t length);
    uint32_t DisableGlobalIRQ(void);
    void EnableGlobalIRQ(uint32_t primask);
    // defined by the real usb_midi.c
    usb_status_t USB_DeviceMidiCallback(class_handle_t handle, uint32_t event, void* param);
    usb_status_t USB_DeviceMidiSetConfigure(class_handle_t handle, uint8_t configure);
    usb_status_t USB_DeviceMidiImplInit(usb_device_composite_struct_t* deviceComposite);
#ifdef __cplusplus
}
#endif

// LPUART (MidiUart.h): only the types the class holds and the instance UartMidiImpl::init names
typedef struct { uint32_t reg; } LPUART_Type;
typedef struct { uint32_t state; } lpuart_handle_t;
typedef struct { uint8_t* data; size_t dataSize; } lpuart_transfer_t;
extern LPUART_Type g_hostLpuart5;
#define LPUART5 (&g_hostLpuart5)
#endif
