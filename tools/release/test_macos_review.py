"""Focused regression tests for the macOS release review fixes."""
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

import audit_package  # noqa: E402
import ed25519  # noqa: E402
import make_archive  # noqa: E402
import seal_package  # noqa: E402
from test_release_tools import macos_folder, macos_release_files  # noqa: E402

START_CHECK = HERE / "macos_start_check.sh"
PACKAGE = HERE / "package.sh"


def make_sealed_fixture(tmp_path: Path) -> tuple[Path, bytes]:
    secret = bytes(range(32))
    files = macos_release_files()
    files["payload.json"] = seal_package.payload_bytes(files, "v1.0.0", "macos-universal", "Test/repo")
    files["payload.json.sig"] = ed25519.sign(secret, files["payload.json"])
    folder = macos_folder(tmp_path, files)
    archive = tmp_path / f"{folder.name}.zip"
    make_archive.zip_file(folder, archive, 1_700_000_000)
    assert audit_package.check_release("macos-universal", archive) == []
    return archive, secret


@pytest.mark.skipif(sys.platform == "win32", reason="macOS release shell checks require a POSIX shell")
def test_macos_start_check_uses_fresh_full_unpack_and_external_logs(tmp_path: Path):
    archive, secret = make_sealed_fixture(tmp_path)
    stubs = tmp_path / "stubs"
    stubs.mkdir()
    cwd_file = tmp_path / "start-cwds"
    (stubs / "codesign").write_text("#!/bin/sh\nexit 0\n")
    (stubs / "arch").write_text(
        "#!/bin/sh\n"
        'printf "%s\\n" "$PWD" >> "$MACOS_START_CHECK_CWD_FILE"\n'
        "printf '%s\\n' 'GAME screen: MainMenu.apt'\n"
        "printf '%s\\n' 'GAME object world: ok=true, 4657 templates'\n"
    )
    for stub in stubs.iterdir():
        stub.chmod(0o755)
    env = {
        **os.environ,
        "PATH": f"{stubs}:{os.environ['PATH']}",
        "MACOS_START_CHECK_CWD_FILE": str(cwd_file),
        "ROTWK_INSTALL": "/private/rotwk",
        "BFME2_INSTALL": "/private/bfme2",
        "RW_GAME_DAT": "/private/rotwk/game.dat",
    }
    script = START_CHECK.read_text()
    assert script.index('audit_package.py" --release macos-universal') < script.index('make_archive.py" --extract-all')

    roots = []
    log_dirs = []
    cwd_dirs = []
    for _ in range(2):
        result = subprocess.run([str(START_CHECK), str(archive)], cwd=tmp_path, env=env, text=True,
                                capture_output=True, check=False)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert "MACOS START PASS: arm64 exit 0, MainMenu.apt, 4657 templates" in output
        assert "MACOS START PASS: x86_64 exit 0, MainMenu.apt, 4657 templates" in output
        roots.append(Path(next(line.split(": ", 1)[1] for line in output.splitlines() if line.startswith("MACOS START ROOT: "))))
        log_dirs.append(Path(next(line.split(": ", 1)[1] for line in output.splitlines() if line.startswith("MACOS START LOGS: "))))
        cwd_dirs.append(Path(next(line.split(": ", 1)[1] for line in output.splitlines() if line.startswith("MACOS START CWD: "))))

    assert roots[0] != roots[1]
    assert cwd_dirs[0] != cwd_dirs[1]
    for root, logs in zip(roots, log_dirs):
        assert root.is_dir() and logs.is_dir()
        assert not logs.is_relative_to(root)
        assert audit_package.check_release("macos-universal", root) == []
        assert all((root / name).is_file() for name in (
            "VERSION", "README_TESTERS.txt", "README_LAUNCHER.txt", "LICENSE", "NOTICE",
            "payload.json", "payload.json.sig",
        ))
        payload = (root / "payload.json").read_bytes()
        signature = (root / "payload.json.sig").read_bytes()
        extracted = {
            p.relative_to(root).as_posix(): p.read_bytes()
            for p in root.rglob("*")
            if p.is_file() and p.name not in ("payload.json", "payload.json.sig")
        }
        assert payload == seal_package.payload_bytes(extracted, "v1.0.0", "macos-universal", "Test/repo")
        assert ed25519.verify(ed25519.public_key(secret), payload, signature)
        assert not list(root.glob("*.log"))
        assert (logs / "start-arm64.log").is_file() and (logs / "start-x86_64.log").is_file()

    cwd_values = cwd_file.read_text().splitlines()
    assert len(cwd_values) == 4
    assert all(cwd_values.count(str(cwd)) == 2 for cwd in cwd_dirs)
    assert all(not cwd.is_relative_to(root.parent) for cwd, root in zip(cwd_dirs, roots))

    for path in [*(root.parent for root in roots), *log_dirs, *cwd_dirs]:
        shutil.rmtree(path)


@pytest.mark.skipif(sys.platform == "win32", reason="package.sh dirty-tree test requires a POSIX shell")
def test_version_override_does_not_hide_dirty_mac_tree(tmp_path: Path):
    bindir = tmp_path / "bin"
    bindir.mkdir()
    git = bindir / "git"
    git.write_text(
        "#!/bin/sh\n"
        'if [ "$1" = "-C" ]; then shift 2; fi\n'
        'case "$1" in\n'
        '  describe) echo "v1.0.0-1-g0123456789-dirty" ;;\n'
        '  rev-parse) echo "0123456789012345678901234567890123456789" ;;\n'
        '  log) echo "1700000000" ;;\n'
        '  *) echo "unexpected git invocation: $*" >&2; exit 65 ;;\n'
        "esac\n"
    )
    git.chmod(0o755)
    godot = bindir / "godot"
    marker = tmp_path / "godot-was-run"
    godot.write_text("#!/bin/sh\nprintf called > \"$GODOT_MARKER\"\nexit 99\n")
    godot.chmod(0o755)

    env = {**os.environ, "PATH": f"{bindir}:{os.environ['PATH']}", "GODOT": str(godot), "GODOT_MARKER": str(marker)}
    result = subprocess.run(
        ["/bin/bash", str(PACKAGE), "--platforms", "macos", "--version", "v2.0.0",
         "--templates", str(tmp_path / "templates.tpz"), "--out", str(tmp_path / "out")],
        cwd=tmp_path, env=env, text=True, capture_output=True, check=False,
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0
    assert "tree has local changes" in output and "-dirty" in output
    assert not marker.exists()


def test_direct_game_handover_explains_macos_updater_limitation():
    source = (REPO / "godot/scripts/release/update_handover.gd").read_text()
    body = source.split("static func why_not(scripted: bool) -> String:", 1)[1].split("\n\n##", 1)[0]
    assert body.index("if not OS.has_feature") < body.index("if OS.get_name() == \"macOS\"")
    assert body.index("if DisplayServer.get_name()") < body.index("if OS.get_name() == \"macOS\"")
    assert body.index("if scripted:") < body.index("if OS.get_name() == \"macOS\"")
    assert 'return "macOS app-bundle updates are not supported"' in body
