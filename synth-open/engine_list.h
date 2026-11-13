// The engines a SYNTH_OPEN build can host, as an X-macro list. Deliberately no include guard: every user defines
// SYNTH_ENGINE(...) first, includes this file and undefines the macro again.
//
//   SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, PATCH_ENGINE, MINI_ICON, ICON)
//
//   NAME          on-screen name, also the Patches/<NAME>/ folder
//   STORED        the engine's canonical number in project files (strMtValues::synthPart[].synthType). It is the
//                 position in the stock Tracker+/Mini CE list ACD FAT VAP WTFM PERC DIRT CHIP (so a stock PERC part
//                 is 4 and a stock CHIP part is 6 here as well), JU6 (7) and PHZ (8) come after it, then the other OpenTracker 3.x engines
//                 FM4 (9) D-Type (10) CS-Type (11) A-Type (12) T-Type (13) 8-Type (14) Hand-Type (15) B-Type (16) Arc-Type (17), then our own PLK (18), the same
//                 numbers as the editor stores. DIRT (5) has no open
//                 engine, and PERC (4) has one only when it is picked: a project that uses an engine this build lacks
//                 (or a plugin engine that is not on the card) keeps the part and shows it as Missing, only a number
//                 no engine can have (DIRT 5, over 255) falls back to a default part. STORED is not the PATCH_ENGINE number (PERC is 4 stored but 6 in a
//                 .patch, CHIP is 6 stored but 8 in a .patch). The number never depends on which engines are built in
//                 (Synth::kSynthTypes only lists the built ones, in this order)
//   TYPE_ID       polysynth::InstrumentTypeId. JU6 has no Polyend id, it uses its OpenTracker engine id 0x81,
//                 PHZ has none either (the Polyend Synth engine is not in tracker-ce), it uses our own 0x82. The other OpenTracker
//                 engines use their module engine id (FM4 0x80, CS-Type 0x83, A-Type 0x84, T-Type 0x85, 8-Type 0x86, Hand-Type 0x87, B-Type 0x88,
//                 Arc-Type 0x89, PLK 0x8A: our own Karplus-Strong pluck, no Polyend or OpenTracker id exists). D-Type's OpenTracker id 0x82 is PHZ's, so D-Type is stored as 0xA2 here, in the .patch and in the editor
//                 (the module itself still gets 0x82 from its engine, the id is only a key)
//   FACTORY       registry factory, defined in open_cold.cpp from DEF
//   DEF           the engine's polysynth::open::EngineDef (engine_<name>.cpp)
//   PATCH_ENGINE  the number in field 1 of a .patch file: Polyend's InstrumentTypeId (PERC 6 kDrumSynth, CHIP 8) or OpenTracker's engine
//                 id (JU6 0x81, the others as TYPE_ID), PHZ 0x82 (ours: a Polyend Synth PHZ .patch has an unknown id here and is refused)
//   MINI_ICON, ICON  enDisplayBitmaps names (list and editor). The icons are the lettered ACD FAT VAP WTFM PERC CHIP PHZ logos, the
//                 OpenTracker engines borrow the one that fits their character (no new artwork, table in synth-open/README.md)
//
// An entry exists only when the Makefile picked the engine (-DSYNTH_ENGINE_<NAME>, synth-open/engines.mk).

#ifdef SYNTH_ENGINE_ACD
SYNTH_ENGINE(ACD, 0, polysynth::InstrumentTypeId::kSimpleAnalogSynth, createSimpleAnalogSynth, kAcdDef, 1,
             kBitmapMiniACD, kBitmapACD)
#endif
#ifdef SYNTH_ENGINE_FAT
SYNTH_ENGINE(FAT, 1, polysynth::InstrumentTypeId::kFatAnalogSynth, createFatAnalogSynth, kFatDef, 2,
             kBitmapMiniFAT, kBitmapFAT)
#endif
#ifdef SYNTH_ENGINE_VAP
SYNTH_ENGINE(VAP, 2, polysynth::InstrumentTypeId::kVASynth, createVASynth, kVapDef, 4, kBitmapMiniVAP, kBitmapVAP)
#endif
#ifdef SYNTH_ENGINE_WTFM
SYNTH_ENGINE(WTFM, 3, polysynth::InstrumentTypeId::kFMSynth, createFMSynth, kWtfmDef, 5, kBitmapMiniWTFM,
             kBitmapWTFM)
