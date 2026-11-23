#ifndef LIBS_AUDIO_INPUT_H_
#define LIBS_AUDIO_INPUT_H_

#include "AudioStream.h"

class AudioInputStream : public AudioStream
{
  public:
    // silentWithoutData: while no buffer was set the stream transmits nothing instead of a block of zeros. Every consumer reads a
    // missing block as silence (AUDIO_NULL_IS_SILENCE), so the result is the same and the whole chain behind it idles.
    // lineInRing (MK66 line in only): setInBuffer copies the finished DMA half into the line in ring (LineInRing.h) and update()
    // takes exactly one block per render from it, instead of reading whatever the stored pointer shows when the render runs
    explicit AudioInputStream(bool silentWithoutData = false, bool lineInRing = false)
      : AudioStream(0, NULL)
      , silentWithoutData(silentWithoutData)
      , lineInRing(lineInRing)
    {
    }

    virtual void update();
    void setInBuffer(void* data);

  private:
    void* data = nullptr;
    bool silentWithoutData;
    bool lineInRing;
};

// line in block counters since power on (LineInRing.h), for the console and the developer Config readout. ff_linein[0..3]:
// dup, skip, under, drop. All zero on a platform without the ring.
extern "C" void ff_linein_counts(uint32_t out[4]);

#endif /* LIBS_AUDIO_INPUT_H_ */
