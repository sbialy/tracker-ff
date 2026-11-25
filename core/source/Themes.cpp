#include "Themes.h"

// COLORS
static constexpr uint32_t one_true_red = 0xcd1818;
static constexpr uint32_t one_true_green = 0x30cb3f;
static constexpr uint32_t white = 0xFFFFFF;
static constexpr uint32_t black = 0x000000;
static constexpr uint32_t cyan = 0x3FFFFF;
static constexpr uint32_t dark_cyan = 0x00A4B3;
static constexpr uint32_t red = 0xFF0000;
static constexpr uint32_t green = 0x00FF00;
static constexpr uint32_t orange = 0xD67022;

static constexpr uint32_t instrYellow = 0xfef749;

static constexpr uint32_t grey_08 = 0x080808;
static constexpr uint32_t grey_0A = 0x0A0A0A;
static constexpr uint32_t grey_12 = 0x121212;
static constexpr uint32_t grey_22 = 0x222222;
static constexpr uint32_t grey_32 = 0x323132;
static constexpr uint32_t grey_33 = 0x333333;
static constexpr uint32_t grey_37 = 0x373737;
static constexpr uint32_t grey_3F = 0x3F3F3F;
static constexpr uint32_t grey_44 = 0x444344;
static constexpr uint32_t grey_57 = 0x575757;
static constexpr uint32_t grey_77 = 0x777777;
static constexpr uint32_t grey_90 = 0x909090;

static constexpr uint32_t grey_X08 = 0xC7C7C7;
static constexpr uint32_t grey_X0A = 0xC0C0C0;
static constexpr uint32_t grey_X12 = 0xA8A8A8;
static constexpr uint32_t grey_X22 = 0x7E7E7E;
static constexpr uint32_t grey_X32 = 0x6E6E6E;
static constexpr uint32_t grey_X33 = 0x6D6D6D;
static constexpr uint32_t grey_X37 = 0x696969;
static constexpr uint32_t grey_X3F = 0x616161;
static constexpr uint32_t grey_X44 = 0x5C5C5C;
static constexpr uint32_t grey_X57 = 0x494949;
static constexpr uint32_t grey_X77 = 0x292929;

// clang-format off

