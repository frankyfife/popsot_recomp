"""Write the PC version's icon as an .ico file, for the launcher and the game.

    python tools/extract-icon.py "F:/GOG Games/Prince of Persia - The Sands of Time/POP.EXE" bin/pop.ico

The icon is the PC game's own artwork, so it is not kept in the repository:
this reads it out of the player's POP.EXE (its first RT_GROUP_ICON and the
RT_ICON images it names) and writes a standard .ico. The build uses the file
when it exists (CMakeLists.txt) and the plain built-in icon otherwise.
"""
import struct
import sys
from pathlib import Path

RT_ICON, RT_GROUP_ICON = 3, 14


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE file")
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva = struct.unpack_from("<I", data, dd + 2 * 8)[0]
    secs = []
    for i in range(nsec):
        o = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((va, max(vsize, rawsize), raw))
    return rsrc_rva, secs


def rva_to_off(secs, rva):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return raw + rva - va
    raise SystemExit("RVA %08X outside the sections" % rva)


def entries(data, base, off):
    named, ids = struct.unpack_from("<HH", data, off + 12)
    for i in range(named + ids):
        name, target = struct.unpack_from("<II", data, off + 16 + 8 * i)
        yield name, target


def resources(data):
    rsrc_rva, secs = sections(data)
    base = rva_to_off(secs, rsrc_rva)
    out = {}
    for tname, ttarget in entries(data, base, base):
        if not ttarget & 0x80000000:
            continue
        for nname, ntarget in entries(data, base, base + (ttarget & 0x7FFFFFFF)):
            leaf = ntarget
            while leaf & 0x80000000:                      # language level
                leaf = next(entries(data, base, base + (leaf & 0x7FFFFFFF)))[1]
            rva, size = struct.unpack_from("<II", data, base + leaf)
            o = rva_to_off(secs, rva)
            out.setdefault(tname, {})[nname] = data[o:o + size]
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    data = Path(sys.argv[1]).read_bytes()
    res = resources(data)
    groups = res.get(RT_GROUP_ICON)
    icons = res.get(RT_ICON, {})
    if not groups:
        raise SystemExit("no icon group in " + sys.argv[1])
    group = groups[sorted(groups)[0]]
    _, _, count = struct.unpack_from("<HHH", group, 0)
    images = []
    for i in range(count):
        w, h, colors, _, planes, bits, size, rid = struct.unpack_from("<BBBBHHIH", group, 6 + 14 * i)
        img = icons.get(rid)
        if img:
            images.append((w, h, colors, planes, bits, img))
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for w, h, colors, planes, bits, img in images:
        out += struct.pack("<BBBBHHII", w, h, colors, 0, planes, bits, len(img), offset)
        offset += len(img)
    for *_, img in images:
        out += img
    Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
    Path(sys.argv[2]).write_bytes(bytes(out))
    print("%s: %d images (%s)" % (sys.argv[2], len(images),
                                  ", ".join("%dx%d %d bit" % (w or 256, h or 256, b) for w, h, _, _, b, _ in images)))


if __name__ == "__main__":
    main()
