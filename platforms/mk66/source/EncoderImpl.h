#ifndef SOURCE_ENCODERIMPL_H_
#define SOURCE_ENCODERIMPL_H_
#include "Encoder.h"
#include "Platform.h"
#include "devhooks.h"

class EncoderImpl : Encoder
{
  public:
    virtual void setAcceleration(int acceleration) override;
    virtual void setResolution(int resolution) override;

    void init();
    void update();
    DEVHOOK_ENCODER_PUBLIC // development hooks (devhooks.h): the injected encoder turns

  private:
    uint32_t encTimer;
    DEVHOOK_ENCODER_PRIVATE
    cEncoder encoderDriver;
};

#endif /* SOURCE_ENCODERIMPL_H_ */