constexpr Theme themeDefault =
{
	.backgroundColor = black,
	//------------------------------
                                        // tekst         tło           ramka         tekst2
	.topBarLabelColors =                 { white,        grey_08,      white,        white},
	.songTimerLabelColors =              { white,        grey_08,      one_true_red, white},

    .activeLabelsColors =                { grey_77,      grey_32,      one_true_red, white},
    .inactiveLabelsColors =              { grey_33,      grey_32,      one_true_red, grey_57},
    .activeButtonLabelsColors =          { grey_77,      grey_32,      one_true_red, grey_77},
    .inactiveButtonLabelsColors =        { grey_33,      grey_32,      one_true_red, grey_33},
	.highlightedButtonLabelsColors =     { grey_77,      grey_22,      one_true_red, grey_77},
	.redButtonLabelsColors =             { grey_77,      one_true_red, one_true_red, grey_77},
    .masterEditTrackNameLabelsColors =   { grey_77,      grey_32,      one_true_red, one_true_red},
    .redBlinkLabelColors =               { one_true_red, one_true_red, one_true_red, one_true_red},
    .performActiveTrackLabelsColors =    { grey_77,      one_true_red, one_true_red, white},
    .performMutedTrackLabelsColors =     { black,        one_true_red, one_true_red, black},
	.blankSpaceCoverLabelColors =        { white,        grey_0A,      red,          black},


	.patchEditSynthModelIconColors =     { white,        grey_0A,      red,     black},


	.bottomButtonsBgColors =             { grey_77,      grey_0A,      one_true_red},

	                                    // text          box-bg        box-sel       bg         frame          dots-bg   dots-sel
	.miniBoxMenuColors =                 { white,        grey_22,      grey_32,      black,     one_true_red,  grey_12,  grey_77},

	                                    // text          active img    non-active img  active square  non-active sqr   frame
	.imgGridColors =                     { white,        grey_32,      white,          white,         grey_32,         one_true_red, },

                                        // kolor główny, kolor dod. 1, kolor dod. 2, kontener, tło
	.activeBarColors =                   { white,        red,          green,        grey_08,  grey_0A},
	.inactiveBarColors =                 { grey_22,      red,          green,        grey_08,  grey_0A},

                                         // frameNormal, frameSelect,  listScrollBar,  listBg,   fontNormal, fontSelection,  scrollParCont
	.activeListColors =                  { white,        white,        grey_57,        grey_0A,  white,      white,          black},
	.inactiveListColors =                { grey_22,      white,        grey_57,        grey_0A,  grey_22,    grey_22,        black},
	.projectActiveListColors =           { one_true_red, one_true_red, grey_57,        grey_0A,  white,      one_true_green, black},
	.projectInactiveListColors =         { white,        white,        grey_57,        grey_0A,  white,      one_true_green, black},
    .sampleLoaderActiveListColors =      { one_true_red, one_true_red, grey_57,        grey_0A,  white,      white,          black},
    .sampleLoaderInactiveListColors =    { white,        white,        grey_57,        grey_0A,  white,      white,          black},

	                                    // tekst         kontener      ramka          zaznaczenie  tło
	.editBoxColors =                     { white,        grey_08,      one_true_red,  grey_22,     grey_0A},

    .frameColors =                       { one_true_red,   grey_3F},
    .greenFrameColors =                  { one_true_green, grey_3F},

                                        // klawisze      tekst         ramka
	.keyboardColors =                    { grey_0A,      white,        one_true_red},

	//------------------------------

    .stepPopupTitleLabelColor =          { black,        white,        one_true_red, black},
    .stepPopupListColor =                { white,        black,        grey_57,      grey_0A,  white,    white,        black}, // activeListColors
    .stepPopupBgLabelColor =             { white,        grey_0A,      red , 		 black},
    .stepPopupDescriptionTextBoxColors = { white,        grey_0A,      one_true_red}, // valueLabelColorsEdit

    .notePopupColors =                   { grey_0A,      white,        one_true_red},

										 // text         bg            text 2
	.noteKeyboardColors =                { grey_77,      grey_08,      white},

    .popupHorizontalBarColors =          { grey_22,      grey_22,      white,        grey_12},
                                        // text          bg            shadow
	.popupTextColors =                   { white,        grey_22,      black},

	                                    // tlo           ramka         pasek/tekst
	.popupWithProgressColors =           { grey_22,      grey_22,      white},

	.noSdInfoTextColors =                { white,        white},

	//------------------------------
                                        // 0                1         2         3            4         5         6             7              8
                                        // nazwy tr,        backgr,   nuta,     instr,       FX 1,     FX 2,     nuta nieakt,  instr nieakt,  FX 1 nieakt,
	.patternTrackerColors =              { black,           grey_08,  0x00e8be, instrYellow, 0xbb58f1, 0x57f1ff, 0x003933,     0x453b1b,      0x321f3a,
			                            // 9                10             11         12            13         14         15              16
			                            // FX 2 nieakt,     zaznaczenie,   podziałka, track nieakt, playhead,  txt zazn., podziałka pion, step num
			                               0x183744,        one_true_red,  0x0e0e0e,  0x171717,     black,     white,     black,          grey_57,
                                        // 17               18         19              20
								        // green mode,      unfocused  track aktywnny  trackend
								           one_true_green,  grey_44,   grey_57,        instrYellow},

	.patternFillBgLabelColors =          { white,        black,    red,   white},

                                        // texty        activ bar/block  scroll     tlo        noact text    editmode        scroll bg   playhead/mute     emptytrack   playhead/activ bar
	.songPlayerColors =                  { white,       white,           grey_57,   grey_0A,   grey_33,      one_true_red,   black,      grey_44,          grey_12,     one_true_green},


	.configItemEditorTextLabelColors =   { white,        grey_0A,      red,          black },
    .gridEditorColors =                  { grey_0A,      white,        one_true_red },
    .configConfirmPopUpLabelColors =     { white,        grey_22,      one_true_red, white},


    .sampleLoadMemBarNormalColors =      { white,        one_true_red, one_true_green, grey_08,  grey_0A},
    .sampleLoadMemBarFullColors =        { one_true_red, one_true_red, one_true_red,   grey_08,  grey_0A},


    .perfFxNameTextLabelColors =         { white,        grey_12,      red,             white},
	.perfValueNormalMLabelColors =       { white,        grey_0A,      one_true_green  },
	.perfValueEditMLabelColors =         { white,        grey_0A,      one_true_red    },


	.startScreenColors =                 { white,        white},

	//------------------------------

    .waveformColors =                    { white,        grey_77},
    .waveformPointsColors =              { white,        grey_0A,      one_true_red,    one_true_green},
    .waveformProgressColors =            { white,        one_true_red},
    .granularProgressColors =            { cyan,         grey_22},
    .waveTablePositionColors =           { white,        white},
    .sliceColors =                       { instrYellow,     one_true_red, one_true_green },

	//------------------------------

	.batteryIconColors =                 { grey_90, green, red},

	.defaultTextColors =                 { black,        white,        one_true_red, black},


    // bars colors
    .white = white,
    .red = one_true_red,
	.orange = orange,
	.yellow = instrYellow,
	.green = one_true_green,
    .cyan = cyan,
    .grey_22 = grey_22,

    .bottomScreenShadow = 1,
    .barShadow = 1,
    .patternEditorPlayheadShadow = 1,
    .patternEditorBottomButtonsLines = 1,
    .moreButtonColors =                  { grey_77,      grey_22,      grey_77,      grey_22},
};

