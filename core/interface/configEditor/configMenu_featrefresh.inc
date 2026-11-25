// configMenu.cpp, included where this block was: the Features page shows the new row set after a State change (FEATURE_FEATURES)
#ifdef FEATURE_FEATURES
// the State row changed the row count, the page shows the new set at the refresh that follows the confirm
void featurePageRefresh()
{
    if (featCurrent >= 0) featurePageFill();
#ifdef FEATURE_FEATURES_FULL
    usbMenuRefresh();
#endif
}
#endif
