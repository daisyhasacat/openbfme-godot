"""tools/render/sharpness.py (lane OWNERFIX-1): it ranks a blurred picture below the sharp one and refuses every unusable input."""
from __future__ import annotations

import os
import struct
import sys
import zlib

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sharpness  # noqa: E402


def write_png(path, w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw.extend(pixel(x, y))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def checker(x, y):
    v = 255 if (x // 2 + y // 2) % 2 else 0
    return (v, v, v)


def blurred(x, y):
    # the checker through a 3x3 box filter
    s = 0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            s += checker(x + dx, y + dy)[0]
    v = s // 9
    return (v, v, v)


@pytest.fixture
def pics(tmp_path):
    p = {}
    for name, fn, size in (("sharp", checker, 32), ("blur", blurred, 32), ("flat", lambda x, y: (90, 90, 90), 32), ("small", checker, 16)):
        p[name] = str(tmp_path / f"{name}.png")
        write_png(p[name], size, size, fn)
    p["trunc"] = str(tmp_path / "trunc.png")
    open(p["trunc"], "wb").write(open(p["sharp"], "rb").read()[:60])
    p["text"] = str(tmp_path / "text.png")
    open(p["text"], "w").write("not a png")
    return p


def test_identical_passes(pics):
    assert sharpness.main(["--ref", pics["sharp"], pics["sharp"]]) == 0


def test_blurred_fails(pics, capsys):
    assert sharpness.main(["--ref", pics["sharp"], pics["blur"]]) == 1
    assert "BLURRY" in capsys.readouterr().out


def test_blur_ratio_is_independent_value(pics):
    # independent expectation: the 2x2 checker's Laplacian energy against its box-blurred copy; the blurred copy keeps well under half of it
    _, _, s = sharpness.sharpness(pics["sharp"])
    _, _, b = sharpness.sharpness(pics["blur"])
    assert b / s < 0.5  # tolerance: the property is the ordering, the exact ratio depends on the border


@pytest.mark.parametrize("bad", ["missing", "flat", "small", "trunc", "text"])
def test_unusable_inputs_refused(pics, tmp_path, bad):
    path = str(tmp_path / "nope.png") if bad == "missing" else pics[bad]
    assert sharpness.main(["--ref", pics["sharp"], path]) == 2


def test_unusable_reference_refused(pics):
    assert sharpness.main(["--ref", pics["flat"], pics["sharp"]]) == 2


def test_no_captures_refused(pics):
    with pytest.raises(SystemExit) as e:
        sharpness.main(["--ref", pics["sharp"]])
    assert e.value.code == 2
