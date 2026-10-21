"""Global-variable references per function: PS2 (MIPS lui/addiu, gp-relative) and Xbox (generated C)."""
import re, struct, pickle, bisect
from pathlib import Path
ROOT = Path(r"D:\Projekte\rePOP_ff")
LINE = re.compile(r"^\s+([0-9A-F]{8}) ([0-9A-F]{8}) (\S+)\s+(\S+)\s+\((.*)\)")
GP = 0x004E5470
def ps2_globals():
    data = []
    for l in open(ROOT / "research/ps2-proto/MAPFILE.MAP", encoding="latin1"):
        m = LINE.match(l)
        if m and m.group(3) in (".data", ".sdata", ".sbss", ".bss", ".rodata") and not m.group(4).startswith(("@", ".")):
            data.append((int(m.group(1), 16), int(m.group(2), 16), m.group(4)))
    data.sort(); starts = [d[0] for d in data]
    def sym(a):
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and data[i][0] <= a < data[i][0] + max(data[i][1], 1): return data[i][2]
    pf, pc, xs, xc, xf = pickle.load(open(ROOT / "research/match/graphs-xbox-retail.pkl", "rb"))
    e = open(ROOT / "research/ps2-proto/SLUS_207.43", "rb").read()
    phoff, = struct.unpack_from("<I", e, 0x1C); phnum, = struct.unpack_from("<H", e, 0x2C)
    segs = [struct.unpack_from("<8I", e, phoff + i * 32) for i in range(phnum)]
    def words(va, n):
        for _, off, vaddr, _, filesz, _, _, _ in segs:
            if vaddr <= va < vaddr + filesz: return struct.unpack_from("<%dI" % n, e, off + va - vaddr)
        return ()
    LOADSTORE = {0x08, 0x09, 0x0D, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x39, 0x37, 0x3F, 0x1E, 0x1F}
    refs = {}
    for a, (name, size, lib) in pf.items():
        hi = {}; out = set()
        for w in words(a, size // 4):
            op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            if op == 0x0F: hi[rt] = imm << 16; continue
            if op in LOADSTORE or op == 0x0D:
                if rs == 28: addr = GP + simm
                elif rs in hi: addr = (hi[rs] + (imm if op == 0x0D else simm)) & 0xFFFFFFFF
                else: continue
                s = sym(addr)
                if s: out.add(s)
        refs[a] = out
    return refs
def xbox_globals(target, lo=0x00418080, hi=0x0076C6E0):
    gen = ROOT / "source" / target / "gen"
    func = re.compile(r"^void sub_([0-9A-F]{8})\(void\)", re.M)
    ref = re.compile(r"MEM(?:8|16|32|64|F|D)?\(0x([0-9A-F]{5,8})\)|\b0x([0-9A-F]{6,8})u?\b")
    refs = {}
    for p in gen.rglob("*.c"):
        t = p.read_text(errors="replace")
        starts = [(m.start(), int(m.group(1), 16)) for m in func.finditer(t)] + [(len(t), None)]
        for (s, f), (e2, _) in zip(starts, starts[1:]):
            out = set()
            for m in ref.finditer(t, s, e2):
                v = int(m.group(1) or m.group(2), 16)
                if lo <= v < hi: out.add(v)
            refs[f] = out
    return refs
if __name__ == "__main__":
    import sys
    pg = ps2_globals(); print("PS2 functions with global refs:", sum(1 for v in pg.values() if v), "distinct globals:", len(set().union(*pg.values())))
    for target, lo, hi in (("xbox-retail", 0x00418080, 0x0076C6E0),):
        xg = xbox_globals(target, lo, hi); print(target, "functions with refs:", sum(1 for v in xg.values() if v), "distinct addresses:", len(set().union(*xg.values())))
        pickle.dump((pg, xg), open(ROOT / ("research/match/globals-%s.pkl" % target), "wb"))
