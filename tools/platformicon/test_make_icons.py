"""Lane PLATFORMICON-1: the committed platform icons are what tools/platformicon/make_icons.py draws, and are plain monochrome glyphs of our own."""
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import make_icons as m  # noqa: E402

ICONS = HERE.parent.parent / "godot" / "assets" / "platform_icons"
NAMES = ["windows", "linux", "macos"]


def pixels(name):
    w, h, rows = m.decode_png((ICONS / f"{name}.png").read_bytes())
    return w, h, rows


def test_the_committed_files_are_the_inventory():
    # the exact set, by name: a fourth file or a missing one fails
    assert sorted(p.name for p in ICONS.iterdir() if p.suffix == ".png") == sorted(f"{n}.png" for n in NAMES)


@pytest.mark.parametrize("name", NAMES)
def test_each_icon_is_a_32_pixel_white_glyph_on_transparent(name):
    w, h, rows = pixels(name)
    assert (w, h) == (32, 32)
    covered = 0
    for row in rows:
        for x in range(w):
            r, g, b, a = row[x * 4:x * 4 + 4]
            assert (r, g, b) == (255, 255, 255)
            covered += 1 if a else 0
    assert 150 < covered < 700, covered
    assert all(rows[0][x * 4 + 3] == 0 for x in range(w)), "the top row is empty (margin)"


def test_the_three_glyphs_differ():
    assert len({pixels(n)[2] and b"".join(pixels(n)[2]) for n in NAMES}) == 3


def test_the_files_equal_the_script_drawing():
    done = subprocess.run([sys.executable, str(HERE / "make_icons.py"), "--check"], capture_output=True, text=True)
    assert done.returncode == 0, done.stdout + done.stderr


def test_the_check_fails_on_a_missing_or_changed_file(tmp_path):
    # the checker fails closed: an empty folder and a folder with one changed file are both refused
    empty = subprocess.run([sys.executable, str(HERE / "make_icons.py"), "--check", str(tmp_path)], capture_output=True, text=True)
    assert empty.returncode == 1
    for n in NAMES:
        (tmp_path / f"{n}.png").write_bytes((ICONS / f"{n}.png").read_bytes())
    assert subprocess.run([sys.executable, str(HERE / "make_icons.py"), "--check", str(tmp_path)]).returncode == 0
    data = bytearray((tmp_path / "linux.png").read_bytes())
    data[-20] ^= 1
    (tmp_path / "linux.png").write_bytes(bytes(data))
    assert subprocess.run([sys.executable, str(HERE / "make_icons.py"), "--check", str(tmp_path)], capture_output=True).returncode == 1


def test_the_pixel_hashes_the_godot_test_pins_are_the_files():
    # godot/tests/platformicon1_test.gd pins these: an import of an older picture fails it
    import hashlib
    gd = (HERE.parent.parent / "godot" / "tests" / "platformicon1_test.gd").read_text()
    for n in NAMES:
        digest = hashlib.sha256(b"".join(pixels(n)[2])).hexdigest()
        assert f'"{n}": "{digest}"' in gd, n
