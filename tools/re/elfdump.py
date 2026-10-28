# SPDX-License-Identifier: GPL-3.0-only
"""Shared helpers for reading decrypted PS5 ELFs (no Sony data embedded).

Parses program headers, the dynamic section (incl. SCE tags), the dynamic
symbol table and the JMPREL/RELA relocations. Module-relative offset ==
vaddr for these files; file offset is derived from the LOAD segments.
"""
import struct, hashlib, base64, os, sys

NID_KEY = bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")

def nid(name: str) -> str:
    d = hashlib.sha1(name.encode() + NID_KEY).digest()[:8]
    # observed: the 8 digest bytes are byte-reversed before base64 (verified below)
    return base64.b64encode(d[::-1]).decode()[:11].replace("/", "-")

# self test against values observed with prospero-nid in the SDK image
assert nid("sceAvcap2Initialize") == "svzPXluOz8U"
assert nid("sceAvcap2OpenVideo") == "4jwUgirLFrA"

class Elf:
    def __init__(self, path):
        self.path = path
        self.d = open(path, "rb").read()
        d = self.d
        (self.e_phoff,) = struct.unpack_from("<Q", d, 0x20)
        self.phnum, = struct.unpack_from("<H", d, 0x38)
        self.ph = []
        for i in range(self.phnum):
            t, fl, off, va, pa, fsz, msz, al = struct.unpack_from("<IIQQQQQQ", d, self.e_phoff + 56 * i)
            self.ph.append(dict(type=t, flags=fl, off=off, va=va, fsz=fsz, msz=msz))
        self.dyn = self._dynamic()
        self.strtab = self.dyn[5][0]
        self.strsz = self.dyn[10][0]
        self.symtab = self.dyn[6][0]

    def v2o(self, va):
        for p in self.ph:
            if p["type"] == 1 and p["va"] <= va < p["va"] + p["fsz"]:
                return p["off"] + va - p["va"]
        raise KeyError(hex(va))

    def read(self, va, n):
        o = self.v2o(va)
        return self.d[o:o + n]

    def _dynamic(self):
        dd = {}
        self.dyn_list = []
        for p in self.ph:
            if p["type"] == 2:
                for i in range(p["fsz"] // 16):
                    tag, val = struct.unpack_from("<qQ", self.d, p["off"] + 16 * i)
                    self.dyn_list.append((tag, val))
                    dd.setdefault(tag, []).append(val)
        return dd

    def cstr(self, off):
        o = self.v2o(self.strtab + off)
        e = self.d.index(b"\0", o)
        return self.d[o:e].decode("latin1")

    def symbols(self):
        out = []
        n = struct.unpack_from("<II", self.read(self.dyn[4][0], 8))[1]  # DT_HASH nchain
        for i in range(n):
            nm, info, oth, shn, val, sz = struct.unpack_from("<IBBHQQ", self.read(self.symtab + 24 * i, 24))
            out.append(dict(i=i, name=self.cstr(nm), info=info, shndx=shn, value=val, size=sz))
        return out

    def tls_free_rela(self, tab, size):
        out = []
        for i in range(size // 24):
            off, info, add = struct.unpack_from("<QQq", self.read(tab + 24 * i, 24))
            out.append((off, info & 0xffffffff, info >> 32, add))
        return out

    def plt_relocs(self):
        return self.tls_free_rela(self.dyn[23][0], self.dyn[2][0])

    def rela(self):
        return self.tls_free_rela(self.dyn[7][0], self.dyn[8][0])

if __name__ == "__main__":
    e = Elf(sys.argv[1])
    for t, v in e.dyn_list:
        print(hex(t), hex(v))
    for s in e.symbols():
        print(s["i"], s["name"], hex(s["info"]), s["shndx"], hex(s["value"]), s["size"])


def func_starts(e):
    """Function start addresses from PT_GNU_EH_FRAME's binary search table."""
    import struct as st
    for p in e.ph:
        if p["type"] == 0x6474e550:
            hdr = p["va"]; d = e.read(hdr, 16)
            ver, enc_ptr, enc_cnt, enc_tab = d[0], d[1], d[2], d[3]
            # assume enc_ptr=0x1b (pcrel sdata4), cnt=0x03 (udata4), tab=0x3b (datarel sdata4)
            pos = hdr + 4
            # eh_frame_ptr
            pos += 4
            (cnt,) = st.unpack_from("<I", e.read(pos, 4)); pos += 4
            tab = e.read(pos, 8 * cnt)
            out = []
            for i in range(cnt):
                a, f = st.unpack_from("<ii", tab, 8 * i)
                out.append(hdr + a)
            return out
    return []
