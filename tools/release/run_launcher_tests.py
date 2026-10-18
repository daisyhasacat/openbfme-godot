"""Contributor entry point for the native launcher test gate."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GODOT_VERSION = "4.7.2"


def fail(message: str) -> int:
    print(f"PREREQUISITE ERROR: {message}", file=sys.stderr)
    return 2


def main() -> int:
    if os.name != "nt":
        return fail("run this command from Windows; Linux contributors should use the existing pytest commands")
    godot = Path(os.environ.get("GODOT", ""))
    if not godot.is_file():
        return fail("Godot console executable is missing; set GODOT to Godot_v4.7.2-stable_win64_console.exe")
    version = subprocess.run([str(godot), "--version"], capture_output=True, text=True)
    if version.returncode or not version.stdout.startswith(f"{GODOT_VERSION}."):
        return fail(f"Godot {GODOT_VERSION} is required; {godot} reported {version.stdout.strip() or version.stderr.strip()}")
    for module in ("pytest", "pefile"):
        try:
            __import__(module)
        except ImportError:
            return fail(f"Python dependency {module} is missing; install it in the active contributor venv")
    if not shutil.which("cl.exe"):
        return fail("MSVC is missing; run from the VS 2022 x64 Native Tools Developer Command Prompt")
    appdata = Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming"))
    template = appdata / "Godot" / "export_templates" / f"{GODOT_VERSION}.stable" / "windows_release_x86_64.exe"
    if not template.is_file():
        return fail(f"official Godot export template is missing by name: {template}")
    env = dict(os.environ, GODOT=str(godot))
    command = [sys.executable, "-m", "pytest", "tools/release/test_launcher.py",
               "tools/release/test_onezip.py", "tools/release/test_launcher_tools.py"]
    result = subprocess.run(command, cwd=ROOT, env=env)
    if result.returncode:
        return result.returncode
    from launcher_test_support import native_launcher_fake_game
    fake_game = native_launcher_fake_game()
    workspace = ROOT / "workspace"
    workspace.mkdir(exist_ok=True)
    return subprocess.run([sys.executable, "tools/release/onezip_windows_test.py",
                           "--work", str(workspace / "launcher native path"),
                           "--native", "--fake-game", str(fake_game),
                           "--published", str(workspace / "published-empty"),
                           "--tags", "none"],
                          cwd=ROOT, env=env).returncode


if __name__ == "__main__":
    raise SystemExit(main())
