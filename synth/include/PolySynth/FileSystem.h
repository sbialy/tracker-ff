#pragma once

#include "PolyUtil/misc.h"

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace polysynth
{
/**
 * Abstraction of file system access.
 */
class FileSystem
{
  public:
    using FileHandle = void*;

    enum class OpenOptions
    {
        kNone = 0,
        kWrite = 1,
    };

    /**
     * returns a view to the list of files. It is owned internally so if there's a need for storage
     * or processing, then copy is needed.
     * Subsequent calls to this function invalidate previous results
     */
    virtual std::vector<std::string_view> listFiles(
      const std::string& path,
      std::function<bool(const ::std::string& name)> matcher,
      tiny_optional<int> maxFiles) = 0;

    virtual std::optional<FileHandle> open(const std::string& path, OpenOptions options) = 0;
    virtual void close(FileHandle file) = 0;
    virtual bool write(FileHandle file, std::span<const uint8_t> data) = 0;
    virtual int32_t read(FileHandle file, std::span<uint8_t> buf) = 0;
    virtual bool seek(FileHandle file, int offset) = 0;

    virtual void createFolder(const std::string& path) = 0;

    virtual ~FileSystem() = default;

#ifdef FEATURE_SYNTH_OPEN
    // Open shim only, declared after the destructor so the vtable of the closed library's FileSystem keeps its layout.
#define POLYSYNTH_FS_PRESENCE 1   // open_fileops.h takes this Presence

    /** What the card said about a path: kAbsent only when the card walked the folder and found no such entry, kUnknown for any card error. */
    enum class Presence
    {
        kAbsent,
        kPresent,
        kUnknown,
    };
    /** The default knows nothing. */
    virtual Presence presence(const std::string& path)
    {
        (void)path;
        return Presence::kUnknown;
    }
    /**
     * Replaces the file `to` by the file `from` (same folder): the old file steps aside as `to`.bak and is deleted once `from` is in place.
     * False: nothing was replaced. `to` is as it was or, when the move back failed or the power went, absent with the old file at
     * `to`.bak: a reader that finds `to` absent moves `to`.bak back first (OpenPatchStore, open_cold.cpp). `from` may be left behind.
     * The default does nothing and fails.
     */
    virtual bool replace(const std::string& from, const std::string& to)
    {
        (void)from;
        (void)to;
        return false;
    }
    /** Deletes a file. False when it is not there or cannot be deleted. The default does nothing and fails. */
    virtual bool remove(const std::string& path)
    {
        (void)path;
        return false;
    }
#endif
};
}
