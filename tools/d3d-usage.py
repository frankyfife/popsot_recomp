"""List the XDK Direct3D functions the lifted game code calls, by name.

    python tools/d3d-usage.py research/ghidra/retail_functions.tsv [xbox-retail]

The TSV is a Ghidra function export (address, size, block, source, name) of
the target's XBE, imported with ghidra-xbe so that XbSymbolDatabase names
the statically linked XDK functions. Calls are taken from the generated
code (RECOMP_ABI_CALL), callers inside the XDK sections are left out, and
the output is one line per called function: section, address, name, call
sites, calling functions. It reads no game data beyond the export.
"""
import collections
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CALL = re.compile(r"RECOMP_ABI_CALL\(0x([0-9A-F]{8})u")
FUNC = re.compile(r"^void sub_([0-9A-F]{8})\(void\)", re.M)
LIBRARIES = ("D3D", "D3DX", "XGRPH")


def main():
    export = Path(sys.argv[1])
    target = sys.argv[2] if len(sys.argv) > 2 else "xbox-retail"
    gen = ROOT / "source" / target / "gen"
    names, block = {}, {}
    for row in csv.DictReader(export.open(encoding="utf-8"), delimiter="\t"):
        a = int(row["address"], 16)
        names[a], block[a] = row["name"], row["block"]
    ranges = {}
    for a, b in block.items():
        lo, hi = ranges.get(b, (a, a))
        ranges[b] = (min(lo, a), max(hi, a))

    def library(a):
        for b in LIBRARIES:
            if b in ranges and ranges[b][0] <= a <= ranges[b][1]:
                return b
        return None

    sites, callers = collections.Counter(), collections.defaultdict(set)
    for path in gen.rglob("*.c"):
        if "xdk" in path.relative_to(gen).parts:
            continue
        text = path.read_text(errors="replace")
        starts = [(m.start(), int(m.group(1), 16)) for m in FUNC.finditer(text)]
        for m in CALL.finditer(text):
            t = int(m.group(1), 16)
            if not library(t):
                continue
            owner = max((s for s in starts if s[0] < m.start()), default=(0, 0))[1]
            if library(owner):
                continue
            sites[t] += 1
            callers[t].add(owner)
    print("section\taddress\tname\tcall_sites\tcalling_functions")
    for t, n in sites.most_common():
        print("%s\t0x%08X\t%s\t%d\t%d" % (library(t), t, names.get(t, "?"), n, len(callers[t])))
    named = sum(1 for t in sites if not names.get(t, "FUN_").startswith(("FUN_", "thunk_")))
    print("# %d library functions called from game code, %d named, %d call sites"
          % (len(sites), named, sum(sites.values())), file=sys.stderr)


if __name__ == "__main__":
    main()
