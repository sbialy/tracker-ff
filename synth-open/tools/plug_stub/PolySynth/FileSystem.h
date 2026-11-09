// Host tests of the plugin host (plug_sidecar_test.cpp): open_fileops.h needs only the Presence of the shim's FileSystem
// (synth/include/PolySynth/FileSystem.h, which needs the firmware's tiny/optional and does not build for arm64 macOS).
// Same names as there.
#pragma once

namespace polysynth
{
class FileSystem
{
  public:
    enum class Presence
    {
        kAbsent,
        kPresent,
        kUnknown,
    };
};
} // namespace polysynth
