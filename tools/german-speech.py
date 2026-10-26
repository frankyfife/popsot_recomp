"""Give xbox-retail the German speech of the user's European disc.

    python tools/german-speech.py --eu-iso "<European disc>.iso" --game Assets/xbox-retail
    python tools/german-speech.py --game Assets/xbox-retail --restore
    python tools/german-speech.py --eu-iso "<European disc>.iso" --game Assets/xbox-retail --films
    python tools/german-speech.py --game Assets/xbox-retail --remove-films

The USA executable plays the English speech set: the 274 banks
sound/english/*.sb2 in SOUND/SOUNDX.big and the stream
sound/english/stream.ls2 in SOUND/soundx0.big. The European disc (En, Fr,
De, Es, It) has the same set in German, under sound/german/ in
SOUND/SOUNDX4.big, with the same names and the same bank format version.
This rewrites the two USA archives with the German banks and stream under
the English names, as one set (banks and stream belong together). Menus,
text and everything else stay NTSC. The English archives are moved to
SOUND/original-english/ first; --restore moves them back.

--films copies the German bonus films (CINE_9014, 9034, 9044, 9054, 9064:
the extras with German subtitles) from the European disc into VIDEO/. The
USA disc has only the English, French and Spanish ones (CINE_90n0..90n2);
the retail hook asks for 90n4 when POPSOT_MOVIE_LANGUAGE is German and the
files are there (source/xbox-retail/target_hooks.c). The cinematics need
nothing: their German sound track is already in the USA files.

Each .fat is a run of entries: u32 offset of the entry in the .fat, u32
offset in the .big, u32 size, u32 0, u32 name length including its NUL,
then the name. Data lies back to back in the .big.
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "port/third_party/xboxrecomp/tools/xiso"))
import xdvdfs  # noqa: E402

ENGLISH = "sound/english/"
GERMAN = "sound/german/"
ARCHIVES = ("SOUNDX", "soundx0")
BACKUP = "original-english"
# default.xbe of the USA disc, the only executable this applies to.
USA_XBE_SHA256 = "753C58B191279BF4D1A8B28A936DF36AA222ED566487AB1FBDFCD090E91EE379"


def parse_fat(data):
    entries, i = [], 0
    while i + 20 <= len(data):
        at, offset, size, zero, length = struct.unpack_from("<5I", data, i)
        if at != i or zero:
            raise SystemExit("unexpected .fat entry at 0x%X" % i)
        name = data[i + 20:i + 20 + length].rstrip(b"\0").decode("latin1")
        entries.append((name, offset, size))
        i += 20 + length
    if i != len(data):
        raise SystemExit(".fat has %d trailing bytes" % (len(data) - i))
    return entries


def write_archive(folder, name, files):
    """files: (archive name, bytes or (path, offset, size)) in order."""
    fat, offset = bytearray(), 0
    with open(folder / (name + ".big.tmp"), "wb") as big:
        for path, data in files:
            if isinstance(data, tuple):
                src, at, size = data
                with open(src, "rb") as f:
                    f.seek(at)
                    data = f.read(size)
            raw = path.encode("latin1") + b"\0"
            fat += struct.pack("<5I", len(fat), offset, len(data), 0, len(raw)) + raw
            big.write(data)
            offset += len(data)
    (folder / (name + ".fat.tmp")).write_bytes(fat)
    os.replace(folder / (name + ".big.tmp"), folder / (name + ".big"))
    os.replace(folder / (name + ".fat.tmp"), folder / (name + ".fat"))


def german_set(iso):
    with xdvdfs.Xiso(str(iso)) as img:
        try:
            fat = img.read(img.find("SOUNDX4.fat", "SOUND"))
            big = img.read(img.find("SOUNDX4.big", "SOUND"))
        except Exception:
            raise SystemExit("%s has no SOUND/SOUNDX4 (the German speech of the European disc)" % iso)
    files = {}
    for name, offset, size in parse_fat(fat):
        if not name.startswith(GERMAN):
            raise SystemExit("SOUNDX4 holds %s, not only German speech" % name)
        files[name[len(GERMAN):]] = big[offset:offset + size]
    return files


def install(game, iso):
    sound = game / "SOUND"
    xbe = game / "default.xbe"
    if xbe.exists() and hashlib.sha256(xbe.read_bytes()).hexdigest().upper() != USA_XBE_SHA256:
        raise SystemExit("%s is not the USA retail XBE" % xbe)
    backup = sound / BACKUP
    if backup.exists():
        raise SystemExit("German speech is already installed (%s exists); use --restore first" % backup)
    german = german_set(iso)
    source = {}
    for name in ARCHIVES:
        source[name] = parse_fat((sound / (name + ".fat")).read_bytes())
    english = [n for n, _, _ in source["SOUNDX"] if n.startswith(ENGLISH)]
    stream = [n for n, _, _ in source["soundx0"]]
    if stream != [ENGLISH + "stream.ls2"]:
        raise SystemExit("soundx0 holds %s, expected %sstream.ls2" % (stream, ENGLISH))
    missing = [n for n in english + stream if n[len(ENGLISH):] not in german]
    if missing:
        raise SystemExit("no German file for %d English ones, e.g. %s" % (len(missing), missing[:3]))

    backup.mkdir()
    for name in ARCHIVES:
        for ext in (".fat", ".big"):
            shutil.move(str(sound / (name + ext)), str(backup / (name + ext)))
    for name in ARCHIVES:
        files = []
        for path, offset, size in source[name]:
            if path.startswith(ENGLISH):
                files.append((path, german[path[len(ENGLISH):]]))
            else:
                files.append((path, (backup / (name + ".big"), offset, size)))
        write_archive(sound, name, files)
    print("German speech installed: %d banks and the stream under %s; English kept in %s"
          % (len(english), ENGLISH, backup))


def restore(game):
    sound = game / "SOUND"
    backup = sound / BACKUP
    if not backup.exists():
        raise SystemExit("nothing to restore: %s does not exist" % backup)
    for name in ARCHIVES:
        for ext in (".fat", ".big"):
            os.replace(backup / (name + ext), sound / (name + ext))
    backup.rmdir()
    print("English speech restored")


FILMS = ["CINE_90%d4.INT" % n for n in (1, 3, 4, 5, 6)]


def install_films(game, iso):
    video = game / "VIDEO"
    if not video.is_dir():
        raise SystemExit("%s has no VIDEO folder" % game)
    with xdvdfs.Xiso(str(iso)) as img:
        found = {e.name.upper(): e for d, e in img.walk() if d.upper() == "VIDEO/30FPS"}
        for name in FILMS:
            if name not in found:
                raise SystemExit("%s has no VIDEO/30fps/%s" % (iso, name))
            target = video / name
            if target.exists() and target.stat().st_size == found[name].size:
                continue
            tmp = video / (name + ".tmp")
            tmp.write_bytes(img.read(found[name]))
            os.replace(tmp, target)
    print("German bonus films installed: %s" % ", ".join(FILMS))


def remove_films(game):
    for name in FILMS:
        path = game / "VIDEO" / name
        if path.exists():
            path.unlink()
    print("German bonus films removed")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--game", required=True, type=Path, help="xbox-retail game folder (with SOUND/)")
    p.add_argument("--eu-iso", type=Path, help="image of the user's European disc")
    p.add_argument("--restore", action="store_true", help="put the English speech back")
    p.add_argument("--films", action="store_true", help="copy the German bonus films only")
    p.add_argument("--remove-films", action="store_true", help="remove the German bonus films")
    a = p.parse_args()
    if a.remove_films:
        remove_films(a.game)
    elif a.films:
        if not a.eu_iso:
            p.error("--films needs --eu-iso")
        install_films(a.game, a.eu_iso)
    elif a.restore:
        restore(a.game)
    elif a.eu_iso:
        install(a.game, a.eu_iso)
    else:
        p.error("--eu-iso or --restore is required")


if __name__ == "__main__":
    main()
