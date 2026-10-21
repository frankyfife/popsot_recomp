"""Propagate PS2 prototype names to an Xbox build through the call graphs."""
import pickle, random, collections, importlib.util, struct, sys, re
from pathlib import Path
ROOT = Path(r"D:\Projekte\rePOP_ff")
spec = importlib.util.spec_from_file_location("ain", ROOT / "tools/ps2-ai-names.py"); ain = importlib.util.module_from_spec(spec); spec.loader.exec_module(ain)
PLATFORM_LIBS = ("libkernl", "libgcc", "libm.", "libg.", "MSLGCC", "libcdvd", "libpad", "libsd", "Scee", "Ps2Graphic", "libvideo_ps2",
                 "moviemanager_ps2", "libgraph", "libdma", "libvu0", "libmc", "libmtap", "libpkt", "libipu", "libmpeg", "CRT0", "libscf", "libdev")
GAME_END = 0x00330000

def seeds(target_xbe):
    text, data = ain.map_symbols(ROOT / "research/ps2-proto/MAPFILE.MAP")
    a, s = data["AI2C_gat_fctdefs"]
    w = struct.unpack("<%dI" % (s // 4), ain.elf_reader(ROOT / "research/ps2-proto/SLUS_207.43")(a, s))
    ps2 = dict(zip(w[0::2], w[1::2]))
    _, pairs = ain.xbe_table(target_xbe, w[0], w[2])
    return {ps2[k]: x for k, x in pairs if k in ps2 and k != 0xFFFFFFFF}

def run(target, xbe, holdout=0.0, seed=1, verbose=True):
    pf, pc, xs, xc, xf = pickle.load(open(ROOT / ("research/match/graphs-%s.pkl" % target), "rb"))
    portable = {a for a, (n, s, lib) in pf.items() if not lib.startswith(PLATFORM_LIBS)}
    P = {a: [c for c in pc[a] if c in portable] for a in portable}
    X = {a: [c for c in v if c < GAME_END and c in xc] for a, v in xc.items() if a < GAME_END}
    S = seeds(xbe)
    S = {p: x for p, x in S.items() if p in P and x in X}
    rnd = random.Random(seed); items = sorted(S.items()); rnd.shuffle(items)
    cut = int(len(items) * holdout); hidden = dict(items[:cut]); p2x = dict(items[cut:]); x2p = {x: p for p, x in p2x.items()}
    psize = {a: pf[a][1] for a in pf}
    rounds = 0
    while True:
        rounds += 1; votes = collections.defaultdict(float)
        for p, x in list(p2x.items()):
            lp, lx = P.get(p, []), X.get(x, [])
            # align by anchors
            ia = [(i, lx.index(p2x[c])) for i, c in enumerate(lp) if c in p2x and p2x[c] in lx]
            # keep increasing chain
            chain, last = [], -1
            for i, j in ia:
                if j > last: chain.append((i, j)); last = j
            bounds = [(-1, -1)] + chain + [(len(lp), len(lx))]
            for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
                gp = [c for c in lp[i0 + 1:i1] if c not in p2x]
                gx = [c for c in lx[j0 + 1:j1] if c not in x2p]
                if gp and len(gp) == len(gx):
                    w = 2.0 if len(gp) == 1 else 1.0 / len(gp)
                    for a, b in zip(gp, gx): votes[(a, b)] += w
        best_p = {}; best_x = {}
        for (a, b), v in votes.items():
            if v > best_p.get(a, (0, None))[0]: best_p[a] = (v, b)
            if v > best_x.get(b, (0, None))[0]: best_x[b] = (v, a)
        new = 0
        for a, (v, b) in best_p.items():
            if v < 2.0 or best_x.get(b, (0, None))[1] != a: continue
            r = xs.get(b, 0) / max(psize.get(a, 1), 1)
            if not 0.2 <= r <= 5: continue
            p2x[a] = b; x2p[b] = a; new += 1
        if not new: break
    # evaluate hidden
    found = {p: p2x[p] for p in hidden if p in p2x}
    correct = sum(1 for p in found if found[p] == hidden[p])
    if verbose:
        print("%s: seeds %d (hidden %d), rounds %d, matched %d (new %d)" % (target, len(S), len(hidden), rounds, len(p2x), len(p2x) - (len(S) - len(hidden))))
        if hidden: print("   hidden seeds re-found: %d, correct %d (precision %.3f, recall %.3f)" % (len(found), correct, correct / max(len(found), 1), correct / len(hidden)))
    return p2x, pf, S

if __name__ == "__main__":
    target = sys.argv[1]; xbe = sys.argv[2]
    for h in (0.25,):
        for s in (1, 2, 3): run(target, xbe, h, s)
