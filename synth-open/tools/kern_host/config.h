// Host stand-in of core/config/config.h for the kern_*_test.cpp tests that build AudioStream.cpp: the interrupt masking and the section
// attributes mean nothing on a host.
#ifndef CONFIG_CONFIG_H_
#define CONFIG_CONFIG_H_

#define DISABLE_GLOBAL_INTERRUPTS() ((void)0)
#define ENABLE_GLOBAL_INTERRUPTS()  ((void)0)
#define DMAMEM
#define AUDIO_CORE_FLASH
#define AUDIO_CORE_RAM

#endif
