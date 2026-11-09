# Fake master insert files for /Plugins/Master (ui_shot inserts:many, the device folder fakefx100): header and manifest plus a 4 KB
# zero image with a wrong crc32. The scan lists them (FX000 .. FXnnn), a pick fails the load with kCrc (err 6) before anything runs and before any flash command (insert 2).
#   python3 -I make_fake_inserts.py OUTDIR [COUNT]      (default 100)
import os, struct, sys
d = sys.argv[1]
count = int(sys.argv[2]) if len(sys.argv) > 2 else 100
os.makedirs(d, exist_ok=True)
for i in range(count):
    name = b"FX%03d" % i
    # magic, header_size, abi 1.0, kind 1 (master insert), cpu 1 (CM4F), flags 0, uid, version, min_host 1, image 4096, bss 0,
    # rebase 0, entry 0, ram 0, cycles 0, crc32 (wrong on purpose), rw_offset 0, reserved 0 0
    hdr = struct.pack("<IHBBBBHIIIIIIIIIII2I", 0x31474C50, 64 + 96, 1, 0, 1, 1, 0, 0x7F000000 + i, 0x10000, 1, 4096, 0, 0, 0, 0, 0,
                      0xDEADBEEF, 0, 0, 0)
    man = struct.pack("<I16s32s16sHBB6I", 96, name, b"plug-fx test", b"MIT", 1 + i % 8, 0, 0, 0, 0, 0, 0, 0, 0)
    assert len(hdr) == 64 and len(man) == 96
    with open(os.path.join(d, "fx%03d.plg" % i), "wb") as f:
        f.write(hdr + man + b"\0" * 4096)
