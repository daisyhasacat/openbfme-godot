"""The macOS launcher paths, fail-closed updates, discovery and signed bundle inventory."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import seal_package  # noqa: E402


def godot_executable() -> str | None:
    return os.environ.get("GODOT") or shutil.which("godot")


def test_macos_launcher_platform_and_payload(tmp_path: Path):
    godot = godot_executable()
    if not godot:
        pytest.skip("SKIPPED LOUDLY: Godot is not available for the macOS launcher script test")

    secret = bytes(range(32))
    files = {
        "OpenBFME.app/Contents/Info.plist": bytes([1, 2, 3, 4]),
        "OpenBFMELauncher.app/Contents/Info.plist": bytes([1, 2, 3, 4]),
    }
    payload = seal_package.payload_bytes(files, "v1.0.0", "macos-universal", "Test/repo")
    payload_path = tmp_path / "payload.json"
    sig_path = tmp_path / "payload.json.sig"
    payload_path.write_bytes(payload)
    sig_path.write_bytes(ed25519.sign(secret, payload))

    home = tmp_path / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), ROTWK_INSTALL="", BFME2_INSTALL="")
    command = [
        godot, "--headless", "--path", str(REPO / "launcher"), "--script",
        "res://tests/macos_platform_test.gd", "--",
        f"--payload={payload_path}", f"--sig={sig_path}", f"--key={ed25519.public_key(secret).hex()}",
    ]
    result = subprocess.run(command, cwd=REPO, env=env, capture_output=True, text=True, check=False)
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "MACOS UNIT OK" in output, output


def test_the_universal_package_manifest_drops_only_the_arm64_entries(tmp_path):
    """DEVPORT-6: the source project names the arm64 library (the default Apple Silicon build) beside the universal one; package.sh's
    universal package carries only the universal library, so its staged manifest loses exactly the two arm64 lines (package.sh's own
    command, run here on the real manifest), and a universal CMake build also writes the arm64 slice under the arm64 name"""
    import re
    import subprocess
    root = Path(__file__).resolve().parents[2]
    source = (root / "godot/openbfme.gdextension").read_text()
    line = next(ln for ln in (root / "tools/release/package.sh").read_text().splitlines() if "arm64 = /d" in ln)
    staged = tmp_path / "openbfme.gdextension"
    staged.write_text(source)
    command = line.strip().replace('"$STAGE/godot/openbfme.gdextension"', str(staged))
    assert subprocess.run(["bash", "-c", command], capture_output=True).returncode == 0
    arm64 = [ln for ln in source.splitlines() if re.match(r"macos\.(debug|release)\.arm64 = ", ln)]
    assert len(arm64) == 2
    assert staged.read_text().splitlines() == [ln for ln in source.splitlines() if ln not in arm64]
    assert 'macos.debug = "res://bin/openbfme.macos.template_debug.universal.dylib"' in staged.read_text()
    cmake = (root / "engine/CMakeLists.txt").read_text()
    assert ('-thin arm64 -output "${OPENBFME_GODOT_BIN}/openbfme.macos.template_debug.arm64.dylib"' in cmake
            and '"arm64" IN_LIST CMAKE_OSX_ARCHITECTURES AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES' in cmake)
