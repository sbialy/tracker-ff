# Emu parity T7: bridge proof, emu and unit

Host tool notes, NOT firmware. Image `make original-release BUILD_TAG=01c3ab2` (ptf sha256 b6af44e4...2aa47da, modit.plg ccf3658e...dd8bda1,
both unchanged by this work). The unit ran that image's code under the tag ep3img0 with the PARITY=1 console module (poly-devtools ep-t7
8294566, load part 47072 of 47104 B, bound to image crc 0xeb27d3df).

## Bridge against the reference

`bridge_check.sh` (this directory) runs make bridge-test, synth-start-test, bridge-fx-ref + bridge-fx-test, emu_plug.py --image-libm over the
card inserts and bridge_check.py. Reference: poly-engines tools/emu_render.py --image-libm with render_harness.plg.

- 14 card synths, 1840 runs, 162 checks, 0 failed (41 s, the reference without hooks): every run byte equal, nothing pinned
- 8 master inserts and 9 Sample Editor .ptfx byte equal to fx_ref.py
- 1050 of 1050 start() variants agree

### Former reference defect: 8TYPE sweep run "random 5" (fixed, a Unicorn memory hook)

The reference used to differ from the bridge in 11876 of 12288 samples, first at block 1 sample 78. The unit settled it: `plugrender 8TYPE`
of the whole sweep transcript gives fnv 3f283215 on the unit and the bridge, and run "random 5" alone gives ccfb1ce9 on both. The reference
gave a87a7f2d alone and ca1c45a5 after the earlier runs. The trigger was osc2 Wave + Sync with osc3 Wave + Level + Sync (params 10 17 20 23 27).

Cause: emu_render.Run installed its CPU counting hooks even with count=False, because `count = self.count` rebinds the argument to the
counter list `[0, 0]`, which is always truthy. One of those hooks is a UC_HOOK_MEM_READ on the engine's work memory, and 8TYPE keeps its
sync Links there (EIGHT_WORK_POOL). In voice 3's linked oscillator pass (oscPassT, eight.plg 0x1fff7bfa `itt eq` then `vldreq s15,
[sb, #0x104]`, the read of the master's wrap position) the hook fires on the predicated load. Unicorn 2.1.4 restores CPU state for the
hook from that instruction's metadata and writes its IT condexec bits back into CPSR (0x600009f3 where the run without the hook has
0x600001f3). Nothing clears them at the end of the IT block, so the next translation block starts as if it were still in the IT block
and a branch goes the wrong way. The first wrong Link write is at frame 78 of block 1. Evidence, same Python driver and inputs:

- every hook removed: ccfb1ce9 (= unit and bridge). Only the work memory read hook kept: a87a7f2d. Only its write half, only the
  per instruction hook, or a read hook on an unrelated page: ccfb1ce9
- the harness was cleared first: a Python driver that calls the engine's note_on / prepare / render directly gives a87a7f2d too. The
  engine makes no host calls in the run (the stubs see only the harness's cycles and query)

Fix (poly-engines ep-t7-refhook): Run honours count=False and installs no hook, which is what transcript_runs and bridge_check use.
The reference then gives 3f283215 for the whole sweep and ccfb1ce9 for the run. bridge_check.py no longer pins any run (REF_DEFECTS
is gone). A count=True Run (the CPU estimates of run_checks.py, tools/alco/devcost_cpu.py) still hooks the work memory, so its audio
is not bit exact for an engine with predicated loads from it.

## Unit against the bridge (console plugrender, per block FNV-1a)

One transcript per engine (`--unit-dir`, 7 runs, 272 blocks). Every block of the unit equals the bridge and the reference.

| engine | fnv | engine | fnv |
|---|---|---|---|
| ALCO | 971c934d | DTYPE | 16eecf59 |
| ARCTYPE | 3d4581d5 | FM4 | f6051529 |
| ATYPE | a6a189f1 | HANDTYPE | e5db28b1 |
| BTYPE | c85a8bb9 | JU6 | 6a6de6c5 |
| CHIP | 49686219 | PHZ | 9421a381 |
| CSTYPE | 05fe5235 | PLK | 2ddec471 |
| 8TYPE | aabd55b5 | TTYPE | e96f4151 |

PLK repeatability: the console now resets the engine work before every plugrender. Two consecutive PLK runs on the unit both give
2ddec471, equal block by block. The bridge reports "work reset" for PLK, CHIP, 8TYPE, TTYPE and ALCO.

## Bridge guest cost per block (host_ucbridge, min to max over the sessions)

PLK 97-156 us, TTYPE 103-140, HANDTYPE 107-184, ALCO 118-603, BTYPE 135-185, JU6 136-394, 8TYPE 172-293, FM4 189-341, PHZ 196-460,
ATYPE 209-226, ARCTYPE 210-389, DTYPE 226-362, CSTYPE 227-361, CHIP 358-687. 2.0 to 3.2 guest calls per block.

## fw_engine_mismatch_sim.sh

Plain: GREEN 102. Its 4 former ABI REDs are asserted checks pinned by abi_contract.txt (mismatch_known_findings.txt). BRIDGE=1:
GREEN 104 after a re-baseline. The bridge gives the seeded JU6 and PLK parts their engine's Init patch at start, so 1.patch and 2.patch
get written. That is a fixture artifact, checked as "129 138" by seeded_init, and the write sums cover project.mt and 0.patch.