// clang-format on

static constexpr uint32_t grey_71 = 0x717171;
static constexpr uint32_t grey_C9CA = 0xC9C9CA;

static constexpr uint32_t background = 0x4B4B4B;

static constexpr uint32_t block = 0x3E3E3E;
static constexpr uint32_t dark_block = 0x303030;

static constexpr uint32_t active_object = 0xC9C9CA;
static constexpr uint32_t inactive_object = 0x303030;

static constexpr uint32_t bright = 0xB9B9BA;

static constexpr uint32_t bright_text = 0xC9C9CA;
static constexpr uint32_t active_text = white;
static constexpr uint32_t inactive_text = 0x303030;
static constexpr uint32_t two_true_red = 0xdd2020;

// clang-format off

constexpr Theme themeWhiteout =
{
	.backgroundColor = background,
	//------------------------------
                                        // tekst          tło            ramka             tekst2
	.topBarLabelColors =                 { black,         bright,        white,            white},
	.songTimerLabelColors =              { black,         bright,        two_true_red,     white},

    .activeLabelsColors =                { active_text,   grey_X32,      two_true_red,     active_text},
    .inactiveLabelsColors =              { inactive_text, grey_X32,      two_true_red,     inactive_text},
    .activeButtonLabelsColors =          { active_text,   grey_X32,      two_true_red,     active_text},
    .inactiveButtonLabelsColors =        { inactive_text, grey_X32,      two_true_red,     inactive_text},
	.highlightedButtonLabelsColors =     { active_text,   grey_X77,      two_true_red,     active_text},
	.redButtonLabelsColors =             { active_text,   two_true_red,  two_true_red,     active_text},
    .masterEditTrackNameLabelsColors =   { active_text,   grey_X32,      two_true_red,     two_true_red},
    .redBlinkLabelColors =               { two_true_red,  two_true_red,  two_true_red,     two_true_red},
    .performActiveTrackLabelsColors =    { active_text,   two_true_red,  two_true_red,     active_text},
    .performMutedTrackLabelsColors =     { active_text,   0xC64E4D,      two_true_red,     active_text},

    .blankSpaceCoverLabelColors =        { active_text,   block,         two_true_red,     active_text},

	.patchEditSynthModelIconColors =     { white,        grey_0A,      red,     black},

                                        // linie         tło            ramka
	.bottomButtonsBgColors =             { grey_71,      background,    two_true_red},

                                        // text          box1-bg       box2-sel     bg       frame           dots-bg     dots-sel
    .miniBoxMenuColors =                 { grey_22,      grey_X22,     grey_X44,    black,   one_true_red,   grey_X77,   grey_X12},

                                        // text          active img    non-active img  active square  non-active sqr   frame
    .imgGridColors =                     { white,        grey_32,      white,          white,         grey_32,         one_true_red, },

                                        // kolor główny,       kolor dod. 1,      kolor dod. 2,          kontener,  tło
	.activeBarColors =                   { active_object,      two_true_red,      one_true_green,        block,     block},
	.inactiveBarColors =                 { inactive_object,    two_true_red,      one_true_green,        block,     block},

	                                     // frameNormal,  frameSelection, listScrollBar,  listBg,   fontNormal,  fontSelection,  scrollParCont
	.activeListColors =                  { active_object, active_object,  grey_X77,       block,    bright_text, bright_text,    grey_X22},
	.inactiveListColors =                { grey_X22,      active_object,  grey_X77,       block,  	0x303030,    0x303030,       grey_X22},
	.projectActiveListColors =           { active_object, two_true_red,   grey_X77,       block,  	bright_text, one_true_green, grey_X22},
	.projectInactiveListColors =         { active_object, active_object,  grey_X77,       block,  	bright_text, one_true_green, grey_X22},
    .sampleLoaderActiveListColors =      { active_object, two_true_red,   grey_X77,       block,  	bright_text, bright_text,    grey_X22},
    .sampleLoaderInactiveListColors =    { active_object, active_object,  grey_X77,       block,  	bright_text, bright_text,    grey_X22},

	                                    // tekst          kontener   ramka             zaznaczenie        tło
	.editBoxColors =                     { active_text,   block,     two_true_red,     background,        background},

    .frameColors =                       { two_true_red,           grey_X3F},
    .greenFrameColors =                  { one_true_green,         grey_X3F},

                                        // klawisze       tekst              ramka
	.keyboardColors =                    { block,         active_text,     two_true_red},

	//------------------------------

    .stepPopupTitleLabelColor =          { active_text,   background,   two_true_red,    black},
                                        // listItemFr,    listItemFrBg, listScrollBar,   listBg,    fontList,        select,        scrollParCont
    .stepPopupListColor =                { active_object, background,   grey_X77,        block,     active_text,     active_text,   grey_X22},
    .stepPopupBgLabelColor =             { active_text,   block,        two_true_red,    black},
    .stepPopupDescriptionTextBoxColors = { active_text,   block,        two_true_red},

    .notePopupColors =                   { block,         active_text,  two_true_red},

	 	 	 	 	 	 	 	 	 	 // text         bg             text 2
    .noteKeyboardColors =                { grey_C9CA,   background,     active_text},

    .popupHorizontalBarColors =          { grey_X22,      background,   active_object,   block},
    .popupTextColors =                   { active_text,   background,   two_true_red},

                                        // tlo            ramka          pasek/tekst
	.popupWithProgressColors =           { grey_X22,      background,      active_text},

	.noSdInfoTextColors =                { active_text, active_text},

	//------------------------------
                                        // 0            1           2            3              4             5              6               7               8
                                        // nazwy tr,    backgr,     nuta,        instr,         FX 1,         FX 2,          nuta nieakt,    instr nieakt,   FX 1 nieakt,
	.patternTrackerColors =              { active_text, block,      active_text, active_text,   active_text,  active_text,   active_text,    active_text,    active_text,
			                            // 9            10             11          12             13                14         15              16
			                            // FX 2 nieakt, zaznaczenie,   podziałka,  track nieakt,  playhead,         txt zazn., podziałka pion  step num
			                               active_text, two_true_red,  0x454545,   inactive_text, inactive_object,  black,     grey_71,        active_text,
	                                    // 17               18         19                  20
								        // green mode       white mode     track aktywnny  trackend
								           one_true_green,  active_text,   dark_block,     instrYellow },

	.patternFillBgLabelColors =          { black,        background,        two_true_red,    black},

	                                    // texty         activ bar/block  scroll      tlo         noact text       editmode        scroll bg   playhead/mute      emptytrack  playhead/activ bar
	.songPlayerColors =                  { active_text,  active_object,   grey_X77,   background, inactive_object, two_true_red,   grey_X22,   inactive_object,   block,      one_true_green},


	.configItemEditorTextLabelColors =   { active_text,  grey_X0A,      two_true_red,      active_text},
	.gridEditorColors =                  { grey_X0A,     black,         two_true_red},
    .configConfirmPopUpLabelColors =     { active_text,  grey_X22,      two_true_red,      black},

                                        // kolor główny,    kolor dod. 1,    kolor dod. 2,    kontener,    tło
    .sampleLoadMemBarNormalColors =      { active_object,   two_true_red,    one_true_green,  block,       block},
    .sampleLoadMemBarFullColors =        { two_true_red,    two_true_red,    two_true_red,    block,       block},


    .perfFxNameTextLabelColors =         { bright_text,      grey_X12,      two_true_red,        black},
    .perfValueNormalMLabelColors =       { bright_text,      block,         one_true_green       },
    .perfValueEditMLabelColors =         { bright_text,      block,         two_true_red         },

	.startScreenColors =                 { active_text,      active_object},

	//------------------------------

    .waveformColors =                    { active_object,    grey_77},
    .waveformPointsColors =              { white,            grey_0A,       two_true_red,        one_true_green},
    .waveformProgressColors =            { black,            two_true_red},
    .granularProgressColors =            { cyan,             grey_X22},
    .waveTablePositionColors =           { black,            black},
    .sliceColors =                       { instrYellow,         two_true_red,  one_true_green  },

	//------------------------------

	.batteryIconColors =                 {grey_90, green, red},

	.defaultTextColors =                 { active_text,        black,         two_true_red,         active_text},


    // bars colors
    .white = active_object,
    .red = two_true_red,
	.orange = orange,
	.yellow = instrYellow,
	.green = one_true_green,
    .cyan = dark_cyan,
    .grey_22 = inactive_object,

    .bottomScreenShadow = 0,
    .barShadow = 0,
    .patternEditorPlayheadShadow = 1,
    .patternEditorBottomButtonsLines = 1,
    .moreButtonColors =                  { active_text,   grey_X32,      active_text,      grey_X32},
};

