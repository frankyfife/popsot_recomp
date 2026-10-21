"""Build the PS2 (from MAPFILE.MAP + ELF) and Xbox (from generated C) call graphs."""
import re, struct, pickle, sys
from pathlib import Path
ROOT = Path(r"D:\Projekte\rePOP_ff")
LINE = re.compile(r"^\s+([0-9A-F]{8}) ([0-9A-F]{8}) (\S+)\s+(\S+)\s+\((.*)\)")

def ps2_graph(mapfile, elf):
    funcs = {}
    for line in open(mapfile, encoding="latin1"):
        m = LINE.match(line)
        if m and m.group(3) == ".text" and int(m.group(2), 16) > 0 and not m.group(4).startswith("."):
            a = int(m.group(1), 16)
            funcs.setdefault(a, (m.group(4), int(m.group(2), 16), m.group(5).split()[0] if m.group(5) else ""))
    e = open(elf, "rb").read()
    phoff, = struct.unpack_from("<I", e, 0x1C); phnum, = struct.unpack_from("<H", e, 0x2C)
    segs = [struct.unpack_from("<8I", e, phoff + i * 32) for i in range(phnum)]
    def word(va):
        for _, off, vaddr, _, filesz, _, _, _ in segs:
            if vaddr <= va < vaddr + filesz: return struct.unpack_from("<I", e, off + va - vaddr)[0]
        return None
    calls = {}
    for a, (name, size, lib) in funcs.items():
        out = []
        for pc in range(a, a + size, 4):
            w = word(pc)
            if w is None: break
            if (w >> 26) == 3:                       # jal
                t = (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if t in funcs and t not in out: out.append(t)
        calls[a] = out
    return funcs, calls

def xbox_graph(target):
    gen = ROOT / "source" / target / "gen"
    hdr = re.compile(r"/\*\*\n \* sub_([0-9A-F]{8})\n \* Original: 0x[0-9A-F]+ - 0x[0-9A-F]+ \((\d+) bytes")
    func = re.compile(r"^void sub_([0-9A-F]{8})\(void\)", re.M)
    call = re.compile(r"RECOMP_ABI_CALL\(0x([0-9A-F]{8})u")
    sizes, calls, folder = {}, {}, {}
    for p in gen.rglob("*.c"):
        t = p.read_text(errors="replace")
        for m in hdr.finditer(t): sizes[int(m.group(1), 16)] = int(m.group(2))
        starts = [(m.start(), int(m.group(1), 16)) for m in func.finditer(t)] + [(len(t), None)]
        for (s, f), (e2, _) in zip(starts, starts[1:]):
            out = []
            for m in call.finditer(t, s, e2):
                c = int(m.group(1), 16)
                if c not in out: out.append(c)
            calls[f] = out; folder[f] = p.relative_to(gen).parts[0]
    return sizes, calls, folder

if __name__ == "__main__":
    (ROOT / "research/match").mkdir(parents=True, exist_ok=True)
    pf, pc = ps2_graph(ROOT / "research/ps2-proto/MAPFILE.MAP", ROOT / "research/ps2-proto/SLUS_207.43")
    print("PS2 functions", len(pf), "edges", sum(len(v) for v in pc.values()))
    for target in ("xbox-retail", "xbox-demo"):
        xs, xc, xf = xbox_graph(target)
        print(target, "functions", len(xc), "edges", sum(len(v) for v in xc.values()))
        pickle.dump((pf, pc, xs, xc, xf), open(ROOT / ("research/match/graphs-%s.pkl" % target), "wb"))
