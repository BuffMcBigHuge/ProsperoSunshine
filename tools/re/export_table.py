# SPDX-License-Identifier: GPL-3.0-only
"""Print the libSceAvcap2 export table: NID, offset, resolved name (from
avcap2_names.txt), IPMI method id and buffer shapes of the stub it calls.
Reads .cache/re/libSceAvcap2.elf only.
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid
import disasm as D
import ipcstubs

CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")
e = Elf(os.path.join(CACHE, "libSceAvcap2.elf"))
names = {}
p = os.path.join(os.path.dirname(__file__), "avcap2_names.txt")
for l in open(p):
    l = l.strip()
    if l and not l.startswith("#"): names[nid(l)] = l
stubs = {a: ipcstubs.summarize(ins) for a, ins in ipcstubs.find_stubs(e).items()}
rows = []
for s in e.symbols():
    if not s["value"]: continue
    n = s["name"].split("#")[0]
    calls = []
    for ins in D.disasm(e, s["value"], s["size"]):
        m = re.search(r"call\s+0x([0-9a-f]+)", ins)
        if m and int(m.group(1), 16) in stubs and stubs[int(m.group(1), 16)][0]:
            calls.append(int(m.group(1), 16))
    ipc = ""
    if calls:
        # skip the generic Close stub (0xa70004) used on error paths of the Open* wrappers
        pick = [c for c in calls if stubs[c][0] != "#0xa70004"] or calls
        a = pick[0]; i, ins_, outs = stubs[a]
        ipc = "%s in=%s out=%s" % (i.lstrip("#"), ",".join(ins_), ",".join(outs))
    rows.append((s["value"], n, names.get(n, "unbekannt"), s["size"], ipc))
for v, n, name, sz, ipc in sorted(rows):
    print("%-12s 0x%04x %4d %-42s %s" % (n, v, sz, name, ipc))
