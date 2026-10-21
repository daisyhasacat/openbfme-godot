#!/usr/bin/env python3
"""Fetch pinned Linux export tools or stage an audited game package as an AppDir."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
GODOT_VERSION = "4.7.2"
# Official GitHub release asset SHA-256 digests, verified 2026-10-20.
ASSETS = {
    "godot.zip": (
        "https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_linux.x86_64.zip",
        "cadd3204e728a35d3f13adb7fd0d7902636b79f6b95c40c265eb73b6c35329e4"),
    "templates.tpz": (
        "https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_export_templates.tpz",
        "f298490b8d44d934be425a5a65a51bf15f422428b229a06a6e11d9ffea248011"),
    "appimagetool": (
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
        "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"),
    "runtime-x86_64": (
        "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
        "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"),
}

APPRUN = '''#!/bin/sh
set -eu
appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$appdir/usr/lib/openbfme"
# The launcher cannot update a read-only mount. Settings uses APPIMAGE to replace
# the outer image through an external helper instead.
export OPENBFME_NO_UPDATE_CHECK=1
exec ./OpenBFME.x86_64 "$@"
'''
DESKTOP = '''[Desktop Entry]
Type=Application
Name=OpenBFME
Comment=Real-time strategy using your own Battle for Middle-earth game files
Exec=AppRun
Icon=openbfme
Terminal=false
Categories=Game;StrategyGame;
'''


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch_asset(directory: Path, name: str, url: str, expected: str) -> Path:
    destination = directory / name
    if destination.is_file() and digest(destination) == expected:
        return destination
    temporary = destination.with_suffix(destination.suffix + ".part")
    subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error", "--retry", "3",
                    "--output", str(temporary), url], check=True)
    if digest(temporary) != expected:
        temporary.unlink()
        raise ValueError(f"SHA-256 mismatch for {name}; refusing downloaded tool")
    temporary.replace(destination)
    return destination


def fetch_tools(directory: Path, templates: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for name, (url, expected) in ASSETS.items():
        fetch_asset(directory, name, url, expected)
    # Extract named members only; no archive paths become output paths.
    with zipfile.ZipFile(directory / "godot.zip") as archive:
        (directory / "godot").write_bytes(archive.read(f"Godot_v{GODOT_VERSION}-stable_linux.x86_64"))
    templates.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(directory / "templates.tpz") as archive:
        version = archive.read("templates/version.txt").decode().strip()
        if version != f"{GODOT_VERSION}.stable":
            raise ValueError(f"Unexpected export template version: {version}")
        for name in ["linux_debug.x86_64", "linux_release.x86_64"]:
            (templates / name).write_bytes(archive.read(f"templates/{name}"))
            (templates / name).chmod(0o755)
        (templates / "version.txt").write_text(version + "\n")
    for name in ["godot", "appimagetool", "runtime-x86_64"]:
        (directory / name).chmod(0o755)


def stage(package: Path, output: Path) -> None:
    # Reuse the release allowlist/content gate before adding AppImage metadata.
    subprocess.run(["python3", str(ROOT / "tools/release/audit_package.py"),
                    "--release", "game-linux", str(package)], check=True)
    if output.exists():
        raise ValueError(f"AppDir already exists: {output}")
    payload = output / "usr/lib/openbfme"
    shutil.copytree(package, payload)
    (payload / "OpenBFME.x86_64").chmod(0o755)
    (output / "AppRun").write_text(APPRUN)
    (output / "AppRun").chmod(0o755)
    (output / "openbfme.desktop").write_text(DESKTOP)
    shutil.copyfile(ROOT / "godot/assets/openbfme-icon.png", output / "openbfme.png")
    (output / ".DirIcon").symlink_to("openbfme.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    fetch = commands.add_parser("fetch-tools")
    fetch.add_argument("--directory", type=Path, required=True)
    fetch.add_argument("--templates", type=Path, required=True)
    appdir = commands.add_parser("stage")
    appdir.add_argument("--package", type=Path, required=True)
    appdir.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "fetch-tools":
        fetch_tools(args.directory, args.templates)
    else:
        stage(args.package, args.output)


if __name__ == "__main__":
    main()
