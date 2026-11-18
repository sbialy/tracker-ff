#ifndef INTERFACE_CORE_INTERFACEMENUPOPUP_H_
#define INTERFACE_CORE_INTERFACEMENUPOPUP_H_

#include "core/modulesBase.h"

#include <stdint.h>

extern const uint8_t kMenuModulesCount;
extern const char* const kModuleNamesList[];

#ifdef TRACKER_MINI

class InterfaceMenuPopup
{
  public:
    void initPopupsDisplayControls();
    void refreshColors();
    void showModulePopup(ModuleBase* sourceModule, void (*switchModuleCallback)(uint8_t) = nullptr);
    void hideModulePopup(ModuleBase* modulePtr);
    inline bool isPopupVisible() { return isVisible; }

    void changeModulePopupValue(int16_t value, uint8_t dir = 0);
    void setModulePopupValue(int16_t value);
    void changeModuleMode();

    struct ModuleModes
    {
        uint8_t count;
        uint32_t* options;
        strBoxMenuData::Modes modes;
    };

    struct ModuleRef
    {
        ModuleBase* module;
        uint8_t button;
        uint32_t defaultOption;
        ModuleModes* modes;
    };
    static const ModuleRef modules[];

  private:
    static constexpr uint16_t kMenuWidth = 440;
    static constexpr uint16_t kMenuHeight = 330;
    static constexpr uint16_t kMenuColumnsCount = 4;
    static constexpr uint16_t kMenuRowsCount = 3;

    void setModulePopupFunct();
    void handleFailedSwitching();

    hControl boxMenu;
    strBoxMenuData boxMenuData;

    uint8_t selectedBox = 12;
    uint8_t selectedModuleOnShow;

    uint8_t menuFunctions[kMenuRowsCount * kMenuColumnsCount];
    const char* modulesMenuTexts[kMenuRowsCount * kMenuColumnsCount];
    strBoxMenuData::Modes* modulesMenuModes[kMenuRowsCount * kMenuColumnsCount];

    ModuleBase* sourceModule;
    void (*switchModuleCallback)(uint8_t) = nullptr;
    bool isVisible = false;
};

#else

class InterfaceMenuPopup
{
  public:
    void initPopupsDisplayControls(){};
    void refreshColors(){};
    void showModulePopup(ModuleBase* /*sourceModule*/,
                         void (*/*switchModuleCallback*/)(uint8_t) = nullptr){};
    void hideModulePopup(ModuleBase* /*modulePtr*/){};
    inline bool isPopupVisible() { return false; }
};

#endif

extern InterfaceMenuPopup mtMenuPopup;

#endif /* INTERFACE_CORE_INTERFACEMENUPOPUP_H_ */
