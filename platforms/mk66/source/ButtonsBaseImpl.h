#ifndef SOURCE_BUTTONSBASEIMPL_H_
#define SOURCE_BUTTONSBASEIMPL_H_

#include "Keys.h"
#include "Platform.h"

class ButtonsBaseImpl : public SwitchGroup
{
  public:
    virtual void setHoldTime(int timeMs) override;
    virtual bool isButtonPressed(int buttonIdx) override;
    virtual void setActionCallback(ActionType action, void (*callback)(uint8_t)) override;
    void disableInterrupts();
    bool update();
#ifdef ENABLE_DEV_CONSOLE
    bool inject(uint8_t button, bool down) { return buttonsDriver.inject(button, down); }
    bool injectPending() const { return buttonsDriver.injectPending(); }
#endif

  protected:
    Keys buttonsDriver;
};

#endif /* SOURCE_BUTTONSBASEIMPL_H_ */
