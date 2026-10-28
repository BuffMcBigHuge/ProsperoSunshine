# SPDX-License-Identifier: GPL-3.0-only
"""Disassemble a function of a PS5 ELF from .cache/re, annotating GOT/PLT
calls with the import symbol (NID#lib#mod) they resolve to.

usage: disasm.py <elf> <hex-offset> [length]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf
import capstone

def got_map(e):
    """GOT slot vaddr -> symbol name, from JMPREL + RELA (GLOB_DAT/JUMP_SLOT)."""
    syms = e.symbols()
    m = {}
    for off, typ, si, add in e.plt_relocs() + e.rela():
        if si and si < len(syms):
            m[off] = syms[si]["name"]
    return m

def disasm(e, start, length, names=None, exports=None):
    gm = got_map(e)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    code = e.read(start, length)
    out = []
    import re
    def plt_target(t):
        # PLT stub: jmp qword ptr [rip + X]
        try:
            ins = list(md.disasm(e.read(t, 8), t, 1))
        except KeyError:
            return None
        if ins and ins[0].mnemonic == "jmp" and "rip" in ins[0].op_str:
            m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", ins[0].op_str)
            g = ins[0].address + ins[0].size + int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)
            return gm.get(g)
        return None
    for i in md.disasm(code, start):
        ann = ""
        if i.mnemonic == "call" and i.op_str.startswith("0x"):
            t = int(i.op_str, 16)
            n = plt_target(t)
            if n:
                ann = "  ; " + (names.get(n.split('#')[0], n) if names else n)
            elif exports and t in exports:
                ann = "  ; <" + exports[t] + ">"
        if "rip" in i.op_str:
            # compute rip-relative target
            import re
            m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", i.op_str)
            if m and i.mnemonic != "call":
                t = i.address + i.size + (int(m.group(2), 16) * (1 if m.group(1) == "+" else -1))
                if t in gm:
                    n = gm[t]
                    ann = "  ; " + (names.get(n.split('#')[0], n) if names else n)
                else:
                    ann = "  ; [%#x]" % t
        out.append("%#06x  %-6s %s%s" % (i.address, i.mnemonic, i.op_str, ann))
    return out

if __name__ == "__main__":
    e = Elf(sys.argv[1])
    start = int(sys.argv[2], 16)
    n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x100
    print("\n".join(disasm(e, start, n)))
