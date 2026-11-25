#!/usr/bin/env python3
"""make_host_module.py OUT [--image-id N] [--run ADDR]: an extras.plg (MOD/IT) stand-in for the ui_shot host (EXTHOOKS=1). With --run 0x70D8A000
(ext_module.cpp kConsoleHostRun) the same file stands in for the console module.plg (UISHOT_HOST_CONSOLE=1).

The host cannot run Arm code: its binary has the three MOD/IT sources inside and ext_module.cpp sets the hook itself (enterModit).
What the host does run is the loader (ext_loader.h moditLoad: header, layout, base, CRC, image id), so the card file has to be a file
that passes it: the same header and manifest as pack_ext.py writes (kind 6), a dummy image, the host's image id (0, ext_module.cpp
imageCrc()) and the host's constant run address (kModitHostRun). Options make the refusals: another image id, another base, a bad CRC,
a header that is not a module, a file cut short."""
import argparse
import struct
import zlib

RUN = 0x70D88000   # ext_module.cpp kModitHostRun
CONSOLE_RUN = 0x70D8A000   # ext_module.cpp kConsoleHostRun: the console module.plg carries no sub-slot table


def abi_rel_bytes():
    import os, re
    text = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "core", "include", "ext_abi.h")).read()
    m = re.search(r"#define EXT_REL_BYTES \(?([0-9]+)u?(?: \* ([0-9]+)u)?\)?", text)
    return int(m.group(1)) * int(m.group(2) or 1)


def rel_table(region=None, version=2):
    region = abi_rel_bytes() if region is None else region
    """extras.plg's sub-slot table (ext_abi.h ext_rel_table_t): MOD/IT 1 KB at 0, the other four tenants empty after it."""
    subs = [(0, 1024)] + [(1024, 0)] * 4
    return struct.pack("<IHHII", 0x54524558, version, len(subs), region, 1024) + b"".join(struct.pack("<II", a, n) for a, n in subs)


def build(image_id=0, run=RUN, bad_crc=False, magic=0x31474C50, cut=0, table=None):
    manifest = struct.pack("<I16s32s16sHBBIIIII4x", 96, b"MOD/IT", b"tracker-ff", b"host", 0, 0, 0, 0, 0, 0, 0, 0)
    if table is None:
        table = run != CONSOLE_RUN
    if table:
        manifest += rel_table(version=table if isinstance(table, int) and not isinstance(table, bool) else 2)
    text, data, bss = 512, 64, 128
    image = bytes((i * 7 + 1) & 0xFF for i in range(text + data))
    payload = manifest + image
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    if bad_crc:
        crc ^= 0x1
    hdr = struct.pack("<IHBBBBH" + "I" * 13, magic, 64 + len(manifest), 1, 1, 6, 1, 0, 0x4D4F4449, (1 << 16) | (4 << 8), 1, len(image), bss, 0, 0,
                      text + data + bss, 0, crc, text, image_id, run)
    assert len(hdr) == 64
    f = hdr + payload
    return f[:len(f) - cut] if cut else f


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--image-id", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--run", type=lambda s: int(s, 0), default=RUN)
    ap.add_argument("--bad-crc", action="store_true")
    ap.add_argument("--bad-magic", action="store_true")
    ap.add_argument("--cut", type=int, default=0)
    ap.add_argument("--no-table", action="store_true", help="a round 1 extras.plg: no sub-slot table after the manifest (refused)")
    a = ap.parse_args()
    with open(a.out, "wb") as o:
        o.write(build(a.image_id, a.run, a.bad_crc, 0x12345678 if a.bad_magic else 0x31474C50, a.cut, False if a.no_table else None))
