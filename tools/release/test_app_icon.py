"""Lane OWNERFIX-3: tools/release/app_icon.py (the OpenBFME icon of the game and the launcher)."""
import re
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import app_icon  # noqa: E402

REPO = Path(__file__).resolve().parents[2]


def test_generated_files_match_the_source():
    # the committed .icns, launcher copies and GDScript icon constants are exactly what the source .ico gives
    assert app_icon.main(["app_icon.py", "--check"]) == 0


def test_the_source_is_six_png_frames_of_godots_sizes():
    frames = app_icon.read_ico(app_icon.SOURCE.read_bytes())
    assert sorted(frames) == [16, 32, 48, 64, 128, 256]
    assert all(p.startswith(app_icon.PNG_MAGIC) for p in frames.values())


def test_icns_holds_the_png_frames():
    frames = app_icon.read_ico(app_icon.MAC_SOURCE.read_bytes())
    icns = app_icon.make_icns(frames)
    assert icns[:4] == b"icns" and struct.unpack(">I", icns[4:8])[0] == len(icns)
    pos, seen = 8, {}
    while pos < len(icns):
        kind, size = icns[pos:pos + 4], struct.unpack(">I", icns[pos + 4:pos + 8])[0]
        seen[kind] = icns[pos + 8:pos + size]
        pos += size
    assert seen == {t: frames[e] for e, t in app_icon.ICNS_TYPES.items()}


def ico(frames):
    hdr = struct.pack("<HHH", 0, 1, len(frames))
    off, dirs, body = 6 + 16 * len(frames), b"", b""
    for w, png in frames:
        dirs += struct.pack("<BBBBHHII", w % 256, w % 256, 0, 0, 1, 32, len(png), off + len(body))
        body += png
    return hdr + dirs + body


def png(w):
    return app_icon.PNG_MAGIC + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", w, w) + b"\x08\x06\x00\x00\x00" + b"\x00" * 4


@pytest.mark.parametrize("data,why", [
    (b"", "truncated"),
    (b"\x00\x00\x02\x00\x01\x00" + b"\x00" * 16, "not an icon"),                                 # a cursor file
    (ico([(16, png(16))]), "frames [16]"),                                                      # too few sizes
    (ico([(s, png(s)) for s in (16, 24, 32, 48, 64, 128, 256)]), "expected"),                  # Godot's exporter ignores a 7-frame .ico
    (ico([(16, png(32))] + [(s, png(s)) for s in (32, 48, 64, 128, 256)]), "the PNG is 32x32"),
    (ico([(16, b"BM" + b"\x00" * 40)] + [(s, png(s)) for s in (32, 48, 64, 128, 256)]), "not PNG data"),
    (ico([(s, png(s)) for s in (16, 32, 48, 64, 128, 256)])[:-10], "runs past the end"),
])
def test_read_ico_refuses(data, why):
    with pytest.raises(ValueError, match=re.escape(why)):
        app_icon.read_ico(data)


def test_check_exe_refuses_an_executable_without_our_icon(tmp_path):
    exe = tmp_path / "x.exe"
    exe.write_bytes(b"MZ" + b"\x00" * 200)
    assert app_icon.check_exe(exe) != []
    assert app_icon.main(["app_icon.py", "--check-exe"]) == 2  # no executable named: refused, not passed


def test_the_presets_name_the_icons():
    game = (REPO / "godot/export_presets.cfg").read_text(encoding="utf-8")
    launcher = (REPO / "launcher/export_presets.cfg").read_text(encoding="utf-8")
    assert game.count('application/icon="res://assets/app_icon/openbfme.ico"') == 1
    assert game.count('application/icon="res://assets/app_icon/openbfme.icns"') == 3
    assert launcher.count('application/icon="res://assets/app_icon/openbfme.ico"') == 1
    assert launcher.count('application/icon="res://assets/app_icon/openbfme.icns"') == 1
    assert "application/modify_resources=false" not in game + launcher
    # the icon files stay out of the packs (the release audit allows no image files there)
    assert (REPO / "godot/assets/app_icon/.gdignore").exists() and (REPO / "launcher/assets/app_icon/.gdignore").exists()


def test_runtime_icon_keeps_windows_and_mac_sources_separate():
    windows = app_icon.read_ico(app_icon.SOURCE.read_bytes())[256]
    mac = app_icon.read_ico(app_icon.MAC_SOURCE.read_bytes())[256]
    assert windows != mac
    script = app_icon.make_gd(windows, "test", mac)
    import base64
    for constant, expected in [("PNG_BASE64", windows), ("MAC_PNG_BASE64", mac)]:
        block = script.split("const " + constant + " := (", 1)[1].split(")", 1)[0]
        encoded = "".join(re.findall(r'"([A-Za-z0-9+/=]*)"', block))
        assert base64.b64decode(encoded) == expected
    assert 'image(OS.get_name() == "macOS")' in script