// clang-format on

static constexpr uint32_t patt_text = 0xD0D0D0;
static constexpr uint32_t patt_text2 = 0x454545;

// clang-format off

constexpr Theme themeAnniversary=
{
	.backgroundColor = black,
	//------------------------------
                                        // tekst          tło            ramka             tekst2
	.topBarLabelColors =                 { white,         black,         white,            white},
	.songTimerLabelColors =              { white,         black,         two_true_red,     white},

    .activeLabelsColors =                { active_text,   grey_X32,      two_true_red,     active_text},
    .inactiveLabelsColors =              { inactive_text, grey_X32,      two_true_red,     inactive_text},
    .activeButtonLabelsColors =          { active_text,   grey_X32,      two_true_red,     active_text},
    .inactiveButtonLabelsColors =        { inactive_text, grey_X32,      two_true_red,     inactive_text},
	.highlightedButtonLabelsColors =     { active_text,   grey_X77,      two_true_red,     active_text},
	.redButtonLabelsColors =             { active_text,   two_true_red,  two_true_red,     active_text},
    .masterEditTrackNameLabelsColors =   { active_text,   grey_X32,      two_true_red,     two_true_red},
    .redBlinkLabelColors =               { two_true_red,  two_true_red,  two_true_red,     two_true_red},
    .performActiveTrackLabelsColors =    { active_text,   two_true_red,  two_true_red,     active_text},
    .performMutedTrackLabelsColors =     { active_text,   0xC64E4D,      two_true_red,     active_text},

    .blankSpaceCoverLabelColors =        { active_text,   black,         two_true_red,     active_text},

	.patchEditSynthModelIconColors =     { white,        grey_0A,      red,     black},

                                        // linie         tło            ramka
	.bottomButtonsBgColors =             { black,        black,         two_true_red},

                                        // text          box-bg        box-sel       bg         frame          dots-bg   dots-sel
    .miniBoxMenuColors =                 { white,        grey_22,      grey_32,      black,     one_true_red,  grey_12,  grey_77},

                                        // text          active img    non-active img  active square  non-active sqr   frame
    .imgGridColors =                     { white,        grey_32,      white,          white,         grey_32,         one_true_red, },

                                        // kolor główny,       kolor dod. 1,      kolor dod. 2,          kontener,  tło
	.activeBarColors =                   { active_object,      two_true_red,      one_true_green,        black,     black},
	.inactiveBarColors =                 { inactive_object,    two_true_red,      one_true_green,        black,     black},

	                                     // frameNormal,  frameSelection, listScrollBar, listBg,   fontNormal,   fontSelection,  scrollParCont
	.activeListColors =                  { active_object, active_object,  grey_57,       black,    bright_text,  bright_text,    black},
	.inactiveListColors =                { grey_X22,      active_object,  grey_57,       black,  	0x303030,    0x303030,       black},
	.projectActiveListColors =           { active_object, two_true_red,   grey_57,       black,  	bright_text, one_true_green, black},
	.projectInactiveListColors =         { active_object, active_object,  grey_57,       black,  	bright_text, one_true_green, black},
    .sampleLoaderActiveListColors =      { active_object, two_true_red,   grey_57,       black,  	bright_text, bright_text,    black},
    .sampleLoaderInactiveListColors =    { active_object, active_object,  grey_57,       black,  	bright_text, bright_text,    black},

	                                    // tekst          kontener   ramka             zaznaczenie        tło
	.editBoxColors =                     { active_text,   black,     two_true_red,     background,        background},

    .frameColors =                       { two_true_red,           grey_X3F},
    .greenFrameColors =                  { one_true_green,         grey_X3F},

                                        // klawisze       tekst              ramka
	.keyboardColors =                    { block,         active_text,     two_true_red},

	//------------------------------

    .stepPopupTitleLabelColor =          { active_text,   black,   two_true_red,         black},
                                        // listItemFr,    listItemFrBg, listScrollBar,   listBg,    fontList,        select,        scrollParCont
    .stepPopupListColor =                { active_object, black,        grey_57,         black,     active_text,     active_text,   black},
    .stepPopupBgLabelColor =             { active_text,   black,        two_true_red,    black},
    .stepPopupDescriptionTextBoxColors = { active_text,   black,        two_true_red},

    .notePopupColors =                   { black,         active_text, two_true_red},
	                                     // text          bg            text 2
    .noteKeyboardColors =                { active_text,   black,       active_text},

    .popupHorizontalBarColors =          { grey_22,       grey_22,      white,        grey_12},

    .popupTextColors =                   { active_text,   background,   two_true_red},

                                        // tlo           ramka         pasek/tekst
    .popupWithProgressColors =           { grey_22,      grey_22,      white},

	.noSdInfoTextColors =                { active_text, active_text},

	//------------------------------
                                        // 0            1           2            3             4             5              6               7               8
                                        // nazwy tr,    backgr,     nuta,        instr,        FX 1,         FX 2,          nuta nieakt,    instr nieakt,   FX 1 nieakt,
	.patternTrackerColors =              { patt_text,   grey_08,    patt_text,   patt_text,    patt_text,     patt_text,    patt_text2,     patt_text2,     patt_text2,
			                            // 9            10             11          12             13                14         15              16
			                            // FX 2 nieakt,   zaznaczenie,   podziałka,  track nieakt,  playhead,       txt zazn., podziałka pion  step num
			                               patt_text2,    two_true_red,  0x0e0e0e,   0x171717,      grey_22,        black,     black,          bright_text,
	                                    // 17               18         19                  20
								        // green mode       white mode     track aktywnny  trackend
								           one_true_green,  active_text,   dark_block,     instrYellow },

	.patternFillBgLabelColors =          { black,        black,        two_true_red,    black},

                                        // texty        activ bar/block  scroll     tlo        noact text    editmode        scroll bg   playhead/mute     emptytrack   playhead/activ bar
    .songPlayerColors =                  { white,       white,           grey_57,   black,     grey_33,      one_true_red,   black,      grey_44,          grey_12,     one_true_green},


	.configItemEditorTextLabelColors =   { active_text,  grey_X0A,      two_true_red,      active_text},
	.gridEditorColors =                  { grey_X0A,     black,         two_true_red},
    .configConfirmPopUpLabelColors =     { active_text,  grey_X22,      two_true_red,      black},

                                        // kolor główny,    kolor dod. 1,    kolor dod. 2,    kontener,    tło
    .sampleLoadMemBarNormalColors =      { active_object,   two_true_red,    one_true_green,  black,       black},
    .sampleLoadMemBarFullColors =        { two_true_red,    two_true_red,    two_true_red,    black,       black},


    .perfFxNameTextLabelColors =         { bright_text,      grey_X12,      two_true_red,        black},
    .perfValueNormalMLabelColors =       { bright_text,      black,         one_true_green       },
    .perfValueEditMLabelColors =         { bright_text,      black,         two_true_red         },

	.startScreenColors =                 { active_text,      active_object},

	//------------------------------

    .waveformColors =                    { active_object,    grey_77},
    .waveformPointsColors =              { white,            grey_0A,       two_true_red,        one_true_green},
    .waveformProgressColors =            { black,            two_true_red},
    .granularProgressColors =            { cyan,             grey_X22},
    .waveTablePositionColors =           { black,            black},
    .sliceColors =                       { instrYellow,         two_true_red,  one_true_green  },

	//------------------------------

	.batteryIconColors =                 { grey_90, white, red},

	.defaultTextColors =                 { active_text,        black,         two_true_red,         active_text},


    // bars colors
    .white = active_object,
    .red = two_true_red,
	.orange = orange,
	.yellow = instrYellow,
	.green = one_true_green,
    .cyan = dark_cyan,
    .grey_22 = inactive_object,

    .bottomScreenShadow = 0,
    .barShadow = 0,
    .patternEditorPlayheadShadow = 0,
    .patternEditorBottomButtonsLines = 0,
    .moreButtonColors =                  { active_text,   grey_X32,      active_text,      grey_X32},
};

// Zero filled (.bss), not a copy of themeDefault in .data: it is only written by setCustomTheme and parseThemeJson (which starts from
// themeDefault) before the display points at it, so the initial value was 1,124 B of flash nobody read
Theme themeCustom{};

// clang-format on
