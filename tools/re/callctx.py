# SPDX-License-Identifier: GPL-3.0-only
"""Show the instructions preceding call sites in a consumer ELF, with
PLT/GOT calls annotated by NID/known name.

usage: callctx.py <ElfName> <hex-callsite> [before_bytes_limit] [after]
"""
import sys, os, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid, func_starts
import disasm as D, capstone, re

CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")

def load_names():
    names = {}
    for fn in ("common_names.txt", "avcap2_names.txt", "consumer_names.txt"):
        p = os.path.join(os.path.dirname(__file__), fn)
        if os.path.exists(p):
            for l in open(p):
                l = l.strip()
                if l and not l.startswith("#"): names[nid(l)] = l
    return names

def context(e, site, after=0x40, names=None, nback=None):
    fs = func_starts(e)
    i = bisect.bisect_right(fs, site) - 1
    start = fs[i]
    end = site + after
    return D.disasm(e, start, end - start, names or load_names())

if __name__ == "__main__":
    e = Elf(os.path.join(CACHE, sys.argv[1] + ".elf"))
    site = int(sys.argv[2], 16)
    after = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x40
    lines = context(e, site, after)
    # print only the last N lines before site and a few after
    idx = [k for k, l in enumerate(lines) if l.startswith("%#06x" % site)]
    k = idx[0] if idx else len(lines)
    nb = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    print("\n".join(lines[max(0, k - nb):]))
