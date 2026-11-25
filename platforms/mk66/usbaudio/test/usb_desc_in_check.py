import re, sys
path, inf, mode, drive, msc = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
name = "in=%d mode=%d drive=%d msc_built=%d" % (inf, mode, drive, msc)
txt = open(path).read().splitlines()
cfg = [l for l in txt if l.startswith("desc 0200")][0]
b = [int(x, 16) for x in cfg.split(":", 1)[1].split()]
served = int(re.search(r"len (\d+):", cfg).group(1))
eps = dict((int(m.group(1)), int(m.group(2), 16)) for m in re.finditer(r" (\d+)=([0-9a-f]{2})", [l for l in txt if l.startswith("endpoints")][0]))
fail = []
tot = b[2] | b[3] << 8
if tot != served: fail.append("wTotalLength %d served %d" % (tot, served))
i = 0; ifs = []; epl = []; iad = None; hdr = None; terms = {}; alts = []
fmts = {}
cur = None
while i < len(b):
    L, T = b[i], b[i + 1]
    if L < 2 or i + L > len(b): fail.append("bad bLength %d at %d" % (L, i)); break
    d = b[i:i + L]
    if T == 4:
        ifs.append((d[2], d[3])); cur = d[2]
    elif T == 5:
        epl.append((cur, d[2], d[3], d[4] | d[5] << 8))
    elif T == 11:
        iad = (d[2], d[3])
    elif T == 0x24 and cur == 4 and d[2] == 1:
        hdr = d  # AC header: bLength 8+n, wTotalLength d[5:7], bInCollection d[7], baInterfaceNr d[8:]
    elif T == 0x24 and d[2] == 2 and L == 11:
        fmts[cur] = d[4]
    elif T == 0x24 and cur == 4 and d[2] in (2, 3):
        terms[d[3]] = (d[2], d[4] | d[5] << 8, d)
    i += L
if i != len(b): fail.append("descriptors chain to %d of %d" % (i, len(b)))
nif = b[4]
nums = []
for n, a in ifs:
    if n not in nums: nums.append(n)
if nums != list(range(len(nums))): fail.append("interface numbers %s" % nums)
if nif != len(nums): fail.append("bNumInterfaces %d found %d" % (nif, len(nums)))
audio_off = mode == 0
exp_audio_ifs = 0 if audio_off else (3 if inf and mode == 1 else 2)
base = 4
exp_n = base + exp_audio_ifs + (1 if drive else 0)
if nif != exp_n: fail.append("bNumInterfaces %d expected %d" % (nif, exp_n))
in_ep = [e for e in epl if e[1] == 0x88]; out_ep = [e for e in epl if e[1] == 0x08]
if audio_off:
    if in_ep or out_ep: fail.append("audio off but endpoint 8 described")
    if eps.get(8) != 0: fail.append("audio off, ENDPT8 = %02x" % eps.get(8, -1))
else:
    want_pkt = 180 if mode == 1 else 990
    if len(in_ep) != 1 or in_ep[0][3] != want_pkt or in_ep[0][2] != 0x05: fail.append("IN endpoint %s" % in_ep)
    fb_ep = [e for e in epl if e[1] == 0x8b]
    if inf and mode == 1:
        if len(out_ep) != 1 or out_ep[0][3] != 180 or out_ep[0][2] != 0x05 or out_ep[0][0] != base + 2: fail.append("OUT endpoint %s" % out_ep)
        if len(fb_ep) != 1 or fb_ep[0][3] != 3 or fb_ep[0][2] != 0x11 or fb_ep[0][0] != base + 2: fail.append("feedback endpoint %s" % fb_ep)
        raw = [b[k:k + 9] for k in range(len(b) - 8) if b[k] == 9 and b[k + 1] == 5 and b[k + 2] == 0x08]
        if not raw or raw[0][8] != 0x8b: fail.append("OUT bSynchAddress %s" % (raw and raw[0][8]))
        if eps.get(8) != 0x1c: fail.append("ENDPT8 = %02x, want 1c" % eps.get(8, -1))
        if eps.get(11) != 0x14: fail.append("ENDPT11 = %02x, want 14" % eps.get(11, -1))
        if iad != (4, 3): fail.append("IAD %s" % (iad,))
        if hdr is None or hdr[7] != 2 or hdr[8:10] != [5, 6]: fail.append("AC header %s" % (hdr,))
        elif (hdr[5] | hdr[6] << 8) != len(hdr) + sum(len(t[2]) for t in terms.values()): fail.append("AC header wTotalLength %d" % (hdr[5] | hdr[6] << 8))
        if terms.get(3, (0, 0))[1] != 0x0101 or terms.get(4, (0, 0))[1] != 0x0301: fail.append("terminals %s" % terms)
        alt = [(n, a) for n, a in ifs if n == 6]
        if alt != [(6, 0), (6, 1)]: fail.append("OUT alternates %s" % alt)
        if terms.get(4, (0, 0, [0] * 9))[2][7] != 3: fail.append("speaker not fed by terminal 3")
    else:
        # flag off, or 8+3 mono with the flag (the OUT interface left out: periodic budget)
        if out_ep or fb_ep: fail.append("no OUT interface expected but an OUT or feedback endpoint is described")
        if eps.get(8) != 0x14: fail.append("ENDPT8 = %02x, want 14" % eps.get(8, -1))
        if eps.get(11, 0) != 0: fail.append("ENDPT11 = %02x, want 0" % eps.get(11, -1))
        if iad != (4, 2): fail.append("IAD %s" % (iad,))
if not audio_off:
    want = 2 if mode == 1 else 11
    if fmts.get(5) != want: fail.append("TX format channels %s want %d" % (fmts.get(5), want))
    if terms.get(1, (0, 0, [0] * 12))[2][7] != want: fail.append("input terminal 1 channels %d want %d" % (terms[1][2][7], want))
    if inf and mode == 1 and (fmts.get(6) != 2 or terms.get(3, (0, 0, [0] * 12))[2][7] != 2): fail.append("RX channels %s %s" % (fmts.get(6), terms.get(3)))
if drive:
    msc_if = [n for n, a in ifs if n == nif - 1]
    mep = [e for e in epl if e[1] in (0x89, 0x0a)]
    if len(mep) != 2 or mep[0][0] != nif - 1: fail.append("MSC endpoints %s on interface %s (last is %d)" % (mep, mep and mep[0][0], nif - 1))
print(("ok    " if not fail else "FAIL  ") + name + ": %d B, %d interfaces, endpoints %s%s" % (served, nif, ["%02x/%d" % (e[1], e[3]) for e in epl], "" if not fail else "  " + "; ".join(fail)))
sys.exit(1 if fail else 0)
