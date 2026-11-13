#pragma once

// Features registry (FEATURE_EXT_HOOKS builds): built-in descriptors plus the ones a module hands in,
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

// The card side, so the parser and the files can be tested on the Mac without a Platform (features_test.cpp).
struct Io
{
    int (*read)(const char* path, char* buf, int cap);            // bytes read, <= 0 none (a lone .bak comes back first)
    bool (*write)(const char* path, const char* buf, int n);      // atomic replace
    bool (*exists)(const char* path);
    void (*mkdir)(const char* path);
    void (*remove)(const char* path);
    uint32_t (*millis)();
};

int add(const feat_desc* d);     // -1: full, bad size or ABI, duplicate id. Built-ins before boot(), a module from its entry
void boot(Platform* platform);   // card Io, registerBuiltins(), load(), menuBuild()
void load(const Io* io);         // read every file, call each boot() (the test calls it with its own Io)
void tick();                     // main loop: debounced saves of changed files
void flush();                    // write every changed file now (any power off, Restart now)
void resetAll();                 // Config > Firmware > Reset config: every feature to its defaults, its files removed
void clear();                    // the test: forget every feature

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
void hideUnlessOn(int i);        // listed only while on (Developer mode without /.develop, decision D8), call before load()
bool listed(int i);

// Restart now (decision D9): the normal power off with a restart, so the Workspace and the files are saved first
void setRestartNow(void (*fn)());
void restartNow();

void registerBuiltins();         // features_builtin.cpp
void menuBuild();                // configMenu.cpp: the Features group after boot()
}   // namespace features
