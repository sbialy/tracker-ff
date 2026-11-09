#!/usr/bin/env python3
"""Build every ot_*.c effect with ptfx_builder.py, check the .ptfx header, then compile
tools/ptfx_host_render.c with clang against the same C source and run it.

  python3 -I editor-fx-sdk/tools/ptfx_host_check.py [effects/ot_tape.c ...]

WAVs go to build/ptfx-wav, host binaries to build/ptfx-host, .ptfx files to
editor-fx-sdk/build/Effects. Exit status is non-zero if any build or check fails.
Nothing here runs on the device.
"""
import glob
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(HERE)
ROOT = os.path.dirname(SDK)
HEADER_SIZE = 472
LIMIT = 32 * 1024  # code + bss on MK66


def toolchain():
    p = os.path.join(ROOT, "agt", "10.3", "bin", "arm-none-eabi-")
    return p if os.path.exists(p + "gcc") else ""


def read_header(path):
    d = open(path, "rb").read()
    magic, ver, flags = struct.unpack_from("<IHH", d, 0)
    name = d[8:40].split(b"\0")[0].decode()
    nparams = d[0x38]
    scratch, code_off, code_size, bss = struct.unpack_from("<IIII", d, 0x3C)
    labels = []
    for i in range(nparams):
        labels.append(d[0x58 + 32 * i:0x58 + 32 * i + 8].split(b"\0")[0].decode())
    return dict(magic=magic, flags=flags, name=name, nparams=nparams, scratch=scratch,
                code=code_size, bss=bss, labels=labels, size=len(d), code_off=code_off)


def main():
    srcs = sys.argv[1:] or sorted(glob.glob(os.path.join(SDK, "effects", "ot_*.c")))
    eff_dir = os.path.join(SDK, "build", "Effects")
    host_dir = os.path.join(ROOT, "build", "ptfx-host")
    wav_dir = os.path.join(ROOT, "build", "ptfx-wav")
    for d in (eff_dir, host_dir, wav_dir):
        os.makedirs(d, exist_ok=True)
    rows, bad = [], 0
    for src in srcs:
        base = os.path.splitext(os.path.basename(src))[0]
        out = os.path.join(eff_dir, base + ".ptfx")
        cmd = [sys.executable, "-I", os.path.join(SDK, "ptfx_builder.py"), src, "-o", out]
        if toolchain():
            cmd += ["--toolchain", toolchain()]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[{base}] BUILD FAILED\n{r.stdout}{r.stderr}")
            rows.append((base, "BUILD FAIL"))
            bad += 1
            continue
        h = read_header(out)
        problems = []
        if h["magic"] != 0x58465450:
            problems.append("bad magic")
        if h["flags"] & 0x10:
            problems.append("IMX_ONLY set")
        if h["nparams"] > 12 or h["nparams"] == 0:
            problems.append(f"{h['nparams']} params")
        if h["code"] + h["bss"] > LIMIT:
            problems.append(f"code+bss {h['code'] + h['bss']} > {LIMIT}")
        if any(len(l) > 7 for l in h["labels"]):
            problems.append("label too long")
        print(f"[{base}] built: '{h['name']}' params={h['nparams']} {h['labels']} code={h['code']} "
              f"bss={h['bss']} scratch={h['scratch']} file={h['size']}")
        hb = os.path.join(host_dir, base)
        cc = ["clang", "-O2", "-Wall", "-Wextra", "-DPTFX_HOST_TEST", f"-DPLUGIN_NPARAMS={h['nparams']}",
              f"-DPLUGIN_SCRATCH={h['scratch']}", f'-DPLUGIN_TAG="{base}"', f"-I{SDK}", "-o", hb,
              os.path.join(HERE, "ptfx_host_render.c"), src, "-lm"]
        r = subprocess.run(cc, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[{base}] HOST COMPILE FAILED\n{r.stderr}")
            problems.append("host compile")
        else:
            if r.stderr.strip():
                print(r.stderr)
            r = subprocess.run([hb, wav_dir], capture_output=True, text=True)
            sys.stdout.write(r.stdout)
            sys.stderr.write(r.stderr)
            if r.returncode != 0:
                problems.append("render checks")
        status = "OK" if not problems else "FAIL: " + ", ".join(problems)
        if problems:
            bad += 1
        rows.append((base, status, h))
    print("\n=== summary ===")
    print(f"{'plugin':<14}{'name':<18}{'params':>6}{'code':>8}{'bss':>6}{'scratch':>9}  result")
    for row in rows:
        if len(row) == 2:
            print(f"{row[0]:<14}{row[1]}")
        else:
            b, st, h = row
            print(f"{b:<14}{h['name']:<18}{h['nparams']:>6}{h['code']:>8}{h['bss']:>6}{h['scratch']:>9}  {st}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
