#ifndef SOURCE_RADIOIMPL_H_
#define SOURCE_RADIOIMPL_H_

#include "Platform.h"
#include "Si4703.h"
class RadioImpl : public Radio
{
  public:
    virtual void enableLineInput(bool enabled) override;
    virtual void enable(bool enabled) override;
    virtual bool setFrequency(float freq) override;
    virtual void setSeekCallback(void (*seekCallback)(void)) override;
    virtual void seek(SeekDirection direction) override;
    virtual void updateSeek() override;
    virtual bool updateRDS() override;
    virtual float getFrequency() override;
    virtual float getBaseFrequency() override;
    virtual float getSpacing() override;
    virtual bool isinitialized() override;
    virtual void clearRDS() override;
    virtual void setRegion(Region region) override;
#ifdef FEATURE_RADIO_STEREO
    virtual void setStereo(bool stereo) override;
#endif
    virtual bool isRegionChanged() override;
    virtual char* getStationName() override;

    struct Pins
    {
        int reset;
        int i2sData;
        int i2sClock;
        int interrupt;
        int lineInSourceSelect;
    };

    void init(Pins* pins);

  private:
    Si4703 radioDriver;
    int lineInSourceSelectPin;
};

#endif /* SOURCE_RADIOIMPL_H_ */
