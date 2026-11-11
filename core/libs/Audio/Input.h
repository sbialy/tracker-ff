#ifndef LIBS_AUDIO_INPUT_H_
#define LIBS_AUDIO_INPUT_H_

#include "AudioStream.h"

class AudioInputStream : public AudioStream
{
  public:
    // silentWithoutData: while no buffer was set the stream transmits nothing instead of a block of zeros. Every consumer reads a
    // missing block as silence (AUDIO_NULL_IS_SILENCE), so the result is the same and the whole chain behind it idles.
    explicit AudioInputStream(bool silentWithoutData = false)
      : AudioStream(0, NULL)
      , silentWithoutData(silentWithoutData)
    {
    }

    virtual void update();
    void setInBuffer(void* data);

  private:
    void* data = nullptr;
    bool silentWithoutData;
};

#endif /* LIBS_AUDIO_INPUT_H_ */
