"""Build reSOT's high-resolution menu font and button atlases (texture pack).

    python tools/hd-ui.py --game Assets/xbox-retail --pack bin/textures

The retail menus draw text from one 512x128 font atlas and button prompts from
one 512x256 atlas (VALIDATION.md, 2026-10-22). This writes both again at
SCALE times the size, as texture pack files <key>.tga (resot_texpack.h):

- Font: every glyph is rendered from the game's own vector font,
  MEDIA/AtlantisMF.ttf on the disc, into the rectangle the game samples
  (tools/hd-ui/font_rects.json, measured with RESOT_FONT_PROBE and
  RECOMP_TRACE_ALLVTX), centred on the original glyph's ink.
- Buttons: the icons come from tools/hd-ui/buttons.svg (drawn for reSOT); the
  controller line drawing is traced from the user's own atlas into SVG paths
  (iso-contours of the cubic-interpolated alpha), so nothing of the game is
  stored in the repository.

- HUD, optional (--hud-pack): the health bar, its frame and end piece, the
  sand tanks and the ring come from the PC version's PoP Texture Studio pack
  (Evgesha.JK beside the GOG game), which redrew them at 8x to 16x. Its
  records are keyed by Jade texture key (the HUD is group 0B007Bxx), ours by
  picture, so each HUD texture of the user's level dump (RESOT_TEXTURE_DUMP=1
  in a fight, e.g. with RESOT_START_LEVEL=6101_couloir) is paired with the
  record whose smallest stored level, scaled to the original size, differs
  least, and only when the next best differs at least twice as much.

Inputs are the two original atlases as written by RESOT_TEXTURE_DUMP=1
(<pack>/dump/<key>_512x128_fmt06.tga and <key>_512x256_fmt06.tga). Both are
stored upside down, as the game samples them; the files written here keep
that orientation. Needs Pillow, NumPy, SciPy and ImageMagick (librsvg) for the
SVG.
"""
import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
HERE = ROOT / "tools" / "hd-ui"
FONT_KEY = "61ae24597fbff11f"      # retail 512x128 menu font atlas
BUTTON_KEY = "961c13f4b5867590"    # retail 512x256 button atlas
FONT_SIZE = 19                     # AtlantisMF size of the original glyphs
FONT_STROKE = 0.0                  # extra stroke, original pixels. 0.5 fits the atlas best
                                   # (mean correlation 0.89), but looks too bold at 4K
                                   # (user, 2026-10-25); the plain TTF weight is used.
FALLBACK_BELOW = 0.6               # glyphs whose TTF shape differs keep the traced original
EDGE_GAP = 2                       # HD texels around a glyph's rectangle no other glyph may ink
BUTTON_REACH = 1                   # original texels the HD buttons may reach past the original ink
DIAGRAM = (237, 0, 512, 256)       # controller drawing, upright atlas pixels
HUD_GROUP = 0x0B007B00             # Jade keys of the HUD textures (0B007B00-0B007BFF)
HUD_MAX_ERROR = 0.07               # mean premultiplied difference at the original size
HUD_MIN_RATIO = 2.0                # next best match / best match


def read_tga(path):
    return Image.open(path).convert("RGBA")


def write_tga(img, path):
    """32-bit BGRA, top-left origin, as resot_texpack.h reads it."""
    img = img.convert("RGBA")
    w, h = img.size
    hdr = bytearray(18)
    hdr[2] = 2
    hdr[12:14] = w.to_bytes(2, "little")
    hdr[14:16] = h.to_bytes(2, "little")
    hdr[16] = 32
    hdr[17] = 0x28
    r, g, b, a = img.split()
    data = Image.merge("RGBA", (b, g, r, a)).tobytes()
    Path(path).write_bytes(bytes(hdr) + data)


# --------------------------------------------------------------------- font
def traced_upscale(alpha, scale):
    """An alpha cell at scale x: cubic field, sharpened around its 0.5
    iso-contour, as the vector tracing renders it."""
    field = ndimage.zoom(alpha, scale, order=3)
    edge = np.clip((field - 0.5) * 2.5 + 0.5, 0, 1)
    return (edge * 255 + 0.5).astype(np.uint8)