#endif
#ifdef SYNTH_ENGINE_PERC
SYNTH_ENGINE(PERC, 4, polysynth::InstrumentTypeId::kDrumSynth, createDrumSynth, kPercDef, 6, kBitmapMiniPERC,
             kBitmapPERC)
#endif
#ifdef SYNTH_ENGINE_CHIP
SYNTH_ENGINE(CHIP, 6, polysynth::InstrumentTypeId::kChiptuneSynth, createChiptuneSynthOpen, kChipDef, 8,
             kBitmapMiniCHIP, kBitmapCHIP)
#endif
#ifdef SYNTH_ENGINE_JU6
SYNTH_ENGINE(JU6, 7, static_cast<polysynth::InstrumentTypeId>(0x81), createJu6Synth, kJu6Def, 0x81, kBitmapMiniVAP,
             kBitmapVAP)
#endif
#ifdef SYNTH_ENGINE_PHZ
SYNTH_ENGINE(PHZ, 8, static_cast<polysynth::InstrumentTypeId>(0x82), createPhzSynth, kPhzDef, 0x82, kBitmapMiniPHZ,
             kBitmapPHZ)
#endif
#ifdef SYNTH_ENGINE_FM4
SYNTH_ENGINE(FM4, 9, static_cast<polysynth::InstrumentTypeId>(0x80), createFm4Synth, kFm4Def, 0x80,
             kBitmapMiniWTFM, kBitmapWTFM)
#endif
#ifdef SYNTH_ENGINE_DTYPE
SYNTH_ENGINE(DTYPE, 10, static_cast<polysynth::InstrumentTypeId>(0xA2), createDtypeSynth, kDtypeDef, 0xA2,
             kBitmapMiniACD, kBitmapACD)
#endif
#ifdef SYNTH_ENGINE_CSTYPE
SYNTH_ENGINE(CSTYPE, 11, static_cast<polysynth::InstrumentTypeId>(0x83), createCstypeSynth, kCstypeDef, 0x83,
             kBitmapMiniFAT, kBitmapFAT)
#endif
#ifdef SYNTH_ENGINE_ATYPE
SYNTH_ENGINE(ATYPE, 12, static_cast<polysynth::InstrumentTypeId>(0x84), createAtypeSynth, kAtypeDef, 0x84,
             kBitmapMiniACD, kBitmapACD)
#endif
#ifdef SYNTH_ENGINE_TTYPE
SYNTH_ENGINE(TTYPE, 13, static_cast<polysynth::InstrumentTypeId>(0x85), createTtypeSynth, kTtypeDef, 0x85,
             kBitmapMiniPERC, kBitmapPERC)
#endif
#ifdef SYNTH_ENGINE_8TYPE
SYNTH_ENGINE(8TYPE, 14, static_cast<polysynth::InstrumentTypeId>(0x86), createEightSynth, kEightDef, 0x86,
             kBitmapMiniCHIP, kBitmapCHIP)
#endif
#ifdef SYNTH_ENGINE_HANDTYPE
SYNTH_ENGINE(HANDTYPE, 15, static_cast<polysynth::InstrumentTypeId>(0x87), createHandSynth, kHandDef, 0x87,
             kBitmapMiniCHIP, kBitmapCHIP)
#endif
#ifdef SYNTH_ENGINE_BTYPE
SYNTH_ENGINE(BTYPE, 16, static_cast<polysynth::InstrumentTypeId>(0x88), createBtypeSynth, kBtypeDef, 0x88,
             kBitmapMiniCHIP, kBitmapCHIP)
#endif
#ifdef SYNTH_ENGINE_ARCTYPE
SYNTH_ENGINE(ARCTYPE, 17, static_cast<polysynth::InstrumentTypeId>(0x89), createArcSynth, kArcDef, 0x89,
             kBitmapMiniWTFM, kBitmapWTFM)
#endif
#ifdef SYNTH_ENGINE_PLK
SYNTH_ENGINE(PLK, 18, static_cast<polysynth::InstrumentTypeId>(0x8A), createPluckSynth, kPlkDef, 0x8A,
             kBitmapMiniVAP, kBitmapVAP)
#endif
