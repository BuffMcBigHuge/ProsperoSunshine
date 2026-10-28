# SPDX-License-Identifier: GPL-3.0-only
"""Summarise the IPMI stub functions of libSceAvcap2 (method id, input/output
buffer shapes) with a tiny symbolic evaluator. Reads the ELF from .cache/re.

Each stub loads rdi=client, builds up to N in-buffer descriptors (ptr,len) on
the stack and calls vtable[+0x58](client, method, in*, nIn, &rc, out*, nOut).
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf
import capstone
from capstone.x86 import *

ARGS = {X86_REG_RDI: "client", X86_REG_RSI: "a1", X86_REG_RDX: "a2", X86_REG_RCX: "a3", X86_REG_R8: "a4", X86_REG_R9: "a5"}
SUB = {}
def canon(r):
    n = {X86_REG_EDI: X86_REG_RDI, X86_REG_ESI: X86_REG_RSI, X86_REG_EDX: X86_REG_RDX, X86_REG_ECX: X86_REG_RCX,
         X86_REG_R8D: X86_REG_R8, X86_REG_R9D: X86_REG_R9, X86_REG_EAX: X86_REG_RAX, X86_REG_EBX: X86_REG_RBX,
         X86_REG_DL: X86_REG_RDX, X86_REG_CL: X86_REG_RCX, X86_REG_SIL: X86_REG_RSI, X86_REG_AL: X86_REG_RAX,
         X86_REG_R10D: X86_REG_R10, X86_REG_R11D: X86_REG_R11}
    return n.get(r, r)

def find_stubs(e, text_lo=0x43e0, text_hi=0x5c20):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    code = e.read(text_lo, text_hi - text_lo)
    funcs, cur = {}, None
    for i in md.disasm(code, text_lo):
        if i.mnemonic == "push" and i.op_str == "rbp" and (cur is None or funcs[cur][-1].mnemonic in ("int3", "ud2")):
            cur = i.address; funcs[cur] = []
        if cur is not None:
            funcs[cur].append(i)
    return funcs

def summarize(ins):
    reg = {r: n for r, n in ARGS.items()}
    mem = {}  # rbp offset -> (value expr, size)
    ipc = None; nin = nout = None; din = dout = None; rsp0 = None
    for i in ins:
        mn = i.mnemonic; ops = i.operands
        def val(op):
            if op.type == X86_OP_IMM: return "#%#x" % (op.imm & 0xffffffffffffffff)
            if op.type == X86_OP_REG: return reg.get(canon(op.reg), "?")
            if op.type == X86_OP_MEM:
                m = op.mem
                if m.base == X86_REG_RBP and m.index == 0: return "mem%+d" % m.disp
            return "?"
        if mn in ("mov", "movzx", "movsxd", "movsx") and len(ops) == 2:
            d, s = ops
            if d.type == X86_OP_REG:
                v = val(s)
                if mn == "movsxd": v = "sx(%s)" % v
                reg[canon(d.reg)] = v
            elif d.type == X86_OP_MEM and d.mem.base == X86_REG_RBP:
                mem[d.mem.disp] = (val(s), d.size)
                if d.mem.base == X86_REG_RSP: pass
            elif d.type == X86_OP_MEM and d.mem.base == X86_REG_RSP and d.mem.disp == 0:
                rsp0 = val(s)
        elif mn == "lea" and ops[1].mem.base == X86_REG_RBP:
            reg[canon(ops[0].reg)] = "&%+d" % ops[1].mem.disp
        elif mn == "xor" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
            reg[canon(ops[0].reg)] = "#0"
        elif mn == "shl" and ops[0].type == X86_OP_REG:
            reg[canon(ops[0].reg)] = "(%s<<%d)" % (reg.get(canon(ops[0].reg), "?"), ops[1].imm)
        elif mn == "call" and ops[0].type == X86_OP_MEM and ops[0].mem.disp == 0x58:
            ipc = reg.get(X86_REG_RSI)
            din, nin = reg.get(X86_REG_RDX), reg.get(X86_REG_RCX)
            dout, nout = reg.get(X86_REG_R9), rsp0
            retp = reg.get(X86_REG_R8)
            break
    def descr(base, n):
        if not base or not base.startswith("&"): return [base]
        off = int(base[1:]); out = []
        try: cnt = int(n.lstrip("#"), 16)
        except Exception: cnt = 0
        for k in range(cnt):
            p = mem.get(off + 16 * k, ("?", 0)); l = mem.get(off + 16 * k + 8, ("?", 0))
            pv = p[0]
            # dereference pointer-to-local: report what is stored there
            if pv.startswith("&"):
                o = int(pv[1:]); w = mem.get(o)
                pv = "local(%s:%dB)" % (w[0], w[1]) if w else "local?"
            out.append("%s[%s]" % (pv, l[0]))
        return out
    return ipc, descr(din, nin or "#0"), descr(dout, nout or "#0")

if __name__ == "__main__":
    e = Elf(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re", "libSceAvcap2.elf"))
    for a, ins in sorted(find_stubs(e).items()):
        ipc, i, o = summarize(ins)
        if ipc:
            print("%#x ipc=%s in=%s out=%s" % (a, ipc, ", ".join(i), ", ".join(o)))