def build_font(atlas, ttf, rects, scale, size=None, stroke=None):
    """atlas: original 512x128 (raw, glyphs upside down)."""
    W, H = atlas.size
    A = np.asarray(atlas.split()[3]).astype(np.float32) / 255.0
    out_a = np.zeros((H * scale, W * scale), np.uint8)
    size_px = int(round((size or FONT_SIZE) * scale))
    stroke = FONT_STROKE * scale if stroke is None else stroke
    font = ImageFont.truetype(str(ttf), size_px)
    missing = []
    fallback = []
    areas = glyph_areas(rects, out_a.shape, scale)
    for code, (u0, u1, v0, v1) in sorted(rects.items(), key=lambda kv: int(kv[0])):
        c = int(code)
        # the game's text bytes are Windows-1252 (0x80..0x9F: Euro sign, S/Z
        # with caron, OE ligature, ellipsis, trade mark ...)
        ch = bytes([c]).decode("cp1252") if c < 256 and c not in (0x81, 0x8D, 0x8F, 0x90, 0x9D) else chr(c)
        x0, x1 = int(round(u0 - 0.5)), int(round(u1))
        y0, y1 = int(round(v0 - 0.5)), int(round(v1))
        cell = A[y0:y1, x0:x1]
        ys, xs = np.nonzero(cell > 0.25)
        if not len(xs):
            continue
        # original ink centre in raw coordinates (upside down)
        cx = x0 + (xs.min() + xs.max() + 1) / 2.0
        cy = y0 + (ys.min() + ys.max() + 1) / 2.0
        size = size_px
        g = Image.new("L", (size * 3, size * 3), 0)
        ImageDraw.Draw(g).text((size, size), ch, font=font, fill=255,
                               stroke_width=int(round(stroke)), stroke_fill=255)
        bbox = g.getbbox()
        if not bbox:
            missing.append(ch)
            continue
        g = g.crop(bbox).transpose(Image.FLIP_TOP_BOTTOM)
        gx = int(round(cx * scale - g.width / 2.0))
        gy = int(round(cy * scale - g.height / 2.0))
        # only where this glyph may ink (glyph_areas), so no neighbour's
        # rectangle ever samples it
        ga = np.asarray(g)
        bx0, bx1, by0, by1, allow = areas[(u0, u1, v0, v1)]
        ax0, ay0 = max(gx, bx0), max(gy, by0)
        ax1, ay1 = min(gx + ga.shape[1], bx1), min(gy + ga.shape[0], by1)
        if ax1 > ax0 and ay1 > ay0:
            sub = ga[ay0 - gy:ay1 - gy, ax0 - gx:ax1 - gx] * allow[ay0 - by0:ay1 - by0, ax0 - bx0:ax1 - bx0]
            out_a[ay0:ay1, ax0:ax1] = np.maximum(out_a[ay0:ay1, ax0:ax1], sub.astype(np.uint8))
    # check each glyph against the original; where the TTF shape differs
    # (a different design or no glyph), trace the original instead
    small = np.asarray(Image.fromarray(out_a).resize((W, H), Image.BOX)).astype(np.float32) / 255.0
    for code, (u0, u1, v0, v1) in rects.items():
        x0, x1 = int(round(u0 - 0.5)), int(round(u1))
        y0, y1 = int(round(v0 - 0.5)), int(round(v1))
        a, b = A[y0:y1, x0:x1], small[y0:y1, x0:x1]
        s = float((a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9))
        if s < FALLBACK_BELOW:
            bx0, bx1, by0, by1, allow = areas[(u0, u1, v0, v1)]
            traced = np.zeros((by1 - by0, bx1 - bx0), np.uint8)
            t = traced_upscale(a, scale)
            tx0, ty0 = x0 * scale - bx0, y0 * scale - by0
            sx0, sy0 = max(tx0, 0), max(ty0, 0)
            sx1, sy1 = min(tx0 + t.shape[1], traced.shape[1]), min(ty0 + t.shape[0], traced.shape[0])
            traced[sy0:sy1, sx0:sx1] = t[sy0 - ty0:sy1 - ty0, sx0 - tx0:sx1 - tx0]
            region = out_a[by0:by1, bx0:bx1]
            region[allow] = traced[allow]
            fallback.append(code)
    out = Image.fromarray(out_a)
    white = Image.new("L", out.size, 255)
    return Image.merge("RGBA", (white, white, white, out)), missing + ["traced:" + ",".join(fallback)] if fallback else missing


