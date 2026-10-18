"""Test-only native Windows launcher exporter; never used for production releases."""
from __future__ import annotations

import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import audit_package
import make_archive


def _run(command: list[str], ignore_failure: bool = False, timeout: int = 900) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    if result.returncode and not ignore_failure:
        raise RuntimeError(f"NATIVE EXPORT ERROR: {command[0]} {command[1:]} failed:\n{result.stdout}{result.stderr}")
    return result


def export_launcher(repo: Path, out: Path, version: str, commit: str, repository: str, public_key: str,
                    godot: str, mtime: int, test_build: bool = True, source_commit: str | None = None) -> bytes:
    if os.name != "nt":
        raise RuntimeError("NATIVE EXPORT ERROR: native launcher export requires Windows")
    godot_path = Path(godot)
    executable = str(godot_path.resolve()) if godot_path.is_file() else shutil.which(godot)
    if not executable:
        raise RuntimeError(f"NATIVE EXPORT ERROR: Godot executable does not exist: {godot}")
    version_result = _run([executable, "--version"], timeout=60)
    if not version_result.stdout.startswith("4.7.2."):
        raise RuntimeError(f"NATIVE EXPORT ERROR: Godot 4.7.2 is required, found {version_result.stdout.strip()}")
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="launcher-native-", dir=out))
    try:
        stage = work / "stage"
        stage.mkdir()
        if source_commit:
            archive = subprocess.run(
                ["git", "-C", str(repo), "archive", "--format=tar", source_commit, "launcher"],
                check=True, capture_output=True,
            ).stdout
            with tarfile.open(fileobj=io.BytesIO(archive)) as source:
                members = source.getmembers()
                if any(Path(member.name).is_absolute() or ".." in Path(member.name).parts
                       or not (member.isfile() or member.isdir()) for member in members):
                    raise RuntimeError("NATIVE EXPORT ERROR: pinned Git archive has an unsafe launcher path or member type")
                source.extractall(stage)
        else:
            listing = subprocess.run(
                ["git", "-C", str(repo), "ls-files", "-z", "--", "launcher"],
                check=True, capture_output=True,
            ).stdout.split(b"\0")
            for raw in listing:
                if not raw:
                    continue
                rel = Path(os.fsdecode(raw))
                src = repo / rel
                dst = stage / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
        source = source_commit or "HEAD"
        for name in ("LICENSE", "NOTICE"):
            with (stage / name).open("wb") as dst:
                subprocess.run(["git", "-C", str(repo), "show", f"{source}:{name}"], check=True,
                               stdout=dst)
        project = stage / "launcher"
        date = datetime.fromtimestamp(mtime, timezone.utc).strftime("%Y-%m-%d")
        info = {"commit": commit, "date": date, "release_key": public_key,
                "repo": repository, "test_build": test_build, "version": version}
        (project / "build_info.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        audited = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("audit_package.py")), str(project)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
        )
        if audited.returncode or "package audit ok" not in audited.stdout:
            raise RuntimeError(f"NATIVE EXPORT ERROR: staged launcher content audit failed: {audited.stdout}{audited.stderr}")
        if "PACK FILES FAIL" in audited.stdout + audited.stderr:
            raise RuntimeError(f"NATIVE EXPORT ERROR: staged launcher content audit reported PACK FILES FAIL:\n{audited.stdout}{audited.stderr}")
        _run([executable, "--headless", "--path", str(project), "--import"], ignore_failure=True, timeout=300)
        _run([executable, "--headless", "--path", str(project), "--import"], timeout=300)
        name = f"openbfme-launcher-{version}-windows-x64"
        folder = work / name
        folder.mkdir()
        exe = folder / "OpenBFMELauncher.exe"
        exported = _run([executable, "--headless", "--path", str(project), "--export-release", "Windows x64", str(exe)],
                        timeout=900)
        if "PACK FILES FAIL" in exported.stdout + exported.stderr:
            raise RuntimeError(f"NATIVE EXPORT ERROR: Godot pack-files audit failed:\n{exported.stdout}{exported.stderr}")
        if not exe.is_file() or (folder / "OpenBFMELauncher.pck").exists():
            raise RuntimeError("NATIVE EXPORT ERROR: export did not embed its pack")
        base = audit_package.embedded_pck_base(exe.read_bytes())
        if base is None:
            raise RuntimeError("NATIVE EXPORT ERROR: exported executable has no embedded pack")
        executable = exe.read_bytes()
        entries = audit_package.pck_entries(executable, base, len(executable) - 12)
        packed = dict(entries)
        expected = {"build_info.json": (project / "build_info.json").read_bytes(),
                    **{f"fonts/{f.name}": f.read_bytes() for f in sorted((project / "fonts").glob("OFL-*.txt"))}}
        got = {k: v for k, v in packed.items() if k == "build_info.json" or (k.startswith("fonts/") and k.endswith(".txt"))}
        if len(packed) != len(entries) or got != expected:
            raise RuntimeError("NATIVE EXPORT ERROR: embedded pack differs from staged build_info.json or font licences")
        for src, dst in ((stage / "LICENSE", folder / "LICENSE"), (stage / "NOTICE", folder / "NOTICE")):
            shutil.copy2(src, dst)
        readme = (project / "README_LAUNCHER.txt").read_text(encoding="utf-8")
        readme = readme.replace("@VERSION@", version).replace("@REPO@", repository).replace("@START@", "OpenBFMELauncher.exe")
        (folder / "README.txt").write_text(readme, encoding="utf-8")
        (folder / "VERSION").write_text(
            f"OpenBFME Launcher {version}\ncommit {commit}\ncommitted {date}\nupdates from {repository}\n", encoding="utf-8")
        content = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("audit_package.py")), str(folder)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
        )
        if content.returncode or "package audit ok" not in content.stdout:
            raise RuntimeError(f"NATIVE EXPORT ERROR: exported launcher content audit failed: {content.stdout}{content.stderr}")
        problems = audit_package.check_release("launcher-windows", folder)
        if problems:
            raise RuntimeError("NATIVE EXPORT ERROR: " + "; ".join(problems))
        archive = out / f"{name}.zip"
        make_archive.zip_file(folder, archive, mtime)
        problems = audit_package.check_release("launcher-windows", archive)
        if problems:
            raise RuntimeError("NATIVE EXPORT ERROR: " + "; ".join(problems))
        return archive.read_bytes()
    finally:
        shutil.rmtree(work, ignore_errors=True)
