// Host stand-in of core/config/config.h for the kern_*_test.cpp tests that build AudioStream.cpp: the interrupt masking and the section
// attributes mean nothing on a host.
#ifndef CONFIG_CONFIG_H_
#define CONFIG_CONFIG_H_

#define DISABLE_GLOBAL_INTERRUPTS() ((void)0)
#define ENABLE_GLOBAL_INTERRUPTS()  ((void)0)
#define DMAMEM
#define AUDIO_CORE_FLASH
#define AUDIO_CORE_RAM
#define AUDIO_CORE_RAM_COLD

// the number of synth parts, as core/config/config.h (a test that builds with -DFEATURE_SYNTH has the 3 part mux)
#ifdef FEATURE_SYNTH
static constexpr int kSynthParts = 3;
#endif

#endif
