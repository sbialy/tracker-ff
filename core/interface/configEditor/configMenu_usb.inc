// configMenu.cpp, included where this block was: Config > USB of the original Tracker (FEATURE_FEATURES_FULL)
#ifdef FEATURE_FEATURES_FULL
// Config > USB (the original). The entry is listed while USB audio or USB drive is on in Features, a change shows at once (no restart).
// Rows: Storage (drive on), the rows of USB audio that its page leaves out (FEAT_PR_ELSEWHERE: Audio mode, FX in 8+3), Restart now
// (a mode change takes effect at the next start). The values are the feature's own bytes, so the .cfg files and EEPROM byte 7 stay as they were
namespace
{
constexpr uint8_t kUsbParamRows = 2;
#define USB_ROW cMenuItem(menuUsb, "", nullptr, menuTypeItemLabel, &featDummySetup)
cMenuItem usbRows[kUsbParamRows + 1] = { USB_ROW, USB_ROW, USB_ROW };   // + Restart now
#undef USB_ROW
strItemTypeListTextWithAction usbEnumSetup[kUsbParamRows];
bool usbHidden;
uint8_t usbHome = 0xFF;   // the slot of the entry in the base list
}

static uint8_t usbMenuSync()
{
    const int a = features::find("usbaudio");
    const int dr = features::find("usbdisk");
    uint8_t n = 0;
#ifdef FEATURE_USB_MSC
    if (features::on(dr)) menuUsb.setChild(n++, &melUsbStorage, melUsbStorage.getName());
#endif
    if (features::on(a))
    {
        const feat_desc* d = features::desc(a);
        uint8_t r = 0;
        for (uint8_t p = 0; p < d->paramCount && r < kUsbParamRows; p++)
        {
            const feat_param& q = d->params[p];
            if (!(q.restart & FEAT_PR_ELSEWHERE) || q.type != FEAT_P_ENUM) continue;
            usbEnumSetup[r] = { features::valueRef(a, p), q.enumCount, q.enumText, features::changed };
            usbRows[r].rebind(q.name, q.help ? q.help : d->help, menuTypeItemListTextWithAction, &usbEnumSetup[r]);
            menuUsb.setChild(n++, &usbRows[r], usbRows[r].getName());
            r++;
        }
        if (features::restartPending(a))
        {
            usbRows[kUsbParamRows].rebind("Restart now", "Saves the project and restarts the unit, so the change takes effect.",
                                          menuTypeItemActionButton, &featRestartSetup);
            menuUsb.setChild(n++, &usbRows[kUsbParamRows], usbRows[kUsbParamRows].getName());
        }
    }
    menuUsb.setCount(n);
    if (usbHome == 0xFF) usbHome = uint8_t(menuUsb.getSlot());
    const bool hide = !features::on(a) && !features::on(dr);
    if (hide != usbHidden)
    {
        constexpr uint8_t last = baseMenuGroupCount - 1 - snapGroupCount;   // the page group of Project Settings stays the last
        if (hide) menuBase.moveChild(usbHome, last);
        else menuBase.moveChild(last, usbHome);
        usbHidden = hide;
    }
    return hide ? 1 : 0;
}

// the USB entry and its rows follow USB audio and USB drive (a change in Features, Reset config)
void usbMenuRefresh()
{
    menuBase.setCount(baseMenuGroupCount - snapGroupCount - usbMenuSync());
}
#endif   // FEATURE_FEATURES_FULL
