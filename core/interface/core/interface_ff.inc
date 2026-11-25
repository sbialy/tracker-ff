// interface.cpp, included where this block was: the plugin host header and the hooks of the page switching and the USB storage mode. The hooks are statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had. Without the features they are empty
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

#ifdef FEATURE_USB_MSC
// USB storage mode off (decision 3 of ref/usb-mass-storage-feasibility.md): what the computer wrote to /Workspace wins. The project in RAM
// was saved when the mode began and could not change since, so it is read back from the card after a remount (a new project when
// /Workspace no longer holds one, as at start)
#define FF_IF_MSC_RELOAD_DECL() bool reloadWorkspace = false
#define FF_IF_MSC_RELOAD_ARM() reloadWorkspace = true
// on the File screen, as when a project is opened there: inputs blocked and the progress popup while it loads, then the pattern editor
// (cProjectEditor::update clears the end status)
#define FF_IF_MSC_RELOAD_RUN()                                                    \
    if (reloadWorkspace)                                                          \
    {                                                                             \
        switchModuleByButton(onScreenModule, interfaceButtonFile, 0);            \
        openStartupProject();                                                     \
        ip->blockAllInputs();                                                     \
    }
// the platform did not lend the card (no card, or the drive is off this session): back out, nothing was lent
#define FF_IF_MSC_REFUSED(usb, state)                                             \
    if ((state) && !(usb)->getClassState(Usb::ClassType::kStorage))               \
    {                                                                             \
        LOGE("USB storage mode refused");                                         \
        ip->unblockAllInputs();                                                   \
        voiceStreamManager.setSuspended(false);                                   \
        newFileManager.setWorkspaceSaveSuspended(false);                          \
        activateInterface();                                                      \
    }
#else
#define FF_IF_MSC_RELOAD_DECL() ((void)0)
#define FF_IF_MSC_RELOAD_ARM() ((void)0)
#define FF_IF_MSC_RELOAD_RUN()
#define FF_IF_MSC_REFUSED(usb, state)
#endif

#ifdef FEATURE_PLUG_HOST
// USB storage mode off: the host may have changed /Plugins/Master, the Inserts page reads every file again
#define FF_IF_PLUG_CARD_CHANGED() plug::cardChanged()
#else
#define FF_IF_PLUG_CARD_CHANGED() ((void)0)
#endif

#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
// switchModuleByButton: Shift + Perform armed the Synth play page (interface_synthplay.inc), the page key opens it
#define FF_IF_SYNTH_PLAY_ENTRY_OR_RETURN(module) \
    if (synthPlayEntry)                           \
    {                                             \
        synthPlayEntry = false;                   \
        deactivateModule(module);                 \
        activateModule(&synthPlay, 0);            \
        return;                                   \
    }
#else
#define FF_IF_SYNTH_PLAY_ENTRY_OR_RETURN(module)
#endif

#ifdef FEATURE_PARTS
// Performance mode swaps tracks into the playing pattern, which is a Part image while Parts play (risk R4)
#define FF_IF_PARTS_PERFORMANCE_OR_RETURN(button)                                                               \
    if ((button) == interfaceButtonPerformance && parts::playing())                                             \
    {                                                                                                           \
        mtPopups.show(0, "A Part is playing", "stop it to use Performance"); /* timed: an info popup would stay up */ \
        return;                                                                                                 \
    }
#else
#define FF_IF_PARTS_PERFORMANCE_OR_RETURN(button)
#endif
