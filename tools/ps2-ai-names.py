"""Name the compiled Jade AI functions of an Xbox XBE from the PS2 prototype.

    python tools/ps2-ai-names.py <MAPFILE.MAP> <SLUS_207.43> Assets/xbox-retail/default.xbe > source/xbox-retail/ai2c_names.tsv

The PS2 prototype of Sands of Time (SLUS_207.43, 2003-08-19) ships a
CodeWarrior link map. Its AI2C_gat_fctdefs table pairs a Jade key with each
compiled AI function. The Xbox builds hold the same kind of table, with the
same keys for the same functions. This finds that table in the XBE by the
first two PS2 keys and prints, for every key both builds have, the Xbox
address and the PS2 name. Keys only one build has are counted on stderr.
"""
import re
import struct
import sys

SECTION_LINE = re.compile(r"^\s+([0-9A-F]{8}) ([0-9A-F]{8}) (\S+)\s+(\S+)\s+\((.*)\)")


def map_symbols(path):
    text, data = {}, {}
    for line in open(path, encoding="latin1"):
        m = SECTION_LINE.match(line)
        if not m:
            continue
        address, size, section, name = int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)
        if section == ".text":
            text.setdefault(address, name)
        else:
            data[name] = (address, size)
    return text, data


def elf_reader(path):
    elf = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phnum, = struct.unpack_from("<H", elf, 0x2C)
    segments = [struct.unpack_from("<8I", elf, phoff + i * 32) for i in range(phnum)]

    def read(va, size):
        for _, offset, vaddr, _, filesz, _, _, _ in segments:
            if vaddr <= va < vaddr + filesz:
                return elf[offset + va - vaddr:offset + va - vaddr + size]
        raise SystemExit("0x%08X is not in the ELF" % va)
    return read


def xbe_table(path, first, second):
    xbe = open(path, "rb").read()
    base = struct.unpack_from("<I", xbe, 0x104)[0]
    count, headers = struct.unpack_from("<II", xbe, 0x11C)
    needle = struct.pack("<II", first, 0)[:4]
    for i in range(count):
        _, va, _, raw, rsize, _ = struct.unpack_from("<6I", xbe, headers - base + i * 56)
        blob = xbe[raw:raw + rsize]
        at = blob.find(needle)
        while at >= 0:
            if blob[at + 8:at + 12] == struct.pack("<I", second):
                pairs = []
                while at + 8 * len(pairs) + 8 <= len(blob):
                    key, fn = struct.unpack_from("<II", blob, at + 8 * len(pairs))
                    if not key and not fn:
                        break
                    pairs.append((key, fn))
                return va + at, pairs
            at = blob.find(needle, at + 1)
    raise SystemExit("no AI2C function table found in %s" % path)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    text, data = map_symbols(sys.argv[1])
    address, size = data["AI2C_gat_fctdefs"]
    words = struct.unpack("<%dI" % (size // 4), elf_reader(sys.argv[2])(address, size))
    ps2 = dict(zip(words[0::2], words[1::2]))
    order = list(words[0::2])
    at, pairs = xbe_table(sys.argv[3], order[0], order[1])
    xbox = dict(pairs)
    common = [k for k in xbox if k in ps2 and ps2[k] in text and k != 0xFFFFFFFF]  # 0xFFFFFFFF: placeholder
    print("# Jade AI2C functions named from the PS2 prototype map; Xbox table at 0x%08X" % at)
    print("address\tkey\tname")
    for k in sorted(common, key=lambda k: xbox[k]):
        print("0x%08X\t0x%08X\t%s" % (xbox[k], k, text[ps2[k]]))
    print("# %d named; %d keys only in the XBE, %d only in the prototype"
          % (len(common), len(set(xbox) - set(ps2)), len(set(ps2) - set(xbox))), file=sys.stderr)


if __name__ == "__main__":
    main()