def glyph_areas(rects, shape, scale):
    """Where each glyph may ink: its sampled rectangle grown by EDGE_GAP HD
    texels, except where another glyph's grown rectangle reaches; per
    rectangle (x0, x1, y0, y1, allowed). The game's rectangles of
    neighbours touch or overlap by half a texel (e.g. Z: u 246.5-262.0,
    [: 262.5-267.0), and the cells the glyphs used to be clipped to were
    rounded outwards: the bracket's stroke began at Z's right edge, and
    Z's bilinear sample there drew it as a thin line before the next letter
    at 4K (user report, 2026-10-25). Codes that share one rectangle, or
    nearly (170 / 186), are one area."""
    H, W = shape
    count = np.zeros(shape, np.int16)
    merged = []
    owner = {}
    for r in sorted(set(tuple(r) for r in rects.values())):
        u0, u1, v0, v1 = r
        for m in merged:
            iw = min(u1, m[1]) - max(u0, m[0])
            ih = min(v1, m[3]) - max(v0, m[2])
            small = min((u1 - u0) * (v1 - v0), (m[1] - m[0]) * (m[3] - m[2]))
            if iw > 0 and ih > 0 and iw * ih > 0.5 * small:
                m[:] = [min(u0, m[0]), max(u1, m[1]), min(v0, m[2]), max(v1, m[3])]
                owner[r] = m
                break
        else:
            merged.append([u0, u1, v0, v1])
            owner[r] = merged[-1]
    boxes = {}
    for m in merged:
        x0, x1 = max(int(np.floor(m[0] * scale - EDGE_GAP)), 0), min(int(np.ceil(m[1] * scale + EDGE_GAP)), W)
        y0, y1 = max(int(np.floor(m[2] * scale - EDGE_GAP)), 0), min(int(np.ceil(m[3] * scale + EDGE_GAP)), H)
        boxes[id(m)] = (x0, x1, y0, y1)
        count[y0:y1, x0:x1] += 1
    areas = {}
    for r, m in owner.items():
        x0, x1, y0, y1 = boxes[id(m)]
        areas[r] = (x0, x1, y0, y1, count[y0:y1, x0:x1] == 1)
    return areas


# ------------------------------------------------------------------ tracing
def marching_squares(field, level):
    """Closed iso-contours of a 2-D field as lists of (x, y) points."""
    f = field
    h, w = f.shape
    b = (f >= level).astype(np.uint8)
    case = b[:-1, :-1] * 8 + b[:-1, 1:] * 4 + b[1:, 1:] * 2 + b[1:, :-1]
    ys, xs = np.nonzero((case != 0) & (case != 15))

    def interp(p, q):
        (y0, x0), (y1, x1) = p, q
        a, c = f[y0, x0], f[y1, x1]
        t = 0.5 if a == c else (level - a) / (c - a)
        return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)

    segs = []
    for y, x in zip(ys, xs):
        c = case[y, x]
        tl, tr, br, bl = (y, x), (y, x + 1), (y + 1, x + 1), (y + 1, x)
        top, right, bottom, left = (tl, tr), (tr, br), (bl, br), (tl, bl)
        table = {1: [(left, bottom)], 2: [(bottom, right)], 3: [(left, right)], 4: [(top, right)],
                 5: [(left, top), (bottom, right)], 6: [(top, bottom)], 7: [(left, top)],
                 8: [(left, top)], 9: [(top, bottom)], 10: [(left, bottom), (top, right)],
                 11: [(top, right)], 12: [(left, right)], 13: [(bottom, right)], 14: [(left, bottom)]}
        for e0, e1 in table[c]:
            segs.append((interp(*e0), interp(*e1)))
    # link segments into rings
    key = lambda p: (round(p[0], 4), round(p[1], 4))
    ends = {}
    for i, (p, q) in enumerate(segs):
        ends.setdefault(key(p), []).append(i)
        ends.setdefault(key(q), []).append(i)
    used = [False] * len(segs)
    rings = []
    for i in range(len(segs)):
        if used[i]:
            continue
        used[i] = True
        ring = [segs[i][0], segs[i][1]]
        while True:
            k = key(ring[-1])
            nxt = None
            for j in ends.get(k, ()):
                if not used[j]:
                    nxt = j
                    break
            if nxt is None:
                break
            used[nxt] = True
            p, q = segs[nxt]
            ring.append(q if key(p) == k else p)
        rings.append(ring)
    return rings


def simplify(points, eps):
    """Ramer-Douglas-Peucker."""
    if len(points) < 3:
        return points
    pts = np.asarray(points)
    a, b = pts[0], pts[-1]
    ab = b - a
    n = np.hypot(*ab)
    rel = pts - a
    d = np.abs(ab[0] * rel[:, 1] - ab[1] * rel[:, 0]) / n if n else np.hypot(rel[:, 0], rel[:, 1])
    i = int(d.argmax())
    if d[i] > eps:
        return simplify(points[:i + 1], eps)[:-1] + simplify(points[i:], eps)
    return [points[0], points[-1]]


