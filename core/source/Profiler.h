#ifndef SOURCE_PROFILER_H_
#define SOURCE_PROFILER_H_
#include "AudioStream.h"
#include "config.h"
#include "stddef.h"
#include "stdint.h"

class Profiler
{
  public:
    static Profiler* getInstance()
    {
        if (instance == nullptr)
        {
            instance = new Profiler;
        }

        return instance;
    }

    struct AudioProcessorUsage
    {
        int minimum;
        int maximum;
        int tempMaximum;
        int average;
        int last;
    };

    void init();

    size_t freeRamBytes();
    size_t maxStackBytes();
    void getAudioProcessingUsageUs(AudioProcessorUsage& usage, bool resetTempMaximum = false);
    void getAudioProcessingUsagePercent(AudioProcessorUsage& usage, bool resetTempMaximum = false);
    void startMeasuringAudio();
    void stopMeasuringAudio();
    void resetMeasuringAudio();
    // Blocks since the last reset that took the whole block time or longer (the same reading as maximum >= 100 %)
    int audioOverruns() const { return overrunBlocks; }

  private:
    static constexpr uint8_t kStackWatermark =
      0xAA; // this value should match the same value from initial code;
    static constexpr float kSamplePeriodUs = (1.0f / 44100.0f) * 1000000.0f;
    static constexpr int kMaxAudioUpdateTime = AUDIO_BLOCK_SAMPLES * kSamplePeriodUs;
    static Profiler* instance;
    int calculatePercentProcessorUsage(int processorUsageUs);

    uint32_t audioUpdateTimer;
    int lastSampleUs;
    int sumOfSamples;
    int samples;
    int minSampleUs;
    int maxSampleUs;
    int tempMaxSampleUs;
    int overrunBlocks = 0;
};

#endif /* SOURCE_PROFILER_H_ */
