#!/usr/bin/env python3
"""The private modules (PHZ, PLK, Console, Tube) as this repository pins them: external.lock.json names the release tag of each private
repository (poly-engines, poly-effects), the commit behind it and the sha256 of every published file this repository uses.

  external.py fetch DEST [--from-local] [--set NAME]   put the pinned files of every set into DEST (one flat directory, the layout
                                                        that build_set.py verify-external and EXTERNAL_DIR expect). Without --from-local the
                                                        release assets of the tag are downloaded with gh (needs a token that can read the
                                                        private repository). --from-local copies from the local build directories named in
                                                        the lock. Every file is checked against its sha256. Exit 3: not available here.
  external.py check DIR                                 every pinned file is in DIR and has its sha256. Exit 1 on any difference.
  external.py source SET DEST FILE...                   the source files of a set at its pinned tag (git archive of a local checkout,
                                                        the repository path in the lock or $EXTERNAL_SRC_<SET>), for host tools that
                                                        compile the private source. The tag must still point at tag_commit. Exit 3: no checkout.

Nothing here reads a private repository unless the caller has it: CI and a clone without access skip these steps (exit 3 or EXTERNAL_DIR unset).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LOCK = os.path.join(HERE, "external.lock.json")
NA = 3


def lock():
    return json.load(open(LOCK))["sets"]


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 16), b""):
            h.update(c)
    return h.hexdigest()


def expand(p):
    return os.path.expanduser(p)


def pick(names, sets):
    return [(n, sets[n]) for n in sets if not names or n in names]


def fetch(a):
    out = os.path.abspath(a.dest)
    os.makedirs(out, exist_ok=True)
    bad = []
    for name, s in pick(a.set, lock()):
        with tempfile.TemporaryDirectory(prefix="extfetch") as tmp:
            if a.from_local:
                dirs = [expand(d) for d in s["local_dirs"]]
                if not all(os.path.isdir(d) for d in dirs):
                    print("external: %s: local build directories are missing (%s)" % (name, ", ".join(dirs)), file=sys.stderr)
                    return NA
            else:
                r = subprocess.run(["gh", "release", "download", s["tag"], "-R", s["repo"], "-D", tmp, "--clobber"] + sum((["-p", f] for f in s["files"]), []),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                if r.returncode:
                    print("external: %s: gh release download %s -R %s failed (%s)" % (name, s["tag"], s["repo"], r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "no access"), file=sys.stderr)
                    return NA
                dirs = [tmp]
            for f, want in sorted(s["files"].items()):
                src = next((os.path.join(d, f) for d in dirs if os.path.exists(os.path.join(d, f))), None)
                if not src:
                    bad.append("%s: %s is not available" % (name, f))
                elif sha256(src) != want:
                    bad.append("%s: %s has sha256 %s, the lock pins %s" % (name, f, sha256(src)[:12], want[:12]))
                else:
                    shutil.copyfile(src, os.path.join(out, f))
    for b in bad:
        print("external FAILED: " + b, file=sys.stderr)
    if not bad:
        print("external ok: %d pinned files of %s in %s" % (sum(len(s["files"]) for _, s in pick(a.set, lock())), ", ".join(n for n, _ in pick(a.set, lock())), out))
    return 1 if bad else 0


def check(a):
    bad = []
    n = 0
    for name, s in lock().items():
        for f, want in sorted(s["files"].items()):
            p = os.path.join(a.dir, f)
            n += 1
            if not os.path.exists(p):
                bad.append("%s: %s is missing" % (name, f))
            elif sha256(p) != want:
                bad.append("%s: %s differs from the pin (%s, tag %s)" % (name, f, s["repo"], s["tag"]))
    for b in bad:
        print("external check FAILED: " + b)
    if not bad:
        print("external check ok: %d pinned files match the lock in %s" % (n, a.dir))
    return 1 if bad else 0


def source(a):
    s = lock().get(a.set)
    if not s:
        sys.exit("external: no set '%s' in the lock" % a.set)
    repo = expand(os.environ.get("EXTERNAL_SRC_" + a.set.upper(), s["local_repo"]))
    if not os.path.isdir(os.path.join(repo, ".git")):
        print("external: %s: no checkout of %s at %s (set EXTERNAL_SRC_%s)" % (a.set, s["repo"], repo, a.set.upper()), file=sys.stderr)
        return NA
    r = subprocess.run(["git", "-C", repo, "rev-parse", s["tag"] + "^{commit}"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode or r.stdout.strip() != s["tag_commit"]:
        print("external: %s: tag %s is %s in %s, the lock pins %s" % (a.set, s["tag"], r.stdout.strip() or "missing", repo, s["tag_commit"]), file=sys.stderr)
        return NA
    os.makedirs(a.dest, exist_ok=True)
    for f in a.files:
        p = subprocess.run(["git", "-C", repo, "show", "%s:%s" % (s["tag"], s["source_root"] + f)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if p.returncode:
            print("external: %s: %s is not in %s" % (a.set, s["source_root"] + f, s["tag"]), file=sys.stderr)
            return NA
        dst = os.path.join(a.dest, os.path.basename(f))
        if not os.path.exists(dst) or open(dst, "rb").read() != p.stdout:   # rewrite only a changed file, make tracks timestamps
            open(dst, "wb").write(p.stdout)
    print("external ok: %s at %s (%s): %s" % (a.set, s["tag"], s["tag_commit"][:7], " ".join(os.path.basename(f) for f in a.files)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    f = sp.add_parser("fetch")
    f.add_argument("dest")
    f.add_argument("--from-local", action="store_true")
    f.add_argument("--set", action="append")
    c = sp.add_parser("check")
    c.add_argument("dir")
    s = sp.add_parser("source")
    s.add_argument("set")
    s.add_argument("dest")
    s.add_argument("files", nargs="+")
    a = ap.parse_args()
    return {"fetch": fetch, "check": check, "source": source}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
