# SPDX-License-Identifier: GPL-3.0-only
"""Second-stage NID brute force: sceAvcap2 + verb + 1..3 words from a vocabulary."""
import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid
CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")
VERBS = "Get Set Open Close Read Start Stop Request Enable Disable Is Notify Wait Map Unmap Query Register Unregister Reset Update Change Select Clear Poll Create Destroy Release Force Insert Invalidate Cancel Pause Resume Flush Lock Unlock Add Remove Apply Check Begin End Send Post Take Release".split()
W = """Video Audio Haptic Pad Spk Speaker Voice Chat Mic Microphone Mix Volume Mute Privacy Image Paste Overlay ScanIn
Second Screen Hdr ColorSpace EventFlag Event Flag Status Info Mode Out Output Input Encode Encoder Config Param Params
Rate Bitrate Frame Frames Key Idr Reference Invalidation Invalidate Picture Rc Control Gop Interval Size Resolution
Memory Buffer Dma Handle Stream Nal Unit NonVcl Header Format Codec Sample Samples Timestamp Time Clock Pts Latency
Delay Priority Resource Arbitrator Id User Session Client Pid Count Max Min Limit Capability Support Supported Protect
Protection Normal Restrict Share Full Aux Cronos Kind Type Purpose Target Source Device Name Version Debug Log
Stat Stats Statistics Error Fence Vsync Lock Unlock Skip Drop Repeat Fps Qp Slice Level Profile Preset Quality
Lowlatency LowLatency Latency Realtime Priority Thread Cpu Affinity Wakeup Notification Callback Mask Bit Bits
Depth Channel Channels Layout Rate Sampling Interleave Mono Stereo Surround""".split()
VERBS = list(dict.fromkeys(VERBS))
if __name__ == "__main__":
    lib = Elf(os.path.join(CACHE, "libSceAvcap2.elf"))
    want = {s["name"].split("#")[0] for s in lib.symbols() if s["value"]}
    done = set(sys.argv[1:])
    found = {}
    n = 0
    for v in VERBS:
        for k in (1, 2, 3):
            if k == 3 and len(W) > 90: pass
            for combo in itertools.product(W, repeat=k) if k < 3 else itertools.product(W[:70], repeat=3):
                name = "sceAvcap2" + v + "".join(combo)
                n += 1
                h = nid(name)
                if h in want and h not in found:
                    found[h] = name
                    print(h, name, flush=True)
    print("tried", n, "found", len(found), file=sys.stderr)
