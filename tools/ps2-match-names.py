"""Name Xbox functions from the PS2 prototype map through the call graphs.

    python tools/ps2-match-names.py            # writes source/xbox-retail/ps2_names.tsv

Inputs, in ignored research/: ps2-proto/MAPFILE.MAP and ps2-proto/SLUS_207.43
(the user's PS2 prototype of 2003-08-19) and Assets/xbox-retail/default.xbe.

1. tools/ps2match/graphs.py: the PS2 call graph from the map's function
   addresses and the ELF's jal instructions; the Xbox call graph from the
   generated code (RECOMP_ABI_CALL).
2. tools/ps2match/globals.py: the named globals each PS2 function touches
   (lui/addiu pairs, gp-relative), and the .rdata/.data addresses each Xbox
   function touches.
3. Seeds: the compiled AI functions, which share Jade keys (ps2-ai-names.py).
4. tools/ps2match/match3.py: pairs are added while both sides agree on
   their already-matched neighbours (callers and callees) and on mapped
   globals: a mutual best score of at least 0.5 with a 0.15 margin, at
   least two pieces of evidence, and a plausible code-size ratio. Holding
   out a quarter of the seeds, 108 of the 112 seeds it found again were
   right (96.4 %), 2026-10-21. Names it adds are a strong hint, not proof.
"""
import importlib.util
import pickle
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/ps2match" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    for step in ("graphs", "globals"):
        subprocess.run([sys.executable, str(ROOT / "tools/ps2match" / (step + ".py"))], check=True)
    m3 = load("match3")
    p2x, pf, seeds, _ = m3.run("xbox-retail", ROOT / "Assets/xbox-retail/default.xbe", 0.0, 1, verbose=True)
    out = ROOT / "source/xbox-retail/ps2_names.tsv"
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# Xbox retail functions named from the PS2 prototype (tools/ps2-match-names.py)\n")
        f.write("address\tname\tps2_library\thow\n")
        for p, x in sorted(p2x.items(), key=lambda kv: kv[1]):
            f.write("0x%08X\t%s\t%s\t%s\n" % (x, pf[p][0], pf[p][2], "key" if p in seeds else "matched"))
    print("wrote", out)


if __name__ == "__main__":
    main()
