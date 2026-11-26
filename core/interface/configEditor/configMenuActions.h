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
void openProjectPageAction();   // Config > Project Settings > Open the page: the Project Settings page of the File screen (F7)
void setRandomSeedValue();      // Project Settings > Random seed: the project changed
void rerollRandomSeedAction();  // Project Settings > Reroll seed
#ifdef FEATURE_FF_CORE
// Project Settings page (opened from the File screen, kOpenProjectSettings)
extern uint8_t projFxSlots, projMacroMode, projStrumVolume, projMpeOut;
extern uint8_t projTuneN, projTunePeriod, projTuneRoot, projTuneHz;
extern uint8_t projPerH, projPerC, projPerD, projPerM, projHzH, projHzU, projHzD, projHzE;
void setProjTune();                // Project Settings > Tuning rows
extern uint8_t projTuneFile;       // the Scale file row: 0 EDO, then the /Tunings files
void setProjTuneFile();
extern uint8_t projTuneNames;      // Note names: 0 Degrees, 1 Ups+downs
void setProjTuneNames();
void tuneFileListSync();           // the row list as the card scan left it (configMenu.cpp)
#ifndef PLATFORM_IMX   // stereo pairs are MK66 only (IMX plays a stereo .pti natively)
extern uint8_t projPair[4];   // Stereo pairs: 1 = pair k linked (core/source/stereoPairs.h)
#endif
void projectPageSync();
void setProjFxSlots();
void setProjMacroMode();
void setProjMpeOut();
void setProjStrumVolume();
#ifndef PLATFORM_IMX
void setProjPair();
#endif
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
void midiMenuSync(); // Config > MIDI lists no "Track N in channel" row for the follower of a linked stereo pair
void usbMenuRefresh(); // Config > USB follows Features (USB audio, USB drive)

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_ */
