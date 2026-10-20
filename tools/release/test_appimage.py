import hashlib
from pathlib import Path
import subprocess
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import appimage
import public_workflows


def test_workflow_policy():
    path = appimage.ROOT / ".github/workflows/appimage.yml"
    assert public_workflows.workflow_problems(str(path), path.read_text()) == []


def test_download_checksum_mismatch_is_not_installed(tmp_path, monkeypatch):
    def download(command, **_kwargs):
        Path(command[command.index("--output") + 1]).write_bytes(b"corrupt")
    monkeypatch.setattr(subprocess, "run", download)
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        appimage.fetch_asset(tmp_path, "tool", "https://example.invalid/tool", "0" * 64)
    assert list(tmp_path.iterdir()) == []


def test_corrupt_cache_is_replaced_and_valid_cache_is_reused(tmp_path, monkeypatch):
    payload = b"verified tool bytes"
    expected = hashlib.sha256(payload).hexdigest()
    (tmp_path / "tool").write_bytes(b"corrupt cache")
    downloads = []
    def download(command, **_kwargs):
        downloads.append(command)
        Path(command[command.index("--output") + 1]).write_bytes(payload)
    monkeypatch.setattr(subprocess, "run", download)
    first = appimage.fetch_asset(tmp_path, "tool", "https://example.invalid/tool", expected)
    second = appimage.fetch_asset(tmp_path, "tool", "https://example.invalid/tool", expected)
    assert first == second
    assert first.read_bytes() == payload
    assert len(downloads) == 1


def test_rejected_payload_never_creates_appdir(tmp_path, monkeypatch):
    def reject(command, **_kwargs):
        raise subprocess.CalledProcessError(1, command)
    monkeypatch.setattr(subprocess, "run", reject)
    with pytest.raises(subprocess.CalledProcessError):
        appimage.stage(tmp_path / "bad-package", tmp_path / "AppDir")
    assert not (tmp_path / "AppDir").exists()


def test_apprun_launches_from_any_directory_and_preserves_arguments(tmp_path, monkeypatch):
    package = tmp_path / "package"
    package.mkdir()
    (package / "OpenBFME.x86_64").write_text(
        '#!/bin/sh\nprintf "%s\\n" "$PWD" "$OPENBFME_NO_UPDATE_CHECK" "$@"\n')
    output = tmp_path / "Open BFME.AppDir"
    # The audit itself has its own corpus tests; here a harmless shell stub
    # makes the actual generated AppRun executable testable on macOS too.
    with monkeypatch.context() as patch:
        patch.setattr(subprocess, "run", lambda *_args, **_kwargs: None)
        appimage.stage(package, output)
    result = subprocess.run([str(output / "AppRun"), "one two", "", "--headless"],
                            cwd=tmp_path, text=True, capture_output=True, check=True)
    assert result.stdout.splitlines() == [str((output / "usr/lib/openbfme").resolve()), "1", "one two", "", "--headless"]
    assert (output / ".DirIcon").resolve() == output / "openbfme.png"
    assert "Icon=openbfme\n" in (output / "openbfme.desktop").read_text()
    with pytest.raises(ValueError, match="already exists"):
        with monkeypatch.context() as patch:
            patch.setattr(subprocess, "run", lambda *_args, **_kwargs: None)
            appimage.stage(package, output)