def trace_diagram(upright, box, zoom=4):
    x0, y0, x1, y1 = box
    crop = upright.crop(box)
    a = np.asarray(crop.split()[3]).astype(np.float32) / 255.0
    rgb = np.asarray(crop.convert("RGB")).reshape(-1, 3)[a.reshape(-1) > 0.8]
    colour = "#%02x%02x%02x" % tuple(int(v) for v in np.median(rgb, 0)) if len(rgb) else "#c8c0a0"
    field = ndimage.zoom(np.pad(a, 2), zoom, order=3)
    rings = marching_squares(field, 0.5)
    parts = []
    for ring in rings:
        if len(ring) < 4:
            continue
        pts = simplify(ring, 0.25)
        if len(pts) < 3:
            continue
        parts.append("M" + " L".join("%.3f,%.3f" % (x0 + p[0] / zoom - 2, y0 + p[1] / zoom - 2) for p in pts) + " Z")
    return '<path fill="%s" fill-rule="evenodd" d="%s"/>' % (colour, " ".join(parts)), len(parts)


def build_buttons(atlas, scale, magick):
    upright = atlas.transpose(Image.FLIP_TOP_BOTTOM)
    svg = (HERE / "buttons.svg").read_text(encoding="utf-8")
    diagram, n = trace_diagram(upright, DIAGRAM)
    svg = svg.replace("</svg>", "  <!-- traced from the user's atlas by tools/hd-ui.py -->\n  " + diagram + "\n</svg>")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "buttons.svg"
        png = Path(tmp) / "buttons.png"
        src.write_text(svg, encoding="utf-8")
        subprocess.run([magick, "-background", "none", "-density", str(96 * scale), str(src), "png32:" + str(png)],
                       check=True, stderr=subprocess.DEVNULL)
        img = Image.open(png).convert("RGBA")
    if img.size != (atlas.width * scale, atlas.height * scale):
        img = img.resize((atlas.width * scale, atlas.height * scale), Image.LANCZOS)
    # nothing more than BUTTON_REACH original texels from the original ink:
    # anti-aliasing or a shape drawn a little larger must not reach into the
    # gap the game's rectangles may sample (fragments at the edges at 4K)
    ink = np.asarray(upright.split()[3]) > 8
    ink = ndimage.binary_dilation(ink, iterations=BUTTON_REACH)
    near = np.repeat(np.repeat(ink, scale, axis=0), scale, axis=1)
    rgba = np.asarray(img).copy()
    rgba[..., 3] = (rgba[..., 3] * near).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    return img.transpose(Image.FLIP_TOP_BOTTOM), n


# ---------------------------------------------------------------------- HUD
def evgjk_records(f):
    """Texture records of a PoP Texture Studio pack (EVGJK1): Jade key, original
    and replacement size, payload kind (1 DXT1, 2 DXT5, 3 BGRA) with its mip
    levels at offset/size."""
    hdr = f.read(0x90)
    if hdr[:8] != b"EVGJK1\r\n":
        raise SystemExit("not a PoP Texture Studio pack (EVGJK1)")
    _, rec_off, rec_size, _, count, _ = struct.unpack_from("<6I", hdr, 8)
    f.seek(rec_off)
    out = []
    for _ in range(count):
        r = f.read(rec_size)
        key, ow, oh, _, _, rw, rh, _, kind = struct.unpack_from("<IHHBBHHBB", r, 0)
        off, size = struct.unpack_from("<QQ", r, 0x18)
        out.append(dict(key=key, ow=ow, oh=oh, rw=rw, rh=rh, kind=kind, off=off, size=size))
    return out


def evgjk_level_bytes(kind, w, h):
    return w * h * 4 if kind == 3 else max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if kind == 1 else 16)


