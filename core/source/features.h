#pragma once

// Features registry (ref/features-design.md, FEATURE_EXT_HOOKS builds): built-in descriptors plus the ones a module hands in,
// one /Features/<id>.cfg per feature (params and opaque state, atomic replace through plug::writeText), the Config > Features page.

#include "feature_abi.h"

#include <stdint.h>

class Platform;

namespace features
{
constexpr int kMax = 8;          // features listed
constexpr int kMaxParams = 8;    // params per feature page
constexpr int kMaxState = 64;    // opaque state bytes per feature
constexpr int kFileMax = 384;    // one .cfg file

int add(const feat_desc* d);     // -1: full, bad size or ABI, duplicate id. Built-ins before boot(), a module from its entry
void boot(Platform* platform);   // read the files, call each boot(), Shift held: nothing dynamic (the plugin host's key window)
void tick();                     // main loop: debounced saves of changed files
void flush();                    // write every changed file now (the power off path)

int count();
const feat_desc* desc(int i);
int find(const char* id);
bool on(int i);
uint8_t value(int i, int p);
uint8_t* onRef(int i);           // the config page edits these bytes in place
uint8_t* valueRef(int i, int p);
void changed();                  // the config page confirmed a row: apply what differs, schedule the save
void set(int i, bool on);        // a shortcut outside the page (the Record key of the restart mode)
const char* rowValue(int i);     // list text: "On", "Off", "Restart", a status
bool restartPending(int i);

void registerBuiltins();         // features_builtin.cpp
void menuBuild();                // configMenu.cpp: the Features group after boot()
}   // namespace features
