# SPDX-License-Identifier: GPL-3.0-only
"""List which libSceAvcap2 exports each consumer ELF imports and where it
calls them (via PLT stubs / GOT slots). Reads ELFs from .cache/re.
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid
import capstone

B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")

def lib_suffix(e, libname):
    mod = lib = None
    for t, v in e.dyn_list:
        if t == 0x61000045 and e.cstr(v & 0xffffffff) == libname: mod = v >> 48
        if t == 0x61000049 and e.cstr(v & 0xffffffff) == libname: lib = v >> 48
    return "#%s#%s" % (B64[lib], B64[mod])

def imports_of(e, libname):
    suf = lib_suffix(e, libname)
    return [s for s in e.symbols() if s["name"].endswith(suf)], suf

def got_slots(e):
    d = {}
    syms = e.symbols()
    for off, typ, si, add in e.plt_relocs() + e.rela():
        if si: d.setdefault(si, []).append(off)
    return d

def call_sites(e, symidx_list):
    """Find 'call [rip+x]' or calls to PLT stubs that go through those GOT slots."""
    gs = got_slots(e)
    want = {}
    for i in symidx_list:
        for slot in gs.get(i, []): want[slot] = i
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    res = {i: [] for i in symidx_list}
    stubs = {}
    for p in e.ph:
        if p["type"] == 1 and p["flags"] & 1:
            code = e.d[p["off"]:p["off"] + p["fsz"]]
            base = p["va"]
            # PLT stubs: ff 25 rel32
            for m in re.finditer(rb"\xff\x25(....)", code, re.S):
                pos = m.start(); rel = int.from_bytes(m.group(1), "little", signed=True)
                tgt = base + pos + 6 + rel
                if tgt in want: stubs[base + pos] = want[tgt]
            for m in re.finditer(rb"[\xe8\xe9](....)", code, re.S):
                pos = m.start(); rel = int.from_bytes(m.group(1), "little", signed=True)
                tgt = base + pos + 5 + rel
                if tgt in stubs: res[stubs[tgt]].append(base + pos)
            for m in re.finditer(rb"\xff[\x15\x25](....)", code, re.S):
                pos = m.start(); rel = int.from_bytes(m.group(1), "little", signed=True)
                tgt = base + pos + 6 + rel
                if tgt in want: res[want[tgt]].append(base + pos)
    return res, stubs

if __name__ == "__main__":
    allexp = {}
    for l in open(os.path.join(os.path.dirname(__file__), "avcap2_names.txt")):
        l = l.strip()
        if l and not l.startswith("#"): allexp[nid(l)] = l
    lib = Elf(os.path.join(CACHE, "libSceAvcap2.elf"))
    exports = {s["name"].split("#")[0]: s for s in lib.symbols() if s["value"]}
    for f in sys.argv[1:] or ["SceRemotePlay", "SceGameLiveStreaming"]:
        e = Elf(os.path.join(CACHE, f + ".elf"))
        syms, suf = imports_of(e, "libSceAvcap2")
        res, stubs = call_sites(e, [s["i"] for s in syms])
        print("== %s (suffix %s): %d imports" % (f, suf, len(syms)))
        for s in syms:
            n = s["name"].split("#")[0]
            ex = exports.get(n)
            print("  %s %-34s export@%s  callsites=%s" % (n, allexp.get(n, "?"), hex(ex["value"]) if ex else "-",
                  " ".join(hex(c) for c in res[s["i"]])))
