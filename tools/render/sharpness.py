#!/usr/bin/env python3
"""Compares the sharpness of captures of one frame taken with different anti-aliasing / anisotropic settings (lane OWNERFIX-1, the owner's "16x looks
awful, super blurry").

    python3 tools/render/sharpness.py --ref <off-off.png> <other.png> [...] [--min-ratio 0.85]

The metric is the mean absolute Laplacian of the luma (0.299 R + 0.587 G + 0.114 B) over the whole picture: the high-frequency energy a blur removes.
Each capture prints its value and its ratio to the reference (the settings off). Exit 0 when every capture's ratio is at least --min-ratio (default 0.85:
anti-aliasing smooths only the edges, a blurred picture loses texture detail everywhere), 1 otherwise, 2 for an unusable input (no file, not a PNG, a size
different from the reference's, a flat picture with no detail at all): the check never passes on nothing.
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from globe_probe import read_png  # noqa: E402


class Unusable(Exception):
    pass


def luma_rows(path: str):
    if not os.path.isfile(path):
        raise Unusable(f"{path}: no such file")
    try:
        w, h, bpp, rows = read_png(path)
    except SystemExit as e:  # read_png refuses with SystemExit
        raise Unusable(str(e)) from None
    except Exception as e:  # a truncated / corrupt file
        raise Unusable(f"{path}: unreadable PNG ({e})") from None
    if w < 3 or h < 3 or len(rows) != h:
        raise Unusable(f"{path}: {w}x{h} is too small to measure")
    out = []
    for line in rows:
        out.append([(299 * line[i] + 587 * line[i + 1] + 114 * line[i + 2]) / 1000.0 for i in range(0, w * bpp, bpp)])
    return w, h, out


def sharpness(path: str):
    w, h, y = luma_rows(path)
    total = 0.0
    for j in range(1, h - 1):
        up, row, down = y[j - 1], y[j], y[j + 1]
        for i in range(1, w - 1):
            total += abs(4.0 * row[i] - row[i - 1] - row[i + 1] - up[i] - down[i])
    value = total / ((w - 2) * (h - 2))
    if value <= 0.0:
        raise Unusable(f"{path}: a flat picture (no detail to measure)")
    return w, h, value


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ref", required=True, help="the capture with anti-aliasing and anisotropic filtering off")
    ap.add_argument("--min-ratio", type=float, default=0.85)
    ap.add_argument("captures", nargs="+")
    a = ap.parse_args(argv)
    try:
        rw, rh, ref = sharpness(a.ref)
        print(f"{a.ref}: {ref:.3f} (reference)")
        worst = None
        for c in a.captures:
            w, h, v = sharpness(c)
            if (w, h) != (rw, rh):
                raise Unusable(f"{c}: {w}x{h}, the reference is {rw}x{rh}")
            ratio = v / ref
            print(f"{c}: {v:.3f} ratio {ratio:.3f}{'' if ratio >= a.min_ratio else '  BLURRY'}")
            worst = ratio if worst is None else min(worst, ratio)
    except Unusable as e:
        print(f"sharpness: {e}", file=sys.stderr)
        return 2
    return 0 if worst >= a.min_ratio else 1


if __name__ == "__main__":
    sys.exit(main())
