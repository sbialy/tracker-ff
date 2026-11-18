// Host shim of teensy_core/cserial.h for fs_host_test.cpp (SdFile.cpp error prints)
#pragma once
#include <stdio.h>
static inline void cppserial_println(const char* s) { printf("serial: %s\n", s); }
