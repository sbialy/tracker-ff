#!/usr/bin/env python3
"""Every plugin module tracker-ff ships comes from a module repository, pinned here: external.lock.json names, per set, the repository
(poly-engines-os and poly-effects-os are public, poly-engines and poly-effects private), its visibility, the release tag, the commit
behind it and the sha256 of every published file this repository uses: "files" (the modules, fetched flat into DEST) and
"notice_files" (NOTICE.md, LICENSE.md and the licence texts that must travel with them, fetched into DEST/<set>/ because every
repository has its own NOTICE.md).

  external.py fetch DEST [--from-local] [--set NAME] [--visibility public|private]
                                put the pinned files of the chosen sets into DEST (one flat directory, the layout check_modules.py
                                verify-external, crosscheck.sh, package_release.sh and EXTERNAL_DIR expect). Without --from-local the
                                release assets of the tag are downloaded: a public set over plain https (no token), a private set with
                                gh (needs a token that can read the private repository). --from-local copies from the local build
                                directories named in the lock. Every file is checked against its sha256. Exit 3: not available here.
  external.py check DIR [--set NAME] [--visibility ...]
                                every pinned file of the chosen sets is in DIR and has its sha256. Exit 1 on any difference.
  external.py source SET DEST FILE...
                                source files of a set at its pinned tag (git show from a local checkout: the repository path in the lock
                                or $EXTERNAL_SRC_<SET>, SET upper case with - as _), for host tools that compile module source (ui_shot).
                                FILE is relative to the set's source_root. The tag must still point at tag_commit. Exit 3: no checkout.
  external.py list [--visibility ...]   one line per set: name, visibility, repository, tag, commit, file count
  external.py relock SET [--tag TAG]     after a module repository was rebuilt and tagged: tag_commit from its local checkout (the
                                        lock's tag or TAG) and the sha256 of every pinned file from its local_dirs, written back to
                                        the lock. The file list stays (edit it by hand for a new module). Check with crosscheck.sh.

Nothing here reads a private repository unless the caller has it: CI and a clone without access skip those sets (exit 3 or
--visibility public).
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
    sets = json.load(open(LOCK))["sets"]
    for n, s in sets.items():
        s["_name"] = n
    return sets


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 16), b""):
            h.update(c)
    return h.hexdigest()


def expand(p):
    return os.path.expanduser(p)


def pick(names, sets, visibility=None):
    return [(n, sets[n]) for n in sets if (not names or n in names) and (not visibility or sets[n]["visibility"] == visibility)]


def pinned(s):
    """(file name, sha256, sub directory) of every pinned file of a set: modules flat, notices in <set>/."""
    return [(f, h, "") for f, h in sorted(s["files"].items())] + [(f, h, s["_name"]) for f, h in sorted(s.get("notice_files", {}).items())]


def download(s, tmp):
    """The pinned release assets of a set into tmp. Public: https, private: gh. Returns an error text or None."""
    names = list(s["files"]) + list(s.get("notice_files", {}))
    if s["visibility"] == "public":
        import urllib.request
        for f in names:
            url = "https://github.com/%s/releases/download/%s/%s" % (s["repo"], s["tag"], f)
            try:
                with urllib.request.urlopen(url, timeout=60) as r, open(os.path.join(tmp, f), "wb") as o:
                    shutil.copyfileobj(r, o)
            except Exception as e:  # noqa: BLE001  (any network or HTTP error: the set is not available here)
                return "%s: %s" % (url, e)
        return None
    r = subprocess.run(["gh", "release", "download", s["tag"], "-R", s["repo"], "-D", tmp, "--clobber"] + sum((["-p", f] for f in names), []),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        return "gh release download %s -R %s failed (%s)" % (s["tag"], s["repo"], r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "no access")
    return None


def fetch(a):
    out = os.path.abspath(a.dest)
    os.makedirs(out, exist_ok=True)
    bad = []
    sets = pick(a.set, lock(), a.visibility)
    for name, s in sets:
        with tempfile.TemporaryDirectory(prefix="extfetch") as tmp:
            if a.from_local:
                dirs = [expand(d) for d in s["local_dirs"]]
                if not all(os.path.isdir(d) for d in dirs):
                    print("external: %s: local build directories are missing (%s)" % (name, ", ".join(dirs)), file=sys.stderr)
                    return NA
            else:
                err = download(s, tmp)
                if err:
                    print("external: %s: %s" % (name, err), file=sys.stderr)
                    return NA
                dirs = [tmp]
            for f, want, sub in pinned(s):
                src = next((os.path.join(d, f) for d in dirs if os.path.exists(os.path.join(d, f))), None)
                if not src:
                    bad.append("%s: %s is not available" % (name, f))
                elif sha256(src) != want:
                    bad.append("%s: %s has sha256 %s, the lock pins %s" % (name, f, sha256(src)[:12], want[:12]))
                else:
                    os.makedirs(os.path.join(out, sub), exist_ok=True)
                    shutil.copyfile(src, os.path.join(out, sub, f))
    for b in bad:
        print("external FAILED: " + b, file=sys.stderr)
    if not bad:
        print("external ok: %d pinned files of %s in %s" % (sum(len(pinned(s)) for _, s in sets), ", ".join(n for n, _ in sets), out))
    return 1 if bad else 0


def check(a):
    bad = []
    n = 0
    for name, s in pick(a.set, lock(), a.visibility):
        for f, want, sub in pinned(s):
            p = os.path.join(a.dir, sub, f)
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
    env = "EXTERNAL_SRC_" + a.set.upper().replace("-", "_")
    repo = expand(os.environ.get(env, s["local_repo"]))
    if not os.path.isdir(os.path.join(repo, ".git")):
        print("external: %s: no checkout of %s at %s (set %s)" % (a.set, s["repo"], repo, env), file=sys.stderr)
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


def list_sets(a):
    for name, s in pick(None, lock(), a.visibility):
        print("%-11s %-8s %-24s %-16s %s %3d files, %d notice files" % (name, s["visibility"], s["repo"], s["tag"], s["tag_commit"][:12],
                                                                          len(s["files"]), len(s.get("notice_files", {}))))
    return 0


def relock(a):
    full = json.load(open(LOCK))
    s = full["sets"].get(a.set)
    s.pop("_name", None) if s else None
    if not s:
        sys.exit("external: no set '%s' in the lock" % a.set)
    tag = a.tag or s["tag"]
    repo = expand(s["local_repo"])
    r = subprocess.run(["git", "-C", repo, "rev-parse", tag + "^{commit}"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("external: %s has no tag %s" % (repo, tag))
    dirs = [expand(d) for d in s["local_dirs"]]
    def hashes(names):
        out = {}
        for f in names:
            src = next((os.path.join(d, f) for d in dirs if os.path.exists(os.path.join(d, f))), None)
            if not src:
                sys.exit("external: %s is not in %s" % (f, ", ".join(dirs)))
            out[f] = sha256(src)
        return out
    files, notices = hashes(s["files"]), hashes(s.get("notice_files", {}))
    changed = sorted(f for f in files if files[f] != s["files"][f]) + sorted(f for f in notices if notices[f] != s["notice_files"][f])
    s["tag"], s["tag_commit"], s["files"] = tag, r.stdout.strip(), files
    if notices:
        s["notice_files"] = notices
    with open(LOCK, "w") as fh:
        json.dump(full, fh, indent=1)
        fh.write("\n")
    print("external: %s pinned at %s (%s), %d files, changed: %s" % (a.set, tag, s["tag_commit"][:12], len(files), ", ".join(changed) or "none"))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    f = sp.add_parser("fetch")
    f.add_argument("dest")
    f.add_argument("--from-local", action="store_true")
    f.add_argument("--set", action="append")
    f.add_argument("--visibility", choices=("public", "private"))
    c = sp.add_parser("check")
    c.add_argument("dir")
    c.add_argument("--set", action="append")
    c.add_argument("--visibility", choices=("public", "private"))
    rl = sp.add_parser("relock")
    rl.add_argument("set")
    rl.add_argument("--tag")
    ls = sp.add_parser("list")
    ls.add_argument("--visibility", choices=("public", "private"))
    s = sp.add_parser("source")
    s.add_argument("set")
    s.add_argument("dest")
    s.add_argument("files", nargs="+")
    a = ap.parse_args()
    return {"fetch": fetch, "check": check, "source": source, "list": list_sets, "relock": relock}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