def evgjk_level(f, r, smallest=False):
    """The first (or the smallest stored, at least 4x4) level as RGBA."""
    w, h, off, left, levels = r["rw"], r["rh"], r["off"], r["size"], []
    while left >= evgjk_level_bytes(r["kind"], w, h):
        n = evgjk_level_bytes(r["kind"], w, h)
        levels.append((w, h, off, n))
        off += n
        left -= n
        if (w <= 4 and h <= 4) or not smallest:
            break
        w, h = max(w // 2, 1), max(h // 2, 1)
    w, h, off, n = levels[-1]
    f.seek(off)
    data = f.read(n)
    if r["kind"] == 3:
        return Image.frombytes("RGBA", (w, h), data, "raw", "BGRA")
    img = Image.frombytes("RGBA", (max(w, 4), max(h, 4)), data, "bcn", 1 if r["kind"] == 1 else 3)
    return img.crop((0, 0, w, h))


def premultiplied(img):
    a = np.asarray(img.convert("RGBA"), dtype=np.float32) / 255.0
    return np.dstack([a[..., :3] * a[..., 3:4], a[..., 3]])


def build_hud(dump, pack, evgjk):
    """HUD textures of the dump replaced by the PC pack's; [(key, img, jade key)]."""
    with open(evgjk, "rb") as f:
        recs = [r for r in evgjk_records(f) if r["key"] & 0xFFFFFF00 == HUD_GROUP and r["rw"] > r["ow"]]
        small = {r["key"]: premultiplied(evgjk_level(f, r, True).resize((r["ow"], r["oh"]), Image.BOX))
                 for r in recs}
        done = []
        for src in sorted(dump.glob("*_fmt*.tga")):
            m = re.match(r"([0-9a-f]{16})_(\d+)x(\d+)_fmt", src.name)
            if not m:
                continue
            size = (int(m.group(2)), int(m.group(3)))
            cands = [r for r in recs if (r["ow"], r["oh"]) == size]
            if not cands:
                continue
            ours = premultiplied(read_tga(src))
            errs = sorted((float(np.abs(small[r["key"]] - ours).mean()), i) for i, r in enumerate(cands))
            best, nxt = errs[0][0], errs[1][0] if len(errs) > 1 else 1.0
            if best > HUD_MAX_ERROR or nxt < best * HUD_MIN_RATIO:
                continue
            r = cands[errs[0][1]]
            img = evgjk_level(f, r)
            write_tga(img, pack / (m.group(1) + ".tga"))
            done.append((m.group(1), img, r["key"]))
    return done


def find_magick():
    for p in (shutil.which("magick"), r"C:\Program Files\ImageMagick-7.1.2-Q16-HDRI\magick.exe"):
        if p and Path(p).exists():
            return p
    for d in Path(r"C:\Program Files").glob("ImageMagick*"):
        if (d / "magick.exe").exists():
            return str(d / "magick.exe")
    raise SystemExit("ImageMagick (magick.exe) is needed for the SVG")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", type=Path, required=True, help="xbox-retail game folder (MEDIA/AtlantisMF.ttf)")
    ap.add_argument("--pack", type=Path, required=True, help="texture pack folder (with dump/ from RESOT_TEXTURE_DUMP=1)")
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--preview", type=Path, help="also write upright previews into this folder")
    ap.add_argument("--hud-pack", type=Path, help="the PC version's PoP Texture Studio pack (Evgesha.JK) for the HUD")
    a = ap.parse_args()
    dump = a.pack / "dump"
    font_src = dump / (FONT_KEY + "_512x128_fmt06.tga")
    button_src = dump / (BUTTON_KEY + "_512x256_fmt06.tga")
    for p in (font_src, button_src):
        if not p.exists():
            raise SystemExit("%s is missing: run the game once with RESOT_TEXTURE_DUMP=1 up to the main menu" % p)
    ttf = a.game / "MEDIA" / "AtlantisMF.ttf"
    rects = json.loads((HERE / "font_rects.json").read_text(encoding="utf-8"))
    font, missing = build_font(read_tga(font_src), ttf, rects, a.scale)
    write_tga(font, a.pack / (FONT_KEY + ".tga"))
    buttons, n = build_buttons(read_tga(button_src), a.scale, find_magick())
    write_tga(buttons, a.pack / (BUTTON_KEY + ".tga"))
    print("font: %d glyphs at %dx%d%s" % (len(rects), font.width, font.height,
                                           "; " + " ".join(missing) if missing else ""))
    print("buttons: %dx%d, controller drawing traced into %d contours" % (buttons.width, buttons.height, n))
    hud = build_hud(dump, a.pack, a.hud_pack) if a.hud_pack else []
    for key, img, jade in hud:
        print("HUD: %s <- %08X %dx%d" % (key, jade, img.width, img.height))
    if a.hud_pack and not hud:
        print("HUD: no match (run a level with RESOT_TEXTURE_DUMP=1 first)")
    if a.preview:
        a.preview.mkdir(parents=True, exist_ok=True)
        for name, img in [("font", font), ("buttons", buttons)] + [("hud_" + k, i) for k, i, _ in hud]:
            up = img.transpose(Image.FLIP_TOP_BOTTOM)
            bg = Image.new("RGBA", up.size, (40, 44, 60, 255))
            bg.alpha_composite(up)
            bg.convert("RGB").save(a.preview / (name + ".png"))


if __name__ == "__main__":
    main()
