# SPDX-License-Identifier: GPL-3.0-only
"""Disassemble the whole function containing an address (uses EH frame table).
usage: funcdump.py <ElfName> <hex-addr> [maxlines]
"""
import sys, os, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, func_starts
from callctx import load_names, CACHE
import disasm as D
e = Elf(os.path.join(CACHE, sys.argv[1] + ".elf"))
a = int(sys.argv[2], 16)
fs = func_starts(e)
i = bisect.bisect_right(fs, a) - 1
L = D.disasm(e, fs[i], fs[i + 1] - fs[i], load_names())
n = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
print("; func %#x-%#x" % (fs[i], fs[i + 1]))
print("\n".join(L[:n]))
