#pragma once

// Feature descriptor (ref/features-design.md): one shape for a built-in feature (compiled into the image) and a dynamic one
// (an extension module on the card hands the same struct to the image from its entry). Plain C layout, no macros, so a module
// can include it. Values are bytes: an enum index, or a number min..max shown as value + shown (the stock config list types).

#include <stdint.h>

#define FEAT_ABI 1u

enum : uint8_t
{
    FEAT_P_ENUM = 0,   // enumCount texts
    FEAT_P_INT = 1,    // min..max, the row shows value + shown
};

enum : uint16_t
{
    FEAT_F_RESTART = 1u,      // the on/off switch takes effect at the next start
    FEAT_F_BOOTFLAG = 2u,     // the on/off state is also kept in the EEPROM (read before the card is up, e.g. the USB descriptors)
    FEAT_F_DEFAULT_ON = 4u,   // on when the card has no file for it
    FEAT_F_DYNAMIC = 8u,      // provided by a module on the card (listed as missing or refused when it did not load)
};

struct feat_param
{
    const char* key;    // file key, [a-z0-9_], never renamed (a renamed key reads as its default)
    const char* name;   // row text
    uint8_t type;
    uint8_t min, max, def;
    int8_t shown;
    uint8_t enumCount;
    const char* const* enumText;
    uint8_t restart;    // 1: a change takes effect at the next start
    const char* help;   // description box text, null: the feature's help
};

struct feat_desc
{
    uint32_t size;      // sizeof(feat_desc) of the provider, checked before any other field is read
    uint16_t abi;       // FEAT_ABI
    uint16_t version;   // of the feature, written to its file
    const char* id;     // file name /Features/<id>.cfg, 1 to 8 of [a-z0-9]
    const char* name;   // list text
    const char* help;   // description box text of the State row
    uint16_t flags;
    uint8_t paramCount;
    const feat_param* params;
    uint16_t stateMax;  // bytes of opaque state kept in the file (hex), 0 = none
    // lifecycle, main thread, every pointer may be null
    void (*boot)(uint8_t on, const uint8_t* values);                  // once at start, after the file was read
    void (*apply)(uint8_t on, const uint8_t* values, uint8_t which);  // a change from the page (which = param, 0xFF = on/off)
    const char* (*status)(uint8_t on);                                // page status row and list value, null = "On" / "Off"
    int (*saveState)(uint8_t* buf, int cap);
    void (*loadState)(const uint8_t* buf, int n);
    // FEAT_F_BOOTFLAG: the state this session really started with (the EEPROM mirror read before the card), null = the file's.
    // A file that differs is re-synced to the mirror at start (apply) and the row reads Restart.
    uint8_t (*running)(void);
};
