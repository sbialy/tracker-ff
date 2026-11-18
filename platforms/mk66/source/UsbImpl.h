#ifndef USBIMPL_H_
#define USBIMPL_H_

#include "Platform.h"
#include "config.h"

class UsbImpl final : public Usb
{
  public:
    virtual void init() override{};
    /**
     * This method should be called whenever any changes in enabled
     * functions are done using SetClassState method to reset USB
     * and allow enumeration of a new set of USB functions
     */
    virtual void reset(void) override{};
    /**
     * For not critical USB endpoints this method should be called
     * on regular basis to handle them in non IRQ context i.e. storage and serial
     */
    virtual void update(void) override{};
    /**
     * This method is responsible for enabling required USB functions.
     *
     * Midi - enabled by default and should be always enabled
     *
     * AudioIn and AudioOut - enabled by default but should be configurable
     * 	from UI and enabled based on NVM settings
     *
     * Storage - disabled by default unless develop mode is enabled. Should
     * 	be configurable from UI on demand
     *
     * Serial - disabled by default unless develop mode is enabled.
     */
#ifdef FEATURE_USB_MSC
    // USB_MSC=1: kStorage lends the card to the computer (usbmsc/msc_drive.cpp)
    virtual void setClassState(Usb::ClassType, bool) override;
    virtual bool getClassState(Usb::ClassType) override;
#else
    virtual void setClassState(Usb::ClassType, bool) override{};
    /**
     * This method retrieves whether certain USB function is enabled
     */
    virtual bool getClassState(Usb::ClassType) override { return false; };
#endif

    virtual bool isAttached() override { return false; };
};

#endif /* USBIMPL_H_ */
