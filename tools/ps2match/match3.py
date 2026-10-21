"""Neighbours + shared globals: PS2 prototype names -> Xbox build."""
import pickle, random, collections, importlib.util, math, sys
from pathlib import Path
ROOT = Path(r"D:\Projekte\rePOP_ff")
def load(n, f):
    s = importlib.util.spec_from_file_location(n, ROOT / f); m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
m1 = load("m1", "tools/ps2match/match.py"); m2 = load("m2", "tools/ps2match/match2.py")

def run(target, xbe, holdout=0.0, seed=1, verbose=True, max_rounds=80):
    pf, xs, P, X, Pr, Xr = m2.prepare(target)
    pg, xg = pickle.load(open(ROOT / ("research/match/globals-%s.pkl" % target), "rb"))
    S = {p: x for p, x in m1.seeds(xbe).items() if p in P and x in X}
    items = sorted(S.items()); random.Random(seed).shuffle(items)
    cut = int(len(items) * holdout); hidden = dict(items[:cut])
    p2x = dict(items[cut:]); x2p = {x: p for p, x in p2x.items()}
    nbp = lambda p: set(P.get(p, [])) | set(Pr.get(p, []))
    nbx = lambda x: set(X.get(x, [])) | set(Xr.get(x, []))
    xusers = collections.defaultdict(set)
    for x, gs in xg.items():
        if x in X:
            for g in gs: xusers[g].add(x)
    pdf = collections.Counter(g for gs in pg.values() for g in gs)
    rounds = 0; gmap = {}
    while rounds < max_rounds:
        rounds += 1
        # learn globals from matched pairs
        votes = collections.Counter()
        for p, x in p2x.items():
            gp, gx = pg.get(p, set()), xg.get(x, set())
            if gp and gx and len(gp) * len(gx) <= 600:
                for a in gp:
                    for b in gx: votes[(a, b)] += 1.0 / max(len(gp), len(gx))
        ba, bb = {}, {}
        for (a, b), v in votes.items():
            if v > ba.get(a, (0, None))[0]: ba[a] = (v, b)
            if v > bb.get(b, (0, None))[0]: bb[b] = (v, a)
        gmap = {a: b for a, (v, b) in ba.items() if v >= 1.5 and bb[b][1] == a}
        # candidates
        cand = set()
        for p, x in p2x.items():
            up = [c for c in nbp(p) if c not in p2x]; ux = [c for c in nbx(x) if c not in x2p]
            if len(up) * len(ux) <= 4000:
                cand.update((a, b) for a in up for b in ux)
        for p in pf:
            if p in p2x or p not in P: continue
            mg = [gmap[g] for g in pg.get(p, ()) if g in gmap]
            if not mg: continue
            pool = set()
            for g in mg:
                if len(xusers[g]) <= 12: pool |= {x for x in xusers[g] if x not in x2p}
            cand.update((p, x) for x in pool)
        scored = []
        for a, b in cand:
            ma = {p2x[c] for c in nbp(a) if c in p2x}; mb = {c for c in nbx(b) if c in x2p}
            ga = {gmap[g] for g in pg.get(a, ()) if g in gmap}; gb = xg.get(b, set()) & set(gmap.values())
            w = lambda g: 1.0 / math.log(2 + len(xusers[g]))
            num = len(ma & mb) + sum(w(g) for g in ga & gb)
            den = len(ma | mb) + sum(w(g) for g in ga | gb)
            evidence = len(ma & mb) + len(ga & gb)
            if evidence < 2 or den == 0: continue
            score = num / den
            r = xs.get(b, 0) / max(pf[a][1], 1)
            if score >= 0.5 and 0.15 <= r <= 6: scored.append((score, evidence, a, b))
        best_a, best_b = {}, {}
        for s, e, a, b in scored:
            if (s, e) > best_a.get(a, (0, 0, None))[:2]: best_a[a] = (s, e, b)
            if (s, e) > best_b.get(b, (0, 0, None))[:2]: best_b[b] = (s, e, a)
        sec_a = collections.defaultdict(float); sec_b = collections.defaultdict(float)
        for s, e, a, b in scored:
            if best_a[a][2] != b: sec_a[a] = max(sec_a[a], s)
            if best_b[b][2] != a: sec_b[b] = max(sec_b[b], s)
        new = 0
        for a, (s, e, b) in best_a.items():
            if best_b[b][2] != a or s - sec_a[a] < 0.15 or s - sec_b[b] < 0.15: continue
            p2x[a] = b; x2p[b] = a; new += 1
        if not new: break
    found = {p: p2x[p] for p in hidden if p in p2x}; correct = sum(1 for p in found if found[p] == hidden[p])
    if verbose:
        libs = collections.Counter(pf[p][2].split("_")[0] for p in p2x if p not in S)
        print("%s: seeds %d hidden %d rounds %d matched %d globals %d | hidden found %d correct %d (prec %.3f rec %.3f) | new by lib %s" % (
            target, len(S), len(hidden), rounds, len(p2x), len(gmap), len(found), correct, correct / max(len(found), 1),
            correct / max(len(hidden), 1), libs.most_common(6)))
    return p2x, pf, S, gmap

CHECKS = {0x002FD810: "CAM_Engine_ComputePlans", 0x00051C00: "water step", 0x00051800: "water impulses", 0x00051A70: "water normals",
          0x00053190: "water heights->VB", 0x002F9EC0: "GDI table fill", 0x000452F0: "cosine", 0x00045340: "sine", 0x000F84A0: "text ref",
          0x000DC700: "large motor", 0x000DC6A0: "small motor", 0x00017D10: "menu tick", 0x0001F7E0: "world bookkeeping", 0x000DB740: "frame clock",
          0x000CFC10: "projection", 0x000CFB90: "GDI Clear", 0x000CFBF0: "GDI Flip", 0x000D0090: "GDI DrawIndexedTriangles", 0x0001E970: "Bink status"}
if __name__ == "__main__":
    target, xbe = sys.argv[1], sys.argv[2]
    run(target, xbe, 0.25, 1)
    p2x, pf, S, g = run(target, xbe, 0.0, 1)
    x2n = {x: pf[p][0] for p, x in p2x.items()}
    for a, what in CHECKS.items(): print("  %08X %-26s -> %s" % (a, what, x2n.get(a, "-")))
    pickle.dump((p2x, S), open(ROOT / ("research/match/result-%s.pkl" % target), "wb"))
