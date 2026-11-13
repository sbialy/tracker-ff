// Host dump of the USB descriptors one usb_desc.c serves (tracker-ff Features > USB audio, usb_desc_test.sh). Compiled against
// the real platforms/mk66/3rdparty/teensy_core/usb_desc.c (USB_MIDI): prints every descriptor of usb_descriptor_list with the
// length usb_dev.c GET_DESCRIPTOR sends, and the endpoint configuration SET_CONFIGURATION writes to ENDPT1..8.
// Argument: 0 or 1, the boot flag handed to ff_usb_audio_select (ignored by a tree without the switch).
#include <stdio.h>
#include <stdlib.h>

// kinetis.h first (include guard), then its Cortex-M instructions as no-ops for the serial number code
#include "kinetis.h"
#undef __enable_irq
#undef __disable_irq
#define __enable_irq()
#define __disable_irq()
// Mach-O has no alias attribute: the three weak aliases of usb_desc.c become weak declarations, defined below as the defaults are
#define alias(x) weak
#include "usb_desc.c"
#undef alias

struct usb_string_descriptor_struct usb_string_manufacturer_name = { 2 + MANUFACTURER_NAME_LEN * 2, 3, MANUFACTURER_NAME };
struct usb_string_descriptor_struct usb_string_product_name = { 2 + PRODUCT_NAME_LEN * 2, 3, PRODUCT_NAME };
struct usb_string_descriptor_struct usb_string_serial_number = { 12, 3, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };

// usb_init_serialnumber (never called here) links against these
int kinetis_hsrun_disable(void) { return 0; }
int kinetis_hsrun_enable(void) { return 0; }
char* ultoa(unsigned long v, char* buf, int radix) { (void)v; (void)radix; buf[0] = 0; return buf; }

int main(int argc, char** argv)
{
    const int on = argc > 1 ? atoi(argv[1]) : 0;
#ifdef FF_USB_AUDIO_TX
    ff_usb_audio_select(on);
#else
    (void)on;
#endif
    for (const usb_descriptor_list_t* l = usb_descriptor_list; l->addr; l++)
    {
        // usb_dev.c case 0x0680: strings by their own length, the rest by the list, the configuration by its wTotalLength
        unsigned len = (l->wValue >> 8) == 3 ? l->addr[0] : l->length;
#ifdef FF_USB_AUDIO_TX
        if ((l->wValue >> 8) == 2) len = l->addr[2] | (l->addr[3] << 8);
#endif
        printf("desc %04x %04x len %u:", l->wValue, l->wIndex, len);
        for (unsigned i = 0; i < len; i++) printf(" %02x", l->addr[i]);
        printf("\n");
    }
    // usb_dev.c case 0x0900: ENDPT1..NUM_ENDPOINTS from the table, an endpoint past the table is never written (reset value 0)
    printf("endpoints:");
    for (int i = 1; i <= 8; i++)
    {
        unsigned c = i <= NUM_ENDPOINTS ? usb_endpoint_config_table[i - 1] : 0;
#ifdef FF_USB_AUDIO_TX
        if (i == AUDIO_TX_ENDPOINT && !ff_usb_audio_on) c = 0;
#endif
        printf(" %u=%02x", i, c);
    }
    printf("\n");
#ifdef FF_USB_AUDIO_TX
    printf("requests endpoint bound %d\n", ff_usb_audio_on ? NUM_ENDPOINTS : AUDIO_TX_ENDPOINT - 1);
#else
    printf("requests endpoint bound %d\n", NUM_ENDPOINTS);
#endif
    return 0;
}
