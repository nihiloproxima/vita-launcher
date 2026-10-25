#!/usr/bin/env python3
"""Génère les images de la bulle LiveArea (icon0, bg, startup) puis les passe en PNG 8 bits indexé
avec pngquant : la Vita refuse d'installer un VPK dont les images ne sont pas indexées.

usage : python3 tools/make_assets.py   (depuis la racine du projet, pngquant requis)
"""
import math
import struct
import subprocess
import zlib

BG_INNER = (66, 58, 140)
BG_OUTER = (12, 12, 20)


def write_png(path, w, h, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def round_rect_sdf(px, py, cx, cy, hw, hh, r):
    qx, qy = abs(px - cx) - (hw - r), abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r


def blend(dst, color, alpha):
    return tuple(d + (c - d) * alpha for d, c in zip(dst, color))


def render(w, h, motif_cx, motif_cy, scale, glow_x, glow_y):
    """Fond dégradé + motif du launcher : une rangée de trois tuiles, celle du milieu sélectionnée."""
    big, small, gap = 46 * scale, 32 * scale, 8 * scale
    shapes = [  # (centre x, centre y, demi-largeur, demi-hauteur, rayon, opacité)
        (motif_cx - big / 2 - gap - small / 2, motif_cy, small / 2, small / 2, small * 0.22, 0.35),
        (motif_cx + big / 2 + gap + small / 2, motif_cy, small / 2, small / 2, small * 0.22, 0.35),
        (motif_cx, motif_cy, big / 2, big / 2, big * 0.22, 1.0),
        (motif_cx, motif_cy + big / 2 + 9 * scale, 8 * scale, 2 * scale, 2 * scale, 1.0),
    ]
    reach = math.hypot(w, h)
    pixels = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            k = min(math.hypot(x - glow_x, y - glow_y) / reach * 1.6, 1.0)
            color = blend(BG_INNER, BG_OUTER, k ** 0.8)
            for cx, cy, hw, hh, r, opacity in shapes:
                coverage = min(max(0.5 - round_rect_sdf(x + 0.5, y + 0.5, cx, cy, hw, hh, r), 0), 1)
                if coverage:
                    color = blend(color, (255, 255, 255), coverage * opacity)
            i = (y * w + x) * 4
            pixels[i:i + 4] = bytes((round(color[0]), round(color[1]), round(color[2]), 255))
    return pixels


ASSETS = [
    ("sce_sys/icon0.png", 128, 128, dict(motif_cx=64, motif_cy=58, scale=0.78, glow_x=34, glow_y=24)),  # la Vita découpe l'icône en cercle
    ("sce_sys/livearea/contents/bg.png", 840, 500, dict(motif_cx=600, motif_cy=240, scale=3.2, glow_x=620, glow_y=120)),
    ("sce_sys/livearea/contents/startup.png", 280, 158, dict(motif_cx=140, motif_cy=72, scale=1.15, glow_x=60, glow_y=20)),
]

if __name__ == "__main__":
    for path, w, h, params in ASSETS:
        write_png(path, w, h, render(w, h, **params))
        subprocess.run(["pngquant", "--force", "--ext", ".png", "256", path], check=True)
        print("généré :", path)
