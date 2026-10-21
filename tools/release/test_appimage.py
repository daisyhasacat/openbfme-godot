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


def test_inline_ui_assets_stay_clean_with_lfs_filter(tmp_path):
    """Model runner LFS clean filtering without needing git-lfs on the test host."""
    import os
    attributes = (appimage.ROOT / '.gitattributes').read_text()
    exception = 'godot/assets/**/*.png binary -filter'
    assert exception in attributes
    repo = tmp_path / 'repo'
    repo.mkdir()

    def git(*args, check=True):
        return subprocess.run(['git', '-C', str(repo), *args], check=check,
                              text=True, capture_output=True)

    git('init')
    git('config', 'user.name', 'Fixture')
    git('config', 'user.email', 'fixture@example.invalid')
    image = repo / 'godot/assets/platform_icons/fixture.png'
    image.parent.mkdir(parents=True)
    image.write_bytes(b'\x89PNG\r\n\x1a\nsynthetic inline UI asset')
    attr = repo / '.gitattributes'
    attr.write_text(attributes.replace(exception, ''))
    git('add', '.gitattributes', 'godot/assets/platform_icons/fixture.png')
    git('commit', '-m', 'Synthetic inline asset')
    # A clean filter has the same relevant behavior as LFS: raw bytes clean to
    # a pointer, whereas an already-normalized pointer stays a pointer.
    cleaner = tmp_path / 'clean.py'
    cleaner.write_text('import sys,hashlib\nb=sys.stdin.buffer.read()\n'
                       'sys.stdout.buffer.write(b if b.startswith(b"version https://git-lfs") else '
                       'b"version https://git-lfs.github.com/spec/v1\\noid sha256:" + '
                       'hashlib.sha256(b).hexdigest().encode() + b"\\nsize " + str(len(b)).encode() + b"\\n")\n')
    import shlex
    git('config', 'filter.lfs.clean', shlex.join([sys.executable, str(cleaner)]))
    stamp = image.stat().st_mtime + 10
    os.utime(image, (stamp, stamp))
    assert git('describe', '--always', '--dirty').stdout.strip().endswith('-dirty')
    assert 'fixture.png' in git('diff', '--name-only').stdout
    attr.write_text(attributes)
    git('add', '.gitattributes')
    git('commit', '-m', 'Keep bundled assets inline')
    assert git('diff', '--exit-code', 'HEAD').returncode == 0
    assert not git('describe', '--always', '--dirty').stdout.strip().endswith('-dirty')
    for path in ('godot/assets/openbfme-icon.png', 'godot/assets/brand/poweredby_openbfme.png',
                 'godot/assets/platform_icons/linux.png'):
        assert git('check-attr', 'filter', '--', path).stdout.strip().endswith('filter: unset')


def test_workflow_preserves_release_folder_name_when_extracting(tmp_path):
    import os
    import shutil

    import audit_package
    import make_archive

    version = "v1.2.3"
    name = f"openbfme-{version}-linux-x64"
    source = tmp_path / "source" / name
    source.mkdir(parents=True)
    (source / "VERSION").write_text(version + "\n")
    runner = tmp_path / "runner temp"
    archives = runner / "linux-package"
    archives.mkdir(parents=True)
    make_archive.tar_gz(source, archives / f"{name}.tar.gz", 0)
    workspace = tmp_path / "workspace"
    scripts = workspace / "tools/release"
    scripts.mkdir(parents=True)
    shutil.copyfile(appimage.ROOT / "tools/release/make_archive.py", scripts / "make_archive.py")
    # Capture the actual workflow's stage arguments after the real extractor runs.
    (scripts / "appimage.py").write_text(
        "import sys\nfrom pathlib import Path\n"
        "Path('stage-args.txt').write_text('\\n'.join(sys.argv[1:]))\n")
    workflow = (appimage.ROOT / ".github/workflows/appimage.yml").read_text()
    block = workflow.split("      - name: Create AppImage and checksum\n", 1)[1]
    commands = block.split("          version=", 1)[1].split("\n", 1)[1]
    commands = commands.split("          desktop-file-validate", 1)[0]
    subprocess.run(["bash", "-euc", commands], cwd=workspace, check=True,
                   env={**os.environ, "RUNNER_TEMP": str(runner), "version": version})
    args = (workspace / "stage-args.txt").read_text().splitlines()
    assert args == ["stage", "--package", str(runner / name), "--output", str(runner / "OpenBFME.AppDir")]
    assert audit_package.TOP_DIR.fullmatch(Path(args[2]).name) is not None
    assert (Path(args[2]) / "VERSION").read_text() == version + "\n"
