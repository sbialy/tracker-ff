"""Shared by build_modules.py: the module format checks of synth-open/plug/plug_wasm.h (imports, exports, stripping), the emscripten
environment and small helpers. No build here."""
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

ALLOWED_IMPORTS = {("env", "memory"), ("env", "__indirect_function_table"), ("env", "__memory_base"), ("env", "__table_base"),
                   ("env", "__stack_pointer")}


def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if not x & 0x80:
            return r, i


def wasm_sections(data):
    """(id, name, payload) of every section. Enough of the binary format to check imports, exports and custom sections."""
    assert data[:8] == b"\0asm\1\0\0\0", "not a wasm module"
    i, out = 8, []
    while i < len(data):
        sid = data[i]
        n, i = leb(data, i + 1)
        body = data[i:i + n]
        name = None
        if sid == 0:
            ln, j = leb(body, 0)
            name = body[j:j + ln].decode()
        out.append((sid, name, body))
        i += n
    return out


def imports_exports(data):
    imps, exps = [], []
    for sid, _, b in wasm_sections(data):
        if sid == 2:
            n, i = leb(b, 0)
            for _ in range(n):
                ml, i = leb(b, i)
                mod = b[i:i + ml].decode()
                i += ml
                fl, i = leb(b, i)
                fld = b[i:i + fl].decode()
                i += fl
                kind = b[i]
                i += 1
                if kind == 0:
                    _, i = leb(b, i)
                elif kind == 1:
                    i += 1
                    flags, i = leb(b, i)
                    _, i = leb(b, i)
                    if flags & 1:
                        _, i = leb(b, i)
                elif kind == 2:
                    flags, i = leb(b, i)
                    _, i = leb(b, i)
                    if flags & 1:
                        _, i = leb(b, i)
                elif kind == 3:
                    i += 2
                else:
                    sys.exit("unknown import kind %d" % kind)
                imps.append((mod, fld, kind))
        elif sid == 7:
            n, i = leb(b, 0)
            for _ in range(n):
                nl, i = leb(b, i)
                nm = b[i:i + nl].decode()
                i += nl
                kind = b[i]
                _, i = leb(b, i + 1)
                exps.append((nm, kind))
    return imps, exps


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def run(cmd, cwd, env):
    r = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("FAILED: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout

def emsdk_env(emsdk):
    up = os.path.join(emsdk, "upstream", "emscripten")
    if not os.path.exists(os.path.join(up, "em++")):
        sys.exit("no em++ under %s (pass --emsdk, or EMSDK in the environment)" % up)
    env = dict(os.environ)
    env["PATH"] = up + os.pathsep + os.path.join(emsdk, "upstream", "bin") + os.pathsep + env["PATH"]
    env.setdefault("EM_CACHE", os.path.join(up, "cache"))
    return env, up
