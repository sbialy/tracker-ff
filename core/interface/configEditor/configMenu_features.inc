// configMenu.cpp, included where this block was: Config > Features, one page per feature (FEATURE_FEATURES, core/source/features.h)
#ifdef FEATURE_FEATURES
//==================================================================================================================================================================
// Features (core/source/features.h): Config > Features lists every feature with its state, Enter opens its page
namespace
{
const char* const kFeatOnOff[] = { "Off", "On" };
strItemTypeLabel featDummySetup{ nullptr };
cMenuItem featRows[3 + features::kMaxParams] = {
#define FEAT_ROW cMenuItem(menuFeaturePage, "", nullptr, menuTypeItemLabel, &featDummySetup)
    FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW,
#undef FEAT_ROW
};
static_assert(3 + features::kMaxParams == 11, "featRows initialisers");
const strItemTypeActionButton featRestartSetup{ features::restartNow, nullptr };
uint8_t featRowFeature[features::kMax];   // Features list row -> feature (a hidden feature has no row)
strItemTypeListTextWithAction featStateSetup{ nullptr, 2, kFeatOnOff, features::changed };
strItemTypeListTextWithAction featEnumSetup[features::kMaxParams];
strItemTypeListValues featIntSetup[features::kMaxParams];
const char* featStatusText = "";
strItemTypeLabelPtr featStatusSetup{ &featStatusText };
int featCurrent = -1;

void featurePageFill()
{
    const int i = featCurrent;
    const feat_desc* d = features::desc(i);
    if (!d) return;
    featStateSetup.value = features::onRef(i);
    featRows[0].rebind("State", d->help ? d->help : d->name, menuTypeItemListTextWithAction, &featStateSetup);
    uint8_t row = 1;
    if (features::on(i))
    {
        for (uint8_t p = 0; p < d->paramCount; p++, row++)
        {
            const feat_param& q = d->params[p];
            if (q.restart & FEAT_PR_ELSEWHERE) { row--; continue; }   // another page edits it (Config > USB)
            if (q.type == FEAT_P_ENUM)
            {
                featEnumSetup[p] = { features::valueRef(i, p), q.enumCount, q.enumText, features::changed };
                featRows[row].rebind(q.name, q.help ? q.help : (d->help ? d->help : d->name), menuTypeItemListTextWithAction, &featEnumSetup[p]);
            }
            else
            {
                featIntSetup[p] = { features::valueRef(i, p), q.min, q.max, 1, uint8_t(q.shown) };
                featRows[row].rebind(q.name, q.help ? q.help : (d->help ? d->help : d->name), menuTypeItemListValues, &featIntSetup[p]);
            }
        }
    }
    featStatusText = features::rowValue(i);
    featRows[row].rebind("Status", d->help ? d->help : d->name, menuTypeItemLabelPtr, &featStatusSetup);
    if (features::restartPending(i))
    {
        // decision D9: the power off of the power button, ending in a restart (the Workspace and the files are saved first)
        row++;
        featRows[row].rebind("Restart now", "Saves the project and restarts the unit, so the change takes effect.",
                             menuTypeItemActionButton, &featRestartSetup);
    }
    for (uint8_t r = 0; r <= row; r++)
    {
        menuFeaturePage.setChild(r, &featRows[r], featRows[r].getName()); // the group lists its own name table
    }
    menuFeaturePage.setCount(row + 1);
}

void featurePageEnter(uint8_t row)
{
    featCurrent = featRowFeature[row < features::kMax ? row : 0];
    featurePageFill();
}

const char* featureRowValue(uint8_t row)
{
    return features::rowValue(featRowFeature[row < features::kMax ? row : 0]);
}
} // namespace

void features::menuBuild()
{
    const int n = features::count();
    uint8_t rows = 0;
    for (int i = 0; i < n; i++)
    {
        if (!features::listed(i)) continue;
        featRowFeature[rows] = uint8_t(i);
        menuFeatures.setChild(rows++, &menuFeaturePage, features::desc(i)->name);
    }
    menuFeatures.setCount(rows);
    menuFeatures.childValue = featureRowValue;
    menuFeatures.onEnter = featurePageEnter;
}
#endif   // FEATURE_FEATURES
