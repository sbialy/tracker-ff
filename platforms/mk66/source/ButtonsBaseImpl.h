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
    DEVHOOK_BUTTONS_PUBLIC // development hooks (devhooks.h, included by Keys.h)

  protected:
    Keys buttonsDriver;
};

#endif /* SOURCE_BUTTONSBASEIMPL_H_ */
