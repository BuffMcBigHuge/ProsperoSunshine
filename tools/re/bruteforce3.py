# SPDX-License-Identifier: GPL-3.0-only
"""Third-stage NID brute force (multiprocess): larger vocabulary, up to 3 words."""
import sys, os, itertools, multiprocessing as mp
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid
CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")
VERBS = "Get Set Change Enable Disable Is Request Notify Wait Clear Poll Invalidate Cancel Update Reset Query Register Unregister Select Force Insert Apply Check Send Take Reserve Release Lock Unlock Flush Mark Add Remove Create Destroy Allocate Free Set Reopen Switch Toggle Begin End Pause Resume Skip".split()
W = """Video Audio Haptic Pad Spk Speaker Mic Volume Mute Privacy Image Paste ScanIn Second Screen Hdr Sdr Event Flag
Status Info Mode Out Output Input Encode Encoder Config Param Params Rate Bitrate Frame Frames Key Idr Reference
Invalid Invalidate Invalidation Picture Control Gop Interval Size Resolution Memory Buffer Handle Stream Nal Header
Format Codec Sample Timestamp Time Latency Priority Resource Id User Session Client Count Max Min Limit Capability
Support Protect Protection Normal Restrict Share Full Aux Kind Type Purpose Target Source Device Name Version Debug
Fps Qp Slice Level Profile Preset Quality Thread Cpu Mask Bit Depth Channel Layout Rate Mix Mixer Balance Gain Level
Ain Aout Venc Ref Refs Scan In Remote Play RemotePlay Live Streaming Record Recording Game App Application Title Home
Menu Dialog System Notification Cursor Blur Border Frame Region Rect Crop Scale Position Offset Alpha Blend Enable
Active Idle Busy Ready Pending Count Num Total Used Free Available Read Write Pts Dts Sync Clock Epoch Origin Offset
Start Stop Pause Seq Sequence Number Index Key Slot Pool Pooled Ring Queue Shared Share Exclusive Owner Pid App""".split()
W = list(dict.fromkeys(W))
def work(v):
    lib = Elf(os.path.join(CACHE, "libSceAvcap2.elf"))
    want = {s["name"].split("#")[0] for s in lib.symbols() if s["value"]}
    out = []
    for k in (1, 2, 3):
        for combo in itertools.product(W, repeat=k):
            name = "sceAvcap2" + v + "".join(combo)
            if nid(name) in want:
                out.append((nid(name), name))
    return out
if __name__ == "__main__":
    with mp.Pool(max(1, mp.cpu_count() - 1)) as p:
        for res in p.imap_unordered(work, VERBS):
            for h, n in res: print(h, n, flush=True)
