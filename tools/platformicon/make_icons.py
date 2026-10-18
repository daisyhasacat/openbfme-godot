#!/usr/bin/env python3
"""Lane PLATFORMICON-1: draws the three platform icons (our own artwork, never a vendor's logo).

Plain monochrome glyphs on a 32 x 32 grid, white on transparent: a window grid (Windows), a penguin (Linux) and an apple (macOS). Each shape is a
union / difference of ellipses and rectangles, sampled 8 x 8 per pixel for the edge's coverage (the alpha), written with the standard library only.

  python3 tools/platformicon/make_icons.py [out_dir]     (default godot/assets/platform_icons)
  python3 tools/platformicon/make_icons.py --check        (the committed files equal what this script draws)
"""
import math
import struct
import sys
import zlib
from pathlib import Path

SIZE = 32
SAMPLES = 8


def ellipse(cx, cy, rx, ry, angle=0.0):
    ca, sa = math.cos(math.radians(angle)), math.sin(math.radians(angle))

    def inside(x, y):
        dx, dy = x - cx, y - cy
        u, v = dx * ca + dy * sa, -dx * sa + dy * ca
        return (u / rx) ** 2 + (v / ry) ** 2 <= 1.0

    return inside


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x < x1 and y0 <= y < y1


def union(*shapes):
    return lambda x, y: any(s(x, y) for s in shapes)


def minus(shape, *holes):
    return lambda x, y: shape(x, y) and not any(h(x, y) for h in holes)


# four panes of a window, a gap between them
WINDOWS = union(rect(4, 5, 15, 15), rect(17, 5, 28, 15), rect(4, 17, 15, 27), rect(17, 17, 28, 27))

# a penguin: the body, a belly and the face cut out of it, two feet
LINUX = union(
    minus(
        union(ellipse(16, 18, 8.6, 11.4), ellipse(16, 9.5, 5.6, 6.0)),
        ellipse(16, 20.5, 5.0, 7.2),  # the belly
        ellipse(13.6, 8.6, 1.25, 1.6), ellipse(18.4, 8.6, 1.25, 1.6),  # the eyes
        ellipse(16, 11.8, 2.3, 1.05),  # the beak
    ),
    ellipse(11.2, 28.6, 3.8, 1.7), ellipse(20.8, 28.6, 3.8, 1.7),
)

# a plain whole apple: a round body with a small dip at the top and a short straight stem
MACOS = union(
    minus(
        union(ellipse(12.6, 18.2, 7.9, 8.8), ellipse(19.4, 18.2, 7.9, 8.8), rect(12.6, 10.5, 19.4, 24.5)),
        ellipse(16, 9.6, 2.4, 1.6),  # the dip
    ),
    rect(15.2, 4.5, 16.8, 10.2),  # the stem
)

SHAPES = {"windows": WINDOWS, "linux": LINUX, "macos": MACOS}


def render(shape):
    """Rows of (r, g, b, a): white, the alpha is the coverage."""
    rows = []
    n = SAMPLES * SAMPLES
    for py in range(SIZE):
        row = bytearray()
        for px in range(SIZE):
            hit = 0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    if shape(px + (sx + 0.5) / SAMPLES, py + (sy + 0.5) / SAMPLES):
                        hit += 1
            row += bytes((255, 255, 255, round(255 * hit / n)))
        rows.append(bytes(row))
    return rows


def png_bytes(rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def decode_png(data):
    """(width, height, rows of RGBA bytes) of a PNG made by png_bytes (8-bit RGBA, no interlace, filter 0); ValueError otherwise."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, size = 8, b"", None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0] != zlib.crc32(tag + body) & 0xFFFFFFFF:
            raise ValueError("bad chunk checksum")
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, ctype, interlace) != (8, 6, 0):
                raise ValueError("not 8-bit RGBA without interlace")
            size = (w, h)
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    if size is None:
        raise ValueError("no IHDR")
    raw = zlib.decompress(idat)
    stride = size[0] * 4 + 1
    if len(raw) != stride * size[1]:
        raise ValueError("wrong pixel data length")
    rows = []
    for y in range(size[1]):
        if raw[y * stride] != 0:
            raise ValueError("a filtered row")
        rows.append(raw[y * stride + 1:(y + 1) * stride])
    return size[0], size[1], rows


def main(argv):
    check = "--check" in argv
    args = [a for a in argv if not a.startswith("--")]
    out = Path(args[0]) if args else Path(__file__).resolve().parents[2] / "godot" / "assets" / "platform_icons"
    bad = 0
    for name, shape in SHAPES.items():
        data = png_bytes(render(shape))
        path = out / f"{name}.png"
        if check:
            if not path.is_file() or path.read_bytes() != data:
                print(f"{path}: differs from the script's drawing")
                bad += 1
        else:
            out.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f"wrote {path} ({len(data)} bytes)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
