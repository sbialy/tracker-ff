// configMenu.cpp, included where this block was: the Features menu group and its feature page (FEATURE_FEATURES)
#ifdef FEATURE_FEATURES
cMenuGroup menuFeatures(menuBase, "Features", features::kMax);
// one page for every feature: State, up to kMaxParams params, Status, Restart now. Re-pointed in featurePageEnter
cMenuGroup menuFeaturePage(menuFeatures, "Feature", 3 + features::kMaxParams);
#endif
