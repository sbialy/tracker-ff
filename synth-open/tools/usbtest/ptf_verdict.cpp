// Host tool (NOT firmware): runs the console's PtfValidator over files and prints "<file> <verdict> <top> <sp> <reset>" per file,
// so usbtest_flash_test.sh can compare it with ptfcheck.py. Built with -DENABLE_DEV_CONSOLE.
#include "UsbTestFlash.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
    int bad = 0;
    for (int i = 1; i < argc; i++)
    {
        FILE* f = fopen(argv[i], "rb");
        if (!f)
        {
            printf("%s unreadable\n", argv[i]);
            bad++;
            continue;
        }
        usbtest::PtfValidator v;
        v.begin();
        static unsigned char buf[512];   // the size of a console chunk
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) v.feed(buf, n);
        fclose(f);
        const usbtest::PtfError e = v.finish();
        printf("%s %s %u %08x %08x\n", argv[i], usbtest::ptfErrorName(e), v.top(), v.stackPointer(), v.resetVector());
    }
    return bad ? 1 : 0;
}
