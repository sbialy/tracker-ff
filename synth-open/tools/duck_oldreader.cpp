// Reads a sidecar with a plug_sidecar.h of the build before ducking (duck_test.sh compiles this against that header, OLD commit) and prints what
// the 1.3.0 plugin host would take from it. A file with a "duck" line must read the same as the file without the line.
#include "plug_sidecar.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    static char buf[plug::kSidecarMax];
    FILE* f = argc > 1 ? fopen(argv[1], "rb") : nullptr;
    if (!f)
        return 2;
    const int n = int(fread(buf, 1, sizeof(buf), f));
    fclose(f);
    static plug::Sidecar s;
    const bool ok = plug::sidecarRead(buf, n, s);
    printf("read=%d format=%d mixer=%d slot1=%08x/%s/%d slot2=%08x/%s/%d\n", ok, s.format, s.mixer, s.slot[0].uid, s.slot[0].name, s.slot[0].count,
           s.slot[1].uid, s.slot[1].name, s.slot[1].count);
    return 0;
}
