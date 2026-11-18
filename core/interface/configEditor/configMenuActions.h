#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_

void restartConfigAllActions();
void initConfigValuesAfterReadConfig();

void setRadioRegion();
void setDisplayBrightness();
void setDisplayTheme();
void setDisplayFont();
void refreshThemeAndFontMenuOptions();
void setPadsBrightness();
void setLineInOutType();

void setPadBoardValues();
void setScaleSnap();
void setAntialiasingEnable();
void setLimiterMode();
void setSendsMode();
void setHeadroomMode();
void setSnapToZeroMode();
void setRandomSeedValue();      // Project Settings > Random seed: the project changed
void rerollRandomSeedAction();  // Project Settings > Reroll seed
#ifdef FEATURE_PLUG_HOST
// Project Settings page (opened from the File screen, kOpenProjectSettings)
extern uint8_t projFxSlots, projMacroMode, projStrumVolume;
void projectPageSync();
void setProjFxSlots();
void setProjMacroMode();
void setProjStrumVolume();
#endif

void setMetronomValues();
void setSlaveClockCompValue();

void openCreditsAction();

void startFileTransferMode();

void openManual1Action();
void openManual2Action();
void openManual3Action();

void setFunctionKey1();
void setFunctionKey2();
void setFunctionKey3();
void setFunctionKey4();
void setFunctionKey5();
void setFunctionKey6();
void setFunctionKey7();
void setFunctionKey8();

void resetConfigShowPopup();
void usbStorageEnter();
void usbAudioChange();
void usbMenuRefresh(); // Config > USB follows Features (USB audio, USB drive)

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_ */
