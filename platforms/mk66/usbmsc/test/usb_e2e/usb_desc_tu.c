// The real teensy_core/usb_desc.c for the host harness (as test/usb_desc_dump.c does): Mach-O has no alias attribute, so the three weak
// aliases become weak declarations, defined below.
#include "usb_dev.h"
#define alias(x) weak
#include "usb_desc.c"
#undef alias

struct usb_string_descriptor_struct usb_string_manufacturer_name = { 2 + MANUFACTURER_NAME_LEN * 2, 3, MANUFACTURER_NAME };
struct usb_string_descriptor_struct usb_string_product_name = { 2 + PRODUCT_NAME_LEN * 2, 3, PRODUCT_NAME };
struct usb_string_descriptor_struct usb_string_serial_number = { 12, 3, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };

int kinetis_hsrun_disable(void) { return 0; }
int kinetis_hsrun_enable(void) { return 0; }
char* ultoa(unsigned long v, char* buf, int radix) { (void)v; (void)radix; buf[0] = 0; return buf; }
