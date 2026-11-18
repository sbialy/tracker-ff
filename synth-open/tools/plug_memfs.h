// An in-memory card for the sidecar host tests (plug_sidecar_test.cpp, plug_mtff_test.cpp). NOT firmware.
#pragma once

#include "plug_sidecar_io.h"

#include <map>
#include <set>
#include <string>

namespace plug_test
{
using namespace plug;

// An in-memory card: path -> bytes, FatFs rules (rename fails onto an existing name). Faults:
//   unknown         stat of these paths answers kUnknown (a card error), openFail: readFile fails, readFailAfter: a read
//                   fails after that many bytes (the SD read error mid file, SdFile::read gives -1)
//   writeFail       writeNew of these paths fails after writing half (a partial file is left, as a full card does)
//   renameFail      the n-th rename from now fails (0: none), cut: the power goes before the n-th mutation from now (0: none):
//                   that one and every later operation fails, the card keeps what it had
struct MemFs
{
    std::map<std::string, std::string> files;
    std::set<std::string> unknown, openFail, writeFail;
    int readFailAfter = -1;
    int renameFail = 0, cut = 0;
    bool dead = false;
    char scratch[kPathBuf];   // the path scratch of a card (plug_host.cpp: static, SDRAM)
    char* pathScratch() { return scratch; }
    int writes = 0, removes = 0, renames = 0, mutations = 0;
    bool step()   // one mutation: false when the power is gone
    {
        if (dead) return false;
        mutations++;
        if (cut && --cut == 0)
        {
            dead = true;
            return false;
        }
        return true;
    }
    void reboot()   // power back, every fault gone
    {
        dead = false;
        cut = renameFail = 0;
        readFailAfter = -1;
        unknown.clear();
        openFail.clear();
        writeFail.clear();
    }
    Presence stat(const char* p)
    {
        if (dead || unknown.count(p)) return Presence::kUnknown;
        return files.count(p) ? Presence::kPresent : Presence::kAbsent;
    }
    int readFile(const char* p, char* buf, int cap)
    {
        auto it = files.find(p);
        if (dead || unknown.count(p) || openFail.count(p) || it == files.end()) return -1;
        if (readFailAfter >= 0 && int(it->second.size()) > readFailAfter) return -1;
        if (int(it->second.size()) > cap) return -1;
        memcpy(buf, it->second.data(), it->second.size());
        return int(it->second.size());
    }
    bool writeNew(const char* p, const char* buf, int n)
    {
        if (!step()) return false;
        writes++;
        if (writeFail.count(p))
        {
            files[p] = std::string(buf, size_t(n / 2));
            return false;
        }
        files[p] = std::string(buf, size_t(n));
        return true;
    }
    bool remove(const char* p)
    {
        if (!files.count(p) || !step()) return false;
        removes++;
        files.erase(p);
        return true;
    }
    bool rename(const char* from, const char* to)
    {
        if (!files.count(from) || files.count(to) || !step()) return false;
        renames++;
        if (renameFail && --renameFail == 0) return false;
        files[to] = files[from];
        files.erase(from);
        return true;
    }
    bool exists(const char* p) { return files.count(p) != 0; }
    // only the canonical names: no .tmp or .bak left over
    bool clean(std::initializer_list<const char*> keep)
    {
        for (auto& [k, v] : files)
        {
            bool ok = false;
            for (const char* q : keep) ok = ok || k == q;
            if (!ok) return false;
        }
        return true;
    }
};
}   // namespace plug_test
