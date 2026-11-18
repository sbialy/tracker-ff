#!/usr/bin/env python3
"""Host proof that the deflated icons (core/libs/images/icons_z.h) decode to the icon arrays of the headers, byte for byte.

  1. Compiles the firmware's own core/libs/NewDisplay/inflate.cpp with icons_z.h and the original icon headers, three times: the
     original release set (FEATURE_SYNTH_OPEN, PERC), a build without SYNTH_OPEN (every group) and a build without FEATURE_SYNTH.
  2. Inflates every group that build carries into a 22,396 B buffer (the size of display.cpp decompressedFontsCommonBuffer) and
     compares each icon of the group, at its kZo_ offset, with the original array.
  3. Reads the ICON(...) rows of display.cpp bitmaps[] and checks that each size column equals the array's byte count and that every
     icon is in a group (so the table and the generator cannot drift apart), under ASan and UBSan.
Usage: icons_inflate_test.py [WORKTREE]        Prints "icons_inflate_test: PASS" (exit 0) or FAIL.
"""
import os
import re
import subprocess
import sys
import tempfile

root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
img = os.path.join(root, "core", "libs", "images")
disp = os.path.join(root, "core", "libs", "NewDisplay")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icons_deflate as gen  # noqa: E402

bad = 0
arrays = gen.read_arrays(img)
names = [n for _, _, ns in gen.GROUPS for n in ns]
groups_of = {n: gi for gi, (_, _, ns) in enumerate(gen.GROUPS) for n in ns}

# the table rows of display.cpp
rows = re.findall(r"^\s*ICON\((\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(L\d),\s*(\d+)\)", open(os.path.join(disp, "display.cpp")).read(), re.M)
for n, addr, size, w, h, fmt, stride in rows:
    if n not in arrays:
        bad += 1
        print("FAIL: table row %s has no icon array" % n)
    elif len(arrays[n]) != int(size):
        bad += 1
        print("FAIL: %s is %d B, the table says %d" % (n, len(arrays[n]), int(size)))
    elif int(stride) * int(h) != int(size):
        bad += 1
        print("FAIL: %s stride %s x height %s is not %s B" % (n, stride, h, size))
    if n not in groups_of:
        bad += 1
        print("FAIL: %s is in no group of icons_deflate.py" % n)
print("table: %d ICON rows checked" % len(rows))

CHECK = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define FW_TRIM 1
%(defs)s
%(incs)s
#include "images/icons_z.h"
#include "NewDisplay/inflate.h"
static uint8_t buf[22396];
static InflateScratch scratch;
struct Ref { const char* name; const uint8_t* data; size_t len; int group; int off; };
static const Ref refs[] = {
%(refs)s
};
int main()
{
    int bad = 0, groups = 0, icons = 0;
    for (size_t g = 0; g < sizeof(kIconZGroups) / sizeof(kIconZGroups[0]); g++)
    {
        const IconZGroup& z = kIconZGroups[g];
        if (!z.stream) continue;
        groups++;
        memset(buf, 0xA5, sizeof(buf));
        int n = inflateRaw(z.stream, z.streamLen, buf, sizeof(buf), &scratch);
        if (n != z.rawLen) { printf("FAIL group %%zu inflate returned %%d, expected %%d\n", g + 1, n, z.rawLen); bad++; continue; }
        for (const Ref& r : refs)
        {
            if (r.group != (int)g + 1) continue;
            icons++;
            if (r.off + r.len > z.rawLen || memcmp(buf + r.off, r.data, r.len)) { printf("FAIL icon %%s differs\n", r.name); bad++; }
        }
    }
    printf("groups %%d icons %%d bad %%d\n", groups, icons, bad);
    return bad != 0;
}
'''

VARIANTS = [
    ("release", ["-DFEATURE_SYNTH", "-DFEATURE_SYNTH_OPEN", "-DSYNTH_ENGINE_PERC"], {"perc"}, {"base", "synth", "perc"}),
    ("full", ["-DFEATURE_SYNTH"], None, {"base", "synth", "perc", "phz", "dirt", "chip"}),
    ("nosynth", [], None, {"base"}),
]
with tempfile.TemporaryDirectory() as tmp:
    for vname, defs, _, expect in VARIANTS:
        src = os.path.join(tmp, vname + ".cpp")
        incs = "".join('#include "images/%s"\n' % f for f in sorted(os.listdir(img)) if f.endswith(".h") and f not in ("icons_z.h",)
                       and not f.startswith("poly_logo") and not f.startswith("RobotoMono") and not f.startswith("Liberation"))
        refs = ",\n".join('    { "%s", %s, sizeof(%s), kZg_%s, kZo_%s }' % (n, n, n, n, n) for n in names)
        defines = "".join("#define %s 1\n" % d[2:] for d in defs)
        # FW_ICON_* as displayStructs.h derives them
        defines += "#if defined(SYNTH_ENGINE_PERC)\n#define FW_ICON_PERC 1\n#else\n#define FW_ICON_PERC 0\n#endif\n"
        defines += "#if !defined(FEATURE_SYNTH_OPEN)\n#define FW_ICON_CHIP 1\n#else\n#define FW_ICON_CHIP 0\n#endif\n"
        if vname == "full":
            defines += "#undef FW_ICON_PERC\n#define FW_ICON_PERC 1\n"
        open(src, "w").write(CHECK % {"defs": defines, "incs": incs, "refs": refs})
        exe = os.path.join(tmp, vname)
        r = subprocess.run(["c++", "-std=gnu++20", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                            "-Wno-unused-const-variable", "-I" + os.path.join(root, "core", "libs"), "-I" + img, src,
                            os.path.join(disp, "inflate.cpp"), "-o", exe], capture_output=True, text=True)
        if r.returncode:
            sys.exit("compile failed (%s):\n%s" % (vname, r.stderr[-3000:]))
        r = subprocess.run([exe], capture_output=True, text=True)
        print("%s: %s" % (vname, r.stdout.strip() or r.stderr[-500:]))
        m = re.search(r"groups (\d+) icons (\d+)", r.stdout)
        if r.returncode or not m or int(m.group(1)) != len(expect):
            bad += 1
            print("FAIL: variant %s (groups expected %d)" % (vname, len(expect)))

print("icons_inflate_test: " + ("FAIL" if bad else "PASS"))
sys.exit(1 if bad else 0)
