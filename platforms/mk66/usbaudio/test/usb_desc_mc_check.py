# usb_desc_test.sh helper: the 8+3 mono configuration against the stereo one. argv: stereo bytes, 8+3 bytes, chname lines.
import sys
s = [int(x, 16) for x in sys.argv[1].split()]
m = [int(x, 16) for x in sys.argv[2].split()]
names = [l for l in sys.argv[3].splitlines() if l]
bad = []


def walk(b):
    i, out = 0, []
    while i < len(b):
        out.append(b[i:i + b[i]])
        i += b[i]
    return out


d = walk(m)
it = [x for x in d if x[1] == 0x24 and x[2] == 2 and len(x) == 12][0]
fmt = [x for x in d if x[1] == 0x24 and x[2] == 2 and len(x) == 11][0]
ep = [x for x in d if x[1] == 5 and x[2] == 0x88][0]
if it[7] != 11 or (it[8] | it[9] << 8) != 0 or it[10] != 0x10:
    bad.append("input terminal %s" % it)
if fmt[4] != 11 or fmt[5] != 2 or fmt[6] != 16 or (fmt[8] | fmt[9] << 8 | fmt[10] << 16) != 44100:
    bad.append("format %s" % fmt)
if (ep[4] | ep[5] << 8) != 990 or ep[3] != 5 or ep[6] != 1:
    bad.append("endpoint %s" % ep)
diff = [i for i in range(min(len(s), len(m))) if s[i] != m[i]]
if len(s) != len(m) or len(diff) != 6:
    bad.append("stereo vs 8+3 differ at %d bytes %s" % (len(diff), diff))
want = ["Track %d" % i for i in range(1, 9)] + ["Synth %d" % i for i in range(1, 4)]
got = []
for l in names:
    h = [int(x, 16) for x in l.split(':')[1].split()]
    if h[0] != len(h) or h[1] != 3:
        bad.append("string header %s" % l)
    got.append(''.join(chr(h[k] | h[k + 1] << 8) for k in range(2, len(h), 2)))
if got != want or not names or not names[0].startswith("chname 0310"):
    bad.append("names %s" % got)
print(("ok    " if not bad else "FAIL  ") + "8+3: input terminal and format 11 ch 16 bit 44100 Hz, wChannelConfig 0, iChannelNames 0x10, "
      "EP8 990 B async every frame, %d bytes differ from stereo, names %s..%s" % (len(diff), got[0] if got else '-', got[-1] if got else '-')
      + ("" if not bad else " :: " + "; ".join(bad)))
sys.exit(1 if bad else 0)
