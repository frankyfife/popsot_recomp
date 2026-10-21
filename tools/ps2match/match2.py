"""Neighbourhood matching: PS2 prototype names -> Xbox build."""
import pickle, random, collections, importlib.util, sys
from pathlib import Path
ROOT = Path(r"D:\Projekte\rePOP_ff")
spec = importlib.util.spec_from_file_location("m1", ROOT / "tools/ps2match/match.py"); m1 = importlib.util.module_from_spec(spec); spec.loader.exec_module(m1)

def prepare(target):
    pf, pc, xs, xc, xf = pickle.load(open(ROOT / ("research/match/graphs-%s.pkl" % target), "rb"))
    portable = {a for a, (n, s, lib) in pf.items() if not lib.startswith(m1.PLATFORM_LIBS)}
    P = {a: [c for c in pc[a] if c in portable] for a in portable}
    X = {a: [c for c in v if c < m1.GAME_END and c in xc] for a, v in xc.items() if a < m1.GAME_END}
    def rev(G):
        R = collections.defaultdict(list)
        for a, cs in G.items():
            for c in cs: R[c].append(a)
        return R
    return pf, xs, P, X, rev(P), rev(X)

def run(target, xbe, holdout=0.0, seed=1, min_common=2, min_score=0.5, verbose=True):
    pf, xs, P, X, Pr, Xr = prepare(target)
    S = {p: x for p, x in m1.seeds(xbe).items() if p in P and x in X}
    items = sorted(S.items()); random.Random(seed).shuffle(items)
    cut = int(len(items) * holdout); hidden = dict(items[:cut])
    p2x = dict(items[cut:]); x2p = {x: p for p, x in p2x.items()}
    def nb_p(p): return set(P.get(p, [])) | set(Pr.get(p, []))
    def nb_x(x): return set(X.get(x, [])) | set(Xr.get(x, []))
    rounds = 0
    while True:
        rounds += 1
        cand = set()
        for p, x in p2x.items():
            up = [c for c in nb_p(p) if c not in p2x]; ux = [c for c in nb_x(x) if c not in x2p]
            if len(up) * len(ux) > 4000: continue
            for a in up:
                for b in ux: cand.add((a, b))
        scored = []
        for a, b in cand:
            ma = {p2x[c] for c in nb_p(a) if c in p2x}; mb = {c for c in nb_x(b) if c in x2p}
            common = len(ma & mb)
            if common < min_common: continue
            score = common / len(ma | mb)
            r = xs.get(b, 0) / max(pf[a][1], 1)
            if score >= min_score and 0.15 <= r <= 6: scored.append((score, common, a, b))
        best_a, best_b = {}, {}
        for s, c, a, b in scored:
            if (s, c) > best_a.get(a, (0, 0, None))[:2]: best_a[a] = (s, c, b)
            if (s, c) > best_b.get(b, (0, 0, None))[:2]: best_b[b] = (s, c, a)
        # require a clear winner on both sides
        second_a = collections.defaultdict(float); second_b = collections.defaultdict(float)
        for s, c, a, b in scored:
            if best_a[a][2] != b: second_a[a] = max(second_a[a], s)
            if best_b[b][2] != a: second_b[b] = max(second_b[b], s)
        new = 0
        for a, (s, c, b) in best_a.items():
            if best_b[b][2] != a or s - second_a[a] < 0.15 or s - second_b[b] < 0.15: continue
            p2x[a] = b; x2p[b] = a; new += 1
        if not new or rounds > 60: break
    found = {p: p2x[p] for p in hidden if p in p2x}
    correct = sum(1 for p in found if found[p] == hidden[p])
    if verbose:
        print("%s mc=%d ms=%.2f: seeds %d hidden %d rounds %d matched %d new %d | hidden found %d correct %d (prec %.3f rec %.3f)" % (
            target, min_common, min_score, len(S), len(hidden), rounds, len(p2x), len(p2x) - (len(S) - len(hidden)),
            len(found), correct, correct / max(len(found), 1), correct / max(len(hidden), 1)))
    return p2x, pf, S

if __name__ == "__main__":
    target, xbe = sys.argv[1], sys.argv[2]
    for mc, ms in ((2, 0.5), (3, 0.5), (2, 0.6)):
        run(target, xbe, 0.25, 1, mc, ms)
