#!/usr/bin/env python3
"""The curated /Waves set of the release zip (flat, AKWF-FREE, CC0 1.0).

  waves_curated.py build AKWF_DIR   pick the files of PLAN from the AKWF-FREE tree, copy them flat into releases/waves/Waves and write
                                    releases/waves/MANIFEST.txt (sha256, bytes, flat name, source folder)
  waves_curated.py check            re-hash releases/waves/Waves against the manifest, names unique, format ok, no stray file
  waves_curated.py stage CARD_DIR   copy Waves/ (the files, the licence and the readme) to CARD_DIR/Waves (package_release.sh calls this)

The editor ships the same files: its copy is taken from MANIFEST.txt, never from the AKWF tree. Run with python3 -I.
"""
import hashlib, os, shutil, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "releases", "waves")
WAVES = os.path.join(OUT, "Waves")
MANIFEST = os.path.join(OUT, "MANIFEST.txt")
EXTRA = ("AKWF-LICENSE.txt", "README.txt")   # in /Waves next to the files, ignored by the host list (it takes .wav only)

# folder -> how many files, evenly spread over the folder. Basic shapes first, then the categories.
PLAN = [
    ("AKWF_bw_sin", 4), ("AKWF_bw_tri", 4), ("AKWF_bw_squ", 6), ("AKWF_bw_saw", 6), ("AKWF_bw_sawbright", 2), ("AKWF_bw_sawrounded", 3),
    ("AKWF_bw_squrounded", 3), ("AKWF_bw_sawgap", 2), ("AKWF_bw_perfectwaves", 4), ("AKWF_bw_blended", 4), ("AKWF_sinharm", 4),
    ("AKWF_symetric", 2), ("AKWF_overtone", 3), ("AKWF_hdrawn", 3), ("AKWF_linear", 3), ("AKWF_bitreduced", 3), ("AKWF_c604", 2),
    ("AKWF_oscchip", 4), ("AKWF_vgamebasic", 3), ("AKWF_vgame", 2), ("AKWF_fmsynth", 4), ("AKWF_granular", 2), ("AKWF_distorted", 3),
    ("AKWF_raw", 2), ("AKWF_snippets", 2), ("AKWF_pluckalgo", 3), ("AKWF_aguitar", 2), ("AKWF_altosax", 2), ("AKWF_birds", 1),
    ("AKWF_cello", 2), ("AKWF_clarinett", 2), ("AKWF_clavinet", 2), ("AKWF_dbass", 3), ("AKWF_ebass", 3), ("AKWF_eguitar", 2),
    ("AKWF_eorgan", 3), ("AKWF_epiano", 3), ("AKWF_flute", 2), ("AKWF_hvoice", 3), ("AKWF_oboe", 2), ("AKWF_piano", 2),
    ("AKWF_stringbox", 2), ("AKWF_theremin", 2), ("AKWF_violin", 2),
] + [("AKWF_%04d" % i, 1) for i in range(1, 21)]
MAXFRAMES = 2048   # PLUG_FILES_FRAMES


def ok_wave(p):
    try:
        with wave.open(p) as w:
            return w.getnchannels() == 1 and w.getsampwidth() == 2 and 1 <= w.getnframes() <= MAXFRAMES
    except Exception:
        return False


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def build(src):
    if os.path.isdir(WAVES):
        for f in os.listdir(WAVES):
            if f.endswith(".wav"):
                os.remove(os.path.join(WAVES, f))
    os.makedirs(WAVES, exist_ok=True)
    rows = []
    for folder, k in PLAN:
        names = sorted(f for f in os.listdir(os.path.join(src, folder)) if f.lower().endswith(".wav") and not f.startswith("."))
        good = [n for n in names if ok_wave(os.path.join(src, folder, n))]
        pick = []
        for i in range(k):
            j = round(i * (len(good) - 1) / (k - 1)) if k > 1 else len(good) // 2 if folder.startswith("AKWF_0") else 0
            pick.append(good[j])
        for n in pick:
            shutil.copyfile(os.path.join(src, folder, n), os.path.join(WAVES, n))
            rows.append((sha(os.path.join(WAVES, n)), os.path.getsize(os.path.join(WAVES, n)), n, folder))
    names = [r[2] for r in rows]
    assert len(set(names)) == len(names), "flat names are not unique"
    rows.sort(key=lambda r: r[2])
    with open(MANIFEST, "w") as m:
        m.write("# Curated /Waves set of the release zip: flat, %d files, AKWF-FREE by Kristoffer Ekstrand (CC0 1.0).\n" % len(rows))
        m.write("# sha256  bytes  file  AKWF folder. Regenerate: python3 -I synth-open/tools/waves_curated.py build <AKWF-FREE>/AKWF\n")
        for r in rows:
            m.write("%s  %d  %s  %s\n" % r)
    print("wrote", len(rows), "files,", sum(r[1] for r in rows), "bytes")


def rows_of_manifest():
    out = []
    for line in open(MANIFEST):
        if line.startswith("#") or not line.strip():
            continue
        h, size, name, folder = line.split()
        out.append((h, int(size), name, folder))
    return out


def check():
    rows = rows_of_manifest()
    bad = 0
    names = [r[2] for r in rows]
    if len(set(names)) != len(names):
        print("FAIL names not unique"); bad += 1
    for h, size, n, _ in rows:
        p = os.path.join(WAVES, n)
        if not os.path.isfile(p) or sha(p) != h or os.path.getsize(p) != size:
            print("FAIL", n); bad += 1
        elif not ok_wave(p):
            print("FAIL format", n); bad += 1
        if len(n) > 31 or "/" in n:
            print("FAIL name", n); bad += 1
    have = {f for f in os.listdir(WAVES)}
    for f in sorted(have - set(names) - set(EXTRA)):
        print("FAIL stray", f); bad += 1
    for f in EXTRA:
        if f not in have:
            print("FAIL missing", f); bad += 1
    print("files %d, %s" % (len(rows), "GREEN" if not bad else "RED (%d)" % bad))
    return 1 if bad else 0


def stage(card):
    if check():
        return 1
    dst = os.path.join(card, "Waves")
    os.makedirs(dst, exist_ok=True)
    for f in sorted(os.listdir(WAVES)):
        shutil.copyfile(os.path.join(WAVES, f), os.path.join(dst, f))
    print("staged", dst)
    return 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["build"] and len(a) == 2:
        build(a[1])
    elif a == ["check"]:
        sys.exit(check())
    elif a[:1] == ["stage"] and len(a) == 2:
        sys.exit(stage(a[1]))
    else:
        sys.exit(__doc__)
