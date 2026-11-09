// host_poke.cpp helper: the sample importer's label buffers (private members, so this TU sees them as public). Host only, NOT firmware.
#include <cstddef>
#include <cstdio>
#include <string>
#define private public
#include "../../../core/interface/sampleImporter/sampleImporter.h"
#undef private

// the raw bytes of the three label buffers (start, end, play time) and their sizes and offsets
extern "C" void hostPokeImporterLabels(std::string* out)
{
    cSampleImporter& s = sampleImporter;
    char b[512];
    auto hex = [](const char* p, size_t n) {
        std::string r;
        char t[8];
        for (size_t i = 0; i < n; i++)
        {
            snprintf(t, sizeof t, "%02x ", (unsigned char)p[i]);
            r += t;
        }
        return r;
    };
    snprintf(b, sizeof b,
             "importer labels: sizeof(start)=%zu sizeof(end)=%zu sizeof(play)=%zu offsets start=%zu end=%zu play=%zu vol=%zu\n"
             "  start: %s\n  end:   %s\n  play:  %s",
             sizeof(s.startPointValueText), sizeof(s.endPointValueText), sizeof(s.playTimeValueText),
             offsetof(cSampleImporter, startPointValueText), offsetof(cSampleImporter, endPointValueText),
             offsetof(cSampleImporter, playTimeValueText), offsetof(cSampleImporter, previewVolumeText),
             hex(s.startPointValueText, sizeof(s.startPointValueText)).c_str(), hex(s.endPointValueText, sizeof(s.endPointValueText)).c_str(),
             hex(s.playTimeValueText, sizeof(s.playTimeValueText)).c_str());
    *out = b;
}

// the play time label at the end of the sample, as the progress refresh in fSampleImport.cpp draws it while a preview runs past 100 s
extern "C" void hostPokeImporterPlayEnd()
{
    cSampleImporter& s = sampleImporter;
    s.playProgressValue = MAX_16BIT;
    s.importPreviewEndPoint = MAX_16BIT;
    s.showImportPreviewValue();
    s.showImportStartPointValue();
    s.showImportEndPointValue();
}
