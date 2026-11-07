#ifndef SOURCE_ENCODERIMPL_H_
#define SOURCE_ENCODERIMPL_H_
#include "Encoder.h"
#include "Platform.h"

class EncoderImpl : Encoder
{
  public:
    virtual void setAcceleration(int acceleration) override;
    virtual void setResolution(int resolution) override;

    void init();
    void update();
#ifdef ENABLE_DEV_CONSOLE
    // USB test console: added to the next reading, so the delta goes the way a turn of the knob does
    void inject(int delta) { injected += delta; }
    bool injectPending() const { return injected != 0; }
#endif

  private:
    uint32_t encTimer;
#ifdef ENABLE_DEV_CONSOLE
    volatile int32_t injected = 0;
#endif
    cEncoder encoderDriver;
};

#endif /* SOURCE_ENCODERIMPL_H_ */
