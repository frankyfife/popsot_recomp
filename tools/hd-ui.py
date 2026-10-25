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

Inputs are the two original atlases as written by RESOT_TEXTURE_DUMP=1
(<pack>/dump/<key>_512x128_fmt06.tga and <key>_512x256_fmt06.tga). Both are
stored upside down, as the game samples them; the files written here keep
that orientation. Needs Pillow, NumPy, SciPy and ImageMagick (librsvg) for the
SVG.
"""
import argparse
import json
import shutil
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
DIAGRAM = (237, 0, 512, 256)       # controller drawing, upright atlas pixels


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
        # clip to the cell, so a neighbour never receives ink
        ga = np.asarray(g)
        cx0, cy0, cx1, cy1 = x0 * scale, y0 * scale, x1 * scale, y1 * scale
        ax0, ay0 = max(gx, cx0), max(gy, cy0)
        ax1, ay1 = min(gx + ga.shape[1], cx1), min(gy + ga.shape[0], cy1)
        if ax1 > ax0 and ay1 > ay0:
            sub = ga[ay0 - gy:ay1 - gy, ax0 - gx:ax1 - gx]
            out_a[ay0:ay1, ax0:ax1] = np.maximum(out_a[ay0:ay1, ax0:ax1], sub)
    # check each glyph against the original; where the TTF shape differs
    # (a different design or no glyph), trace the original instead
    small = np.asarray(Image.fromarray(out_a).resize((W, H), Image.BOX)).astype(np.float32) / 255.0
    for code, (u0, u1, v0, v1) in rects.items():
        x0, x1 = int(round(u0 - 0.5)), int(round(u1))
        y0, y1 = int(round(v0 - 0.5)), int(round(v1))
        a, b = A[y0:y1, x0:x1], small[y0:y1, x0:x1]
        s = float((a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9))
        if s < FALLBACK_BELOW:
            out_a[y0 * scale:y1 * scale, x0 * scale:x1 * scale] = traced_upscale(a, scale)
            fallback.append(code)
    out = Image.fromarray(out_a)
    white = Image.new("L", out.size, 255)
    return Image.merge("RGBA", (white, white, white, out)), missing + ["traced:" + ",".join(fallback)] if fallback else missing


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
    return img.transpose(Image.FLIP_TOP_BOTTOM), n


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
    if a.preview:
        a.preview.mkdir(parents=True, exist_ok=True)
        for name, img in (("font", font), ("buttons", buttons)):
            up = img.transpose(Image.FLIP_TOP_BOTTOM)
            bg = Image.new("RGBA", up.size, (40, 44, 60, 255))
            bg.alpha_composite(up)
            bg.convert("RGB").save(a.preview / (name + ".png"))


if __name__ == "__main__":
    main()
