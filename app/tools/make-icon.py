# SPDX-License-Identifier: GPL-3.0-only
# Draws app/sce_sys/icon0.png (512x512): a sun rising behind a play button on a night sky.
# Python standard library only: python app/tools/make-icon.py
import math
import struct
import sys
import zlib
from pathlib import Path

N = 512


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def smooth(edge, x):
    return max(0.0, min(1.0, 0.5 - x / edge))


def pixel(x, y):
    # Sky: deep blue at the top to violet near the horizon.
    t = y / N
    color = mix((11, 16, 36), (59, 36, 84), t ** 1.6)
    # Sun with a glow, centred below the middle.
    cx, cy, r = 256, 300, 150
    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
    glow = max(0.0, 1.0 - (d - r) / 140) ** 2 if d > r else 1.0
    color = mix(color, (245, 165, 36), 0.55 * glow)
    sun = mix((255, 214, 102), (240, 120, 30), max(0.0, min(1.0, (y - (cy - r)) / (2 * r))))
    color = mix(color, sun, smooth(1.5, d - r))
    # Horizon: the lower part is dark ground with faint stripes.
    if y >= 380:
        ground = (16, 14, 30)
        stripe = 0.5 + 0.5 * math.cos((y - 380) * 0.35)
        color = mix(color, ground, 0.85 + 0.1 * stripe)
    # Play triangle over the sun.
    px, py = x + 0.5 - 256, y + 0.5 - 292
    edges = [
        (px + 50) * -1,                       # left edge x = -50
        (px * 0.5 + py * 0.866 - 50) / 1.0,   # lower right edge
        (px * 0.5 - py * 0.866 - 50) / 1.0,   # upper right edge
    ]
    inside = max(edges)
    color = mix(color, (20, 18, 40), smooth(1.5, inside) * 0.92)
    # Opaque: the home screen rounds the tile itself.
    return tuple(int(round(c)) for c in color) + (255,)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "sce_sys" / "icon0.png")
    rows = bytearray()
    for y in range(N):
        rows.append(0)
        for x in range(N):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
