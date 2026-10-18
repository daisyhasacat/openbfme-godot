from __future__ import annotations

import hashlib
import json
import os
import plistlib
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import make_archive  # noqa: E402
import make_macos_manifest  # noqa: E402
import ed25519  # noqa: E402
import test_macos_release as macos_release  # noqa: E402
import package_macos_preview  # noqa: E402
import seal_package  # noqa: E402
from launcher_test_server import ReleaseServer  # noqa: E402

VERSION = "v1.2.3-preview.1"
NEXT_VERSION = "v1.2.4-preview.1"
COMMIT = "0123456789abcdef0123456789abcdef01234567"
DATE = "2026-10-16"
SWAP_SOURCE = HERE / "macos_bundle_swap.c"
# MACPLAY-1: the throwaway key of these tests' launchers; the test packages are sealed with it (a Mac launcher offers the package beside
# it only when its signed payload inventory verifies)
TEST_SECRET = bytes(range(32))
REPO = HERE.parents[1]


@pytest.fixture(scope="module")
def native_swap_helper(tmp_path_factory):
    if sys.platform != "darwin":
        pytest.skip("Darwin renameatx_np helper is macOS-only")
    binary = tmp_path_factory.mktemp("macos-swap") / "openbfme-bundle-swap"
    subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", str(SWAP_SOURCE), "-o", str(binary)], check=True)
    return binary


def _export_test_launcher(
    tmp: Path, godot: Path, helper: Path, version: str, public_key: bytes, godot_home: Path,
) -> Path:
    project = tmp / f"launcher-project-{version}"
    shutil.copytree(REPO / "launcher", project)
    presets = project / "export_presets.cfg"
    presets.write_text(
        package_macos_preview.arm64_launcher_preset_text(presets.read_text(encoding="utf-8")),
        encoding="utf-8",
    )
    (project / "build_info.json").write_text(json.dumps({
        "version": version,
        "display_version": version,
        "commit": COMMIT,
        "date": DATE,
        "repo": "Test/repo",
        "release_key": public_key.hex(),
        "test_build": True,
        "local_trial": True,
        "macos_preview": False,
        "macos_updates": True,
    }, sort_keys=True) + "\n", encoding="utf-8")
    app = tmp / f"launcher-{version}" / "OpenBFME Launcher.app"
    app.parent.mkdir(parents=True)
    result = subprocess.run(
        ["arch", "-arm64", str(godot), "--headless", "--path", str(project), "--export-release",
         package_macos_preview.LAUNCHER_PRESET, str(app)],
        capture_output=True, text=True, timeout=300,
        env={**os.environ, "HOME": str(godot_home)},
    )
    assert result.returncode == 0, result.stdout[-3000:] + result.stderr[-3000:]
    bundle_helper = app / "Contents/MacOS/openbfme-bundle-swap"
    shutil.copyfile(helper, bundle_helper)
    bundle_helper.chmod(0o755)
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True, capture_output=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True, capture_output=True)
    return app


@pytest.fixture(scope="module")
def exported_macos_launchers(tmp_path_factory):
    if sys.platform != "darwin":
        pytest.skip("the exported macOS launcher flow requires Darwin")
    godot = Path(os.environ.get("GODOT", "/Users/devin/Applications/Godot-4.7.2/Godot.app/Contents/MacOS/Godot"))
    if not godot.is_file():
        pytest.skip("set GODOT to the installed Godot 4.7.2 editor")
    tmp = tmp_path_factory.mktemp("macos-launcher-e2e")
    helper = tmp / "openbfme-bundle-swap"
    subprocess.run(
        ["clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-Wall", "-Wextra", "-Werror",
         str(SWAP_SOURCE), "-o", str(helper)],
        check=True,
    )
    godot_home = tmp / "godot-home"
    template_dir = godot_home / "Library/Application Support/Godot/export_templates/4.7.2.stable"
    template_dir.parent.mkdir(parents=True)
    template_archive = Path(os.environ.get("OPENBFME_MAC_TEMPLATES",
                                            REPO / "workspace/mac3-tools/Godot_v4.7.2-stable_export_templates.tpz"))
    if not template_archive.is_file():
        pytest.skip(f"SKIPPED LOUDLY: no Godot 4.7.2 export templates at {template_archive} (set OPENBFME_MAC_TEMPLATES)")
    package_macos_preview.install_templates(template_archive, template_dir, sanitize_paths=True)
    secret = TEST_SECRET
    public = ed25519.public_key(secret)
    a = _export_test_launcher(tmp, godot, helper, "v1.2.2", public, godot_home)
    b = _export_test_launcher(tmp, godot, helper, VERSION, public, godot_home)
    wrong_version = _export_test_launcher(tmp, godot, helper, "v1.2.4-preview.1", public, godot_home)
    yield {"a": a, "b": b, "wrong_version": wrong_version, "secret": secret, "public": public, "helper": helper}
    base = str(tmp_path_factory.getbasetemp().resolve())
    processes = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True, check=True)
    for line in processes.stdout.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) == 2 and fields[1].startswith(base + "/"):
            try:
                os.kill(int(fields[0]), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass


def _test_game_app(launcher_app: Path, destination: Path) -> None:
    shutil.copytree(launcher_app, destination)
    contents = destination / "Contents"
    executable = contents / "MacOS"
    (executable / package_macos_preview.LAUNCHER_EXECUTABLE).rename(executable / "OpenBFME")
    (executable / "openbfme-bundle-swap").unlink()
    resources = contents / "Resources"
    (resources / "OpenBFME Launcher.pck").rename(resources / "OpenBFME.pck")
    info_path = contents / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info["CFBundleExecutable"] = "OpenBFME"
    info_path.write_bytes(plistlib.dumps(info))
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(destination)], check=True, capture_output=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(destination)], check=True, capture_output=True)


def _test_package(root: Path, launcher_app: Path, version: str, epoch: int = 1_700_000_000) -> Path:
    root.parent.mkdir(parents=True, exist_ok=True)
    root.mkdir()
    shutil.copytree(launcher_app, root / "OpenBFME Launcher.app")
    _test_game_app(launcher_app, root / "OpenBFME.app")
    (root / "README-MAC.txt").write_text("Test-only signed Mac update fixture.\n", encoding="utf-8")
    (root / "LICENSE").write_text("Test fixture.\n", encoding="utf-8")
    (root / "NOTICE").write_text("Test fixture.\n", encoding="utf-8")
    (root / "VERSION").write_text(f"OpenBFME {version}\ncommit {COMMIT}\ncommitted {DATE}\n", encoding="utf-8")
    seal_package.seal_folder(root, TEST_SECRET, "Test/repo", version, "macos-arm64")
    archive = root.parent / f"{root.name}.zip"
    make_archive.zip_file(root, archive, epoch)
    return archive


def _test_signed_sidecars(archive: Path | None, secret: bytes, version: str) -> dict[str, bytes]:
    if archive is None:
        manifest = make_macos_manifest.build(version, "Test/repo", COMMIT, DATE, no_entry=True)
        files = {}
    else:
        data = archive.read_bytes()
        top, contents, problems = macos_release.audit_package.read_release_zip(data, archive.name, nested=True)
        assert not problems and top == archive.stem
        asset = {
            "name": archive.name,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "files": {name: {"size": len(body), "sha256": hashlib.sha256(body).hexdigest()}
                      for name, body in sorted(contents.items())},
        }
        manifest = {
            "format": 3, "product": "OpenBFME", "repo": "Test/repo", "version": version,
            "channel": "preview" if "-" in version else "stable", "date": DATE, "commit": COMMIT,
            "assets": {"game": {"macos-arm64": asset}},
        }
        files = {archive.name: data}
    payload = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode()
    return {"manifest-macos.json": payload, "manifest-macos.json.sig": ed25519.sign(secret, payload), **files}


def _run_exported(app: Path, home: Path, server: ReleaseServer, *args: str, env_extra: dict[str, str] | None = None):
    env = os.environ.copy()
    env.update({
        "HOME": str(home),
        "OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS": "30000",
        "OPENBFME_TEST_MACOS_CONFIRM_EXIT": "1",
        "OPENBFME_TEST_LAUNCHER_OPTIONS": f"--api-base={server.origin} --repo=Test/repo",
    })
    if env_extra:
        env.update(env_extra)
    command = [str(app / "Contents/MacOS/OpenBFME Launcher"), "--headless", "--",
               "--api-base=" + server.origin, "--repo=Test/repo", *args]
    stdout_path = home / f"launcher-{time.monotonic_ns()}.stdout"
    stderr_path = stdout_path.with_suffix(".stderr")
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
        result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=90, env=env)
    return subprocess.CompletedProcess(
        command, result.returncode, stdout_path.read_text(encoding="utf-8", errors="replace"),
        stderr_path.read_text(encoding="utf-8", errors="replace"),
    )


def _run_exported_until_ui(
    app: Path, home: Path, server: ReleaseServer, expected: str, *args: str, timeout: float = 30,
) -> str:
    env = os.environ.copy()
    env.update({
        "HOME": str(home),
        "OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS": "30000",
        "OPENBFME_TEST_LAUNCHER_OPTIONS": f"--api-base={server.origin} --repo=Test/repo",
    })
    command = [str(app / "Contents/MacOS/OpenBFME Launcher"), "--headless", "--",
               "--api-base=" + server.origin, "--repo=Test/repo", *args, "--ui-keep-open"]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    log_path = _app_data(home) / "launcher.log"
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            try:
                log = log_path.read_text(encoding="utf-8")
                if expected in log:
                    return log
            except OSError:
                pass
            if process.poll() is not None:
                break
            time.sleep(0.05)
        log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
        raise AssertionError(f"launcher did not report UI state {expected!r}: {log}")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def _file_inventory(root: Path) -> dict[str, dict[str, int | str]]:
    return {
        path.relative_to(root).as_posix(): {
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
        for path in root.rglob("*")
        if path.is_file()
    }


def _wait_for_receipt(home: Path, version: str, timeout: float = 30) -> Path | None:
    directory = _app_data(home)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for path in directory.glob("macos-update-receipt-*.json"):
            try:
                if json.loads(path.read_text(encoding="utf-8")).get("version") == version:
                    return path
            except (OSError, json.JSONDecodeError):
                pass
        time.sleep(0.1)
    return None


def _wait_for_rejected_version(home: Path, version: str, timeout: float = 30) -> Path | None:
    directory = _app_data(home)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for path in directory.glob("macos-update-rejected-*.json"):
            try:
                state = json.loads(path.read_text(encoding="utf-8"))
                if state.get("version") == version and state.get("attempts", 0) > 0:
                    return path
            except (OSError, json.JSONDecodeError):
                pass
        time.sleep(0.1)
    return None


def _wait_for_log_count(home: Path, text: str, count: int, timeout: float = 10) -> bool:
    path = _app_data(home) / "launcher.log"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if path.read_text(encoding="utf-8").count(text) >= count:
                return True
        except OSError:
            pass
        time.sleep(0.1)
    return False


def _wait_for_process_exit(helper: Path, pid: int, timeout: float = 10) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = subprocess.run([helper, "--pid-alive", str(pid)], check=False)
        if result.returncode == 1:
            return True
        time.sleep(0.05)
    return False


def _app_data(home: Path) -> Path:
    return home / "Library/Application Support/OpenBFMELauncher"


def _installed_root(base: Path, launcher_app: Path) -> Path:
    root = base / "openbfme-v1.2.2-macos-arm64"
    archive = _test_package(root, launcher_app, "v1.2.2")
    extracted = root.parent / (root.name + "-extracted")
    make_archive.extract_all(archive, extracted)
    shutil.rmtree(root)
    extracted.rename(root)
    archive.unlink()
    return root


def _wait_for_version(root: Path, version: str, timeout: float = 30) -> bool:
    import time

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if (root / "VERSION").read_text(encoding="utf-8").split()[1] == version:
                return True
        except (OSError, IndexError):
            pass
        time.sleep(0.25)
    return False


def test_exported_launcher_downloads_swaps_confirms_and_revalidates_receipt(exported_macos_launchers, tmp_path: Path):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        initial = _run_exported_until_ui(
            root / "OpenBFME Launcher.app", home, server,
            "LAUNCHER ui screen: play",
            "--offline",
            "--screenshot=" + str(tmp_path / "initial.png"),
        )
        # MACPLAY-1: the sealed package verifies: no package notice, and Play starts it
        assert "Mac package notice" not in initial and "the game beside the launcher is not offered" not in initial, initial
        package_b = _test_package(
            tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION,
        )
        sidecars_b = _test_signed_sidecars(package_b, exported_macos_launchers["secret"], VERSION)
        server.publish(VERSION, sidecars_b)
        staged = _run_exported(
            root / "OpenBFME Launcher.app", home, server, "--update", "--channel=preview",
        )
        assert staged.returncode == 0 and VERSION in staged.stdout + staged.stderr, staged.stdout + staged.stderr
        apply = _run_exported(
            root / "OpenBFME Launcher.app", home, server, "--version",
            env_extra={"OPENBFME_TEST_MACOS_KILL_AT": "after-candidate-removal"},
        )
        assert apply.returncode == 0, apply.stdout + apply.stderr
        assert _wait_for_version(root, VERSION), "the exported B launcher did not confirm the staged transaction"
        expected_b = json.loads(sidecars_b["manifest-macos.json"])["assets"]["game"]["macos-arm64"]["files"]
        assert _file_inventory(root) == expected_b
        for app_name in ("OpenBFME.app", "OpenBFME Launcher.app"):
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(root / app_name)], check=True)
        receipt_b = _wait_for_receipt(home, VERSION)
        assert receipt_b is not None, "the confirmed B launcher did not persist its signed receipt"
        journal_path = _app_data(home) / "macos-update.json"
        deadline = time.monotonic() + 20
        journal = {}
        while time.monotonic() < deadline:
            try:
                journal = json.loads(journal_path.read_text(encoding="utf-8"))
                if journal.get("state") == "cleaning-confirmed":
                    break
            except (OSError, json.JSONDecodeError):
                pass
            time.sleep(0.1)
        assert journal.get("state") == "cleaning-confirmed"
        assert _wait_for_process_exit(exported_macos_launchers["helper"], journal["apply_pid"])

        package_c = _test_package(
            tmp_path / f"openbfme-{NEXT_VERSION}-macos-arm64",
            exported_macos_launchers["wrong_version"],
            NEXT_VERSION,
        )
        sidecars_c = _test_signed_sidecars(package_c, exported_macos_launchers["secret"], NEXT_VERSION)
        server.publish(NEXT_VERSION, sidecars_c)
        staged_c = _run_exported(
            root / "OpenBFME Launcher.app", home, server, "--update", "--channel=preview",
        )
        staged_output = staged_c.stdout + staged_c.stderr
        assert staged_c.returncode == 0 and NEXT_VERSION in staged_output, staged_output
        assert "previous external update helper" not in staged_output
        apply_c = _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert apply_c.returncode == 0, apply_c.stdout + apply_c.stderr
        assert _wait_for_version(root, NEXT_VERSION), "the exported C launcher did not confirm the second transaction"
        expected_c = json.loads(sidecars_c["manifest-macos.json"])["assets"]["game"]["macos-arm64"]["files"]
        assert _file_inventory(root) == expected_c
        for app_name in ("OpenBFME.app", "OpenBFME Launcher.app"):
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(root / app_name)], check=True)
        receipt_c = _wait_for_receipt(home, NEXT_VERSION)
        assert receipt_c is not None, "the confirmed C launcher did not persist its signed receipt"
        assert receipt_b.is_file(), "the B transaction receipt was unexpectedly removed"
        if journal_path.exists():
            journal = json.loads(journal_path.read_text(encoding="utf-8"))
            assert _wait_for_process_exit(exported_macos_launchers["helper"], journal["apply_pid"])
            cleaned = _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
            assert cleaned.returncode == 0, cleaned.stdout + cleaned.stderr
        assert not journal_path.exists()
        helper_root = _app_data(home) / "macos-update/helper"
        assert not helper_root.exists() or not any(helper_root.iterdir())
        zip_requests = [path for path, _headers in server.requests if path.endswith(".zip")]
        expected_names = {
            f"openbfme-{VERSION}-macos-arm64.zip",
            f"openbfme-{NEXT_VERSION}-macos-arm64.zip",
        }
        assert zip_requests and all(Path(path).name in expected_names for path in zip_requests)
        assert {Path(path).name for path in zip_requests} == expected_names
        receipt_c.unlink()
        failed_closed = _run_exported(root / "OpenBFME Launcher.app", home, server, "--play")
        assert failed_closed.returncode != 0
        assert "receipt" in failed_closed.stdout.lower() + failed_closed.stderr.lower()
    finally:
        server.stop()


@pytest.mark.parametrize("bad_signature", [False, True], ids=["authenticated-no-entry", "invalid-present-sidecar"])
def test_exported_launcher_does_not_downgrade_sidecar_or_fetch_other_platform(
    exported_macos_launchers, tmp_path: Path, bad_signature: bool,
):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        files = _test_signed_sidecars(None, exported_macos_launchers["secret"], VERSION)
        if bad_signature:
            files["manifest-macos.json.sig"] = b"\x00" * 64
        server.publish(VERSION, files)
        if bad_signature:
            checked = _run_exported(root / "OpenBFME Launcher.app", home, server, "--update", "--channel=preview")
        else:
            selected = _run_exported(root / "OpenBFME Launcher.app", home, server, "--channel=preview")
            assert selected.returncode == 0
            checked = _run_exported_until_ui(
                root / "OpenBFME Launcher.app", home, server,
                "LAUNCHER ui releases link: shown",
                "--screenshot=" + str(tmp_path / "no-entry.png"),
            )
            output = checked
        if bad_signature:
            output = checked.stdout + checked.stderr
            assert checked.returncode in (0, 3), output
        paths = [path for path, _headers in server.requests]
        assert not any(path.endswith(".zip") for path in paths)
        if bad_signature:
            assert "rejected" in output.lower() or "signature" in output.lower()
        else:
            assert "no signed Mac package" in output or "releases" in output.lower()
            assert "LAUNCHER ui releases link: shown" in output
    finally:
        server.stop()


@pytest.mark.parametrize("crash_at", ["before-confirm", "after-exchange"], ids=["rollback-no-confirm", "recover-interrupted-exchange"])
def test_exported_launcher_rolls_back_or_recovers_after_real_process_kill(
    exported_macos_launchers, tmp_path: Path, crash_at: str,
):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        package = _test_package(tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION)
        server.publish(VERSION, _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION))
        staged = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr
        _run_exported(
            root / "OpenBFME Launcher.app", home, server, "--version",
            env_extra={
                "OPENBFME_TEST_MACOS_KILL_AT": crash_at,
                "OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS": "1500" if crash_at == "before-confirm" else "30000",
            },
        )
        if crash_at == "before-confirm":
            assert _wait_for_rejected_version(home, VERSION), "failed B startup was not recorded as rejected"
            assert _wait_for_version(root, "v1.2.2")
            assert _wait_for_log_count(home, "LAUNCHER OpenBFME Launcher v1.2.2", 3), \
                "launcher A was not relaunched after rollback"
        else:
            assert _wait_for_version(root, VERSION, timeout=35), f"transaction did not reach {VERSION}"
            journal = json.loads((_app_data(home) / "macos-update.json").read_text(encoding="utf-8"))
            assert _wait_for_process_exit(exported_macos_launchers["helper"], journal["apply_pid"])
            restarted = _run_exported(
                root / "OpenBFME Launcher.app", home, server, "--screenshot=" + str(tmp_path / "recovered.png"),
            )
            assert restarted.returncode == 0
            assert _wait_for_receipt(home, VERSION) is not None
    finally:
        server.stop()


def test_exported_launcher_rejects_tampered_archive_and_preserves_a(exported_macos_launchers, tmp_path: Path):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        package = _test_package(tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION)
        files = _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION)
        zip_name = package.name
        files[zip_name] = files[zip_name] + b"tampered"
        server.publish(VERSION, files)
        before = _file_inventory(root)
        rejected = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert rejected.returncode != 0 or "rejected" in rejected.stdout.lower() + rejected.stderr.lower()
        assert _file_inventory(root) == before
    finally:
        server.stop()


def test_exported_launcher_rejects_a_signed_package_with_wrong_launcher_version(
    exported_macos_launchers, tmp_path: Path,
):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        before = _file_inventory(root)
        package = _test_package(
            tmp_path / f"openbfme-{VERSION}-macos-arm64",
            exported_macos_launchers["wrong_version"], VERSION,
        )
        server.publish(VERSION, _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION))
        staged = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr
        _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert _wait_for_rejected_version(home, VERSION), "mismatched B launcher was not rejected"
        assert _file_inventory(root) == before
        log = (_app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert "launched version v1.2.4-preview.1 does not match staged version " + VERSION in log
        assert _wait_for_log_count(home, "LAUNCHER OpenBFME Launcher v1.2.2", 3), \
            "launcher A was not relaunched after rejecting B"
    finally:
        server.stop()


def test_exported_launcher_refuses_staged_tamper_without_replacing_a(exported_macos_launchers, tmp_path: Path):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        package = _test_package(tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION)
        server.publish(VERSION, _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION))
        staged = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr
        before = _file_inventory(root)
        candidate = next(root.parent.glob(".openbfme-update-*"))
        (candidate / "VERSION").write_text("OpenBFME v9.9.9\n", encoding="utf-8")
        refused = _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert _file_inventory(root) == before
        assert "inventory" in refused.stdout.lower() + refused.stderr.lower() or "unexpected" in refused.stdout.lower() + refused.stderr.lower()
    finally:
        server.stop()


def test_exported_launcher_authenticates_external_helper_and_corrupt_journal_before_play(
    exported_macos_launchers, tmp_path: Path,
):
    server = ReleaseServer()
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        before = _file_inventory(root)
        package = _test_package(tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION)
        server.publish(VERSION, _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION))
        staged = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr
        journal_path = _app_data(home) / "macos-update.json"
        journal = json.loads(journal_path.read_text(encoding="utf-8"))
        helper_app = Path(journal["helper_launcher"]).parents[2]
        pcks = list(helper_app.rglob("*.pck"))
        assert len(pcks) == 1
        pcks[0].write_bytes(pcks[0].read_bytes() + b"OPENBFME-TAMPERED-HELPER-MARKER")
        launch_marker = tmp_path / "helper-launched"
        refused = _run_exported(
            root / "OpenBFME Launcher.app", home, server, "--version",
            env_extra={"OPENBFME_TEST_MACOS_HELPER_LAUNCH_MARKER": str(launch_marker)},
        )
        output = refused.stdout.lower() + refused.stderr.lower()
        assert not launch_marker.exists()
        assert "untrusted" in output or "helper" in output or "authenticated" in output
        assert _file_inventory(root) == before
        blocked = _run_exported(root / "OpenBFME Launcher.app", home, server, "--play")
        assert blocked.returncode != 0
        assert _file_inventory(root) == before

        journal_path.write_text("{", encoding="utf-8")
        corrupt = _run_exported(root / "OpenBFME Launcher.app", home, server, "--play")
        corrupt_output = corrupt.stdout.lower() + corrupt.stderr.lower()
        assert corrupt.returncode != 0
        assert "journal" in corrupt_output or "corrupt" in corrupt_output
        assert _file_inventory(root) == before
    finally:
        server.stop()


def test_exported_launcher_defers_exchange_while_game_process_runs(exported_macos_launchers, tmp_path: Path):
    server = ReleaseServer()
    game = None
    try:
        home = tmp_path / "home"
        home.mkdir()
        root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
        game_exe = root / "OpenBFME.app/Contents/MacOS/OpenBFME"
        shutil.copyfile("/bin/sleep", game_exe)
        game_exe.chmod(0o755)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(root / "OpenBFME.app")],
                       check=True, capture_output=True)
        # MACPLAY-1: the installed package with this stand-in game is a signed one (sealed again): a previous package is trusted only
        # through signed data
        seal_package.seal_folder(root, TEST_SECRET, "Test/repo", "v1.2.2", "macos-arm64")
        package = _test_package(tmp_path / f"openbfme-{VERSION}-macos-arm64", exported_macos_launchers["b"], VERSION)
        sidecars = _test_signed_sidecars(package, exported_macos_launchers["secret"], VERSION)
        server.publish(VERSION, sidecars)
        staged = _run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr
        game = subprocess.Popen([str(game_exe), "60"])
        deferred = _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        deadline = time.monotonic() + 10
        journal = {}
        while time.monotonic() < deadline:
            try:
                journal = json.loads((_app_data(home) / "macos-update.json").read_text(encoding="utf-8"))
                if journal.get("state") == "deferred":
                    break
            except (OSError, json.JSONDecodeError):
                pass
            time.sleep(0.1)
        assert journal.get("state") == "deferred", deferred.stdout + deferred.stderr
        assert (root / "VERSION").read_text(encoding="utf-8").startswith("OpenBFME v1.2.2")
        game.terminate()
        game.wait(timeout=10)
        game = None
        resumed = _run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert resumed.returncode == 0, resumed.stdout + resumed.stderr
        assert _wait_for_version(root, VERSION), "the deferred update did not resume after the game exited"
        assert _wait_for_receipt(home, VERSION) is not None
        expected = json.loads(sidecars["manifest-macos.json"])["assets"]["game"]["macos-arm64"]["files"]
        assert _file_inventory(root) == expected
        for app_name in ("OpenBFME.app", "OpenBFME Launcher.app"):
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(root / app_name)], check=True)
    finally:
        if game is not None and game.poll() is None:
            game.kill()
            game.wait(timeout=10)
        server.stop()


def test_exported_launcher_leaves_live_owner_transaction_untouched(exported_macos_launchers, tmp_path: Path):
    server = ReleaseServer()
    home = tmp_path / "home"
    home.mkdir()
    root = _installed_root(tmp_path / "install", exported_macos_launchers["a"])
    launcher = root / "OpenBFME Launcher.app"
    helper = launcher / "Contents/MacOS/openbfme-bundle-swap"
    lock = _app_data(home) / "macos-update.lock"
    token = "live-owner-test"
    try:
        initialized = _run_exported(launcher, home, server, "--version")
        assert initialized.returncode == 0, initialized.stdout + initialized.stderr
        before = _file_inventory(root)
        subprocess.run([helper, "--lock-acquire", lock, str(os.getpid()), token], check=True)
        blocked = _run_exported(launcher, home, server, "--play")
        output = blocked.stdout.lower() + blocked.stderr.lower()
        assert blocked.returncode != 0 and "in progress" in output
        assert _file_inventory(root) == before
        status = subprocess.run([helper, "--lock-status", lock], check=False, capture_output=True, text=True)
        assert status.returncode == 3 and status.stdout.split()[-1] == token
    finally:
        subprocess.run([helper, "--lock-release", lock, str(os.getpid()), token], check=False)
        server.stop()


def test_generate_signed_contract_inventory_from_audited_zip(tmp_path: Path):
    root, files = macos_release.generic_package(tmp_path, VERSION)
    archive = tmp_path / f"{root.name}.zip"
    make_archive.zip_file(root, archive, 1_700_000_000)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = tmp_path / f"{archive.name}.sha256"
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii")

    manifest = make_macos_manifest.build(
        VERSION, "Test/repo", COMMIT, DATE, archive=archive, checksum=checksum,
    )

    asset = manifest["assets"]["game"]["macos-arm64"]
    assert set(manifest) == {"format", "product", "repo", "version", "channel", "date", "commit", "assets"}
    assert manifest["format"] == 3
    assert manifest["assets"].keys() == {"game"}
    assert asset["name"] == archive.name
    assert asset["size"] == archive.stat().st_size
    assert asset["sha256"] == digest
    assert {name: item["size"] for name, item in asset["files"].items()} == {
        name: len(content) for name, content in files.items()
    }


def test_authenticated_no_entry_manifest_has_only_empty_game_object():
    manifest = make_macos_manifest.build(VERSION, "Test/repo", COMMIT, DATE, no_entry=True)
    assert manifest["assets"] == {"game": {}}


def test_throwaway_key_signs_and_rejects_changed_macos_sidecar(tmp_path: Path):
    secret = os.urandom(32)
    manifest_path = tmp_path / "manifest-macos.json"
    manifest_path.write_text(json.dumps(
        make_macos_manifest.build(VERSION, "Test/repo", COMMIT, DATE, no_entry=True),
        sort_keys=True, indent=2,
    ) + "\n", encoding="utf-8")
    signature = ed25519.sign(secret, manifest_path.read_bytes())
    signature_path = tmp_path / "manifest-macos.json.sig"
    signature_path.write_bytes(signature)
    public = ed25519.public_key(secret)
    assert len(signature) == 64
    assert ed25519.verify(public, manifest_path.read_bytes(), signature)
    assert not ed25519.verify(public, manifest_path.read_bytes() + b" ", signature)
    assert signature_path.stat().st_size == 64


def test_manifest_generator_rejects_bad_checksum_and_unsafe_versions(tmp_path: Path):
    root, _ = macos_release.generic_package(tmp_path, VERSION)
    archive = tmp_path / f"{root.name}.zip"
    make_archive.zip_file(root, archive, 1_700_000_000)
    checksum = tmp_path / f"{archive.name}.sha256"
    checksum.write_text("0" * 64 + f"  {archive.name}\n", encoding="ascii")
    with pytest.raises(SystemExit, match="does not exactly match"):
        make_macos_manifest.build(VERSION, "Test/repo", COMMIT, DATE, archive=archive, checksum=checksum)
    with pytest.raises(SystemExit, match="release version"):
        make_macos_manifest.build("../unsafe", "Test/repo", COMMIT, DATE, no_entry=True)


def test_native_swap_exchanges_sibling_roots_and_can_exchange_back(native_swap_helper: Path, tmp_path: Path):
    root = tmp_path / "openbfme-v1.2.3-macos-arm64"
    candidate = tmp_path / ".openbfme-update-test"
    for directory in [root, candidate]:
        (directory / "OpenBFME.app").mkdir(parents=True)
        (directory / "OpenBFME Launcher.app").mkdir()
    (root / "VERSION").write_text("A\n")
    (candidate / "VERSION").write_text("B\n")

    subprocess.run([native_swap_helper, root, candidate], check=True)
    assert (root / "VERSION").read_text() == "B\n"
    assert (candidate / "VERSION").read_text() == "A\n"
    subprocess.run([native_swap_helper, root, candidate], check=True)
    assert (root / "VERSION").read_text() == "A\n"
    assert (candidate / "VERSION").read_text() == "B\n"


def test_native_swap_rejects_symlinks_and_unsafe_paths(native_swap_helper: Path, tmp_path: Path):
    root = tmp_path / "openbfme-v1.2.3-macos-arm64"
    candidate = tmp_path / ".openbfme-update-test"
    for directory in [root, candidate]:
        (directory / "OpenBFME.app").mkdir(parents=True)
        (directory / "OpenBFME Launcher.app").mkdir()
    link = tmp_path / ".openbfme-update-linked"
    link.symlink_to(candidate, target_is_directory=True)
    unsafe = subprocess.run([native_swap_helper, root, link], check=False, capture_output=True, text=True)
    assert unsafe.returncode != 0
    unrelated = tmp_path / "unrelated"
    for directory in [unrelated, unrelated / "OpenBFME.app", unrelated / "OpenBFME Launcher.app"]:
        directory.mkdir()
    outside = subprocess.run([native_swap_helper, unrelated, candidate], check=False, capture_output=True, text=True)
    assert outside.returncode != 0
    relative = subprocess.run([native_swap_helper, "installed", str(candidate)], check=False, capture_output=True, text=True)
    assert relative.returncode != 0
    same = subprocess.run([native_swap_helper, root, root], check=False, capture_output=True, text=True)
    assert same.returncode != 0
    assert root.is_dir() and candidate.is_dir()


# MACPLAY-1 (Sol r1): the sync test only checked the exit code, so a helper without its fsync calls passed. fsync is interposed (dyld
# __interpose, the helper is not hardened) and every call logged with its file's path.
FSYNC_LOG_SOURCE = r"""
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
static int logged_fsync(int fd) {
    char path[MAXPATHLEN] = "?";
    fcntl(fd, F_GETPATH, path);
    const char *log = getenv("OPENBFME_TEST_FSYNC_LOG");
    FILE *f = log ? fopen(log, "a") : NULL;
    if (f) { fprintf(f, "fsync %s\n", path); fclose(f); }
    return fsync(fd);
}
__attribute__((used)) static struct { const void *replacement; const void *replacee; } interposers[]
    __attribute__((section("__DATA,__interpose"))) = { { (const void *)logged_fsync, (const void *)fsync } };
"""


def _fsyncs(helper: Path, dylib: Path, tmp: Path, *args: str) -> list[str]:
    log = tmp / f"fsync-{time.monotonic_ns()}.log"
    subprocess.run([helper, *args], check=True, env=dict(os.environ, DYLD_INSERT_LIBRARIES=str(dylib), OPENBFME_TEST_FSYNC_LOG=str(log)))
    return log.read_text(encoding="utf-8").splitlines() if log.exists() else []


def test_native_helper_durably_syncs_a_journal_file(native_swap_helper: Path, tmp_path: Path):
    """--sync fsyncs the journal file and its folder; --sync-parent fsyncs the folder (each call seen through an fsync interposer); a
    helper built without those calls is caught by the same check"""
    dylib = tmp_path / "libfsynclog.dylib"
    source = tmp_path / "fsynclog.c"
    source.write_text(FSYNC_LOG_SOURCE, encoding="utf-8")
    subprocess.run(["clang", "-dynamiclib", "-o", str(dylib), str(source)], check=True)
    folder = (tmp_path / "data").resolve()
    folder.mkdir()
    journal = folder / "macos-update.json"
    journal.write_text('{"state":"swapping"}\n', encoding="utf-8")
    assert _fsyncs(native_swap_helper, dylib, tmp_path, "--sync", str(journal)) == [f"fsync {journal}", f"fsync {folder}"]
    assert _fsyncs(native_swap_helper, dylib, tmp_path, "--sync-parent", str(journal)) == [f"fsync {folder}"]
    mutant_source = tmp_path / "swap-no-sync.c"
    mutant_source.write_text(SWAP_SOURCE.read_text(encoding="utf-8").replace("fsync(file) != 0 || fsync(parent) != 0", "0")
                             .replace("int result = fsync(parent);", "int result = 0;"), encoding="utf-8")
    mutant = tmp_path / "swap-no-sync"
    subprocess.run(["clang", str(mutant_source), "-o", str(mutant)], check=True)
    assert _fsyncs(mutant, dylib, tmp_path, "--sync", str(journal)) == []
    assert _fsyncs(mutant, dylib, tmp_path, "--sync-parent", str(journal)) == []


def test_native_lock_publishes_atomically_and_rejects_contention(native_swap_helper: Path, tmp_path: Path):
    lock = tmp_path / "macos-update.lock"
    owner = os.getpid()

    def acquire(index: int) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [native_swap_helper, "--lock-acquire", lock, str(owner), f"test-token-{index}"],
            check=False, capture_output=True, text=True,
        )

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(acquire, range(8)))
    successes = [result for result in results if result.returncode == 0]
    assert len(successes) == 1
    assert all(result.returncode in {0, 3} for result in results)
    status = subprocess.run(
        [native_swap_helper, "--lock-status", lock], check=False, capture_output=True, text=True,
    )
    assert status.returncode == 3
    assert status.stdout.split()[0] == str(owner)
    token = status.stdout.split()[-1]
    refused = subprocess.run(
        [native_swap_helper, "--lock-release", lock, str(owner), "wrong-token"],
        check=False, capture_output=True, text=True,
    )
    assert refused.returncode == 2
    assert subprocess.run([native_swap_helper, "--lock-status", lock], check=False).returncode == 3
    subprocess.run([native_swap_helper, "--lock-release", lock, str(owner), token], check=True)
    assert subprocess.run([native_swap_helper, "--lock-status", lock], check=False).returncode == 0


def test_native_lock_recovers_only_dead_readable_owner(native_swap_helper: Path, tmp_path: Path):
    lock = tmp_path / "macos-update.lock"
    owner = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    try:
        subprocess.run(
            [native_swap_helper, "--lock-acquire", lock, str(owner.pid), "dead-owner"],
            check=True,
        )
        owner.terminate()
        owner.wait(timeout=10)
        subprocess.run(
            [native_swap_helper, "--lock-acquire", lock, str(os.getpid()), "new-owner"],
            check=True,
        )
        status = subprocess.run(
            [native_swap_helper, "--lock-status", lock], check=False, capture_output=True, text=True,
        )
        assert status.returncode == 3 and status.stdout.split()[-1] == "new-owner"
        subprocess.run(
            [native_swap_helper, "--lock-release", lock, str(os.getpid()), "new-owner"],
            check=True,
        )
    finally:
        if owner.poll() is None:
            owner.kill()
            owner.wait(timeout=10)
    unreadable = tmp_path / "unreadable-lock"
    unreadable.mkdir()
    assert subprocess.run(
        [native_swap_helper, "--lock-status", unreadable], check=False,
    ).returncode == 2
    assert subprocess.run(
        [native_swap_helper, "--lock-acquire", unreadable, str(os.getpid()), "not-safe"],
        check=False,
    ).returncode == 2
    assert unreadable.is_dir()


def test_native_helper_reports_process_liveness(native_swap_helper: Path):
    assert subprocess.run(
        [native_swap_helper, "--pid-alive", str(os.getpid())], check=False,
    ).returncode == 0
    assert subprocess.run(
        [native_swap_helper, "--pid-alive", "2147483647"], check=False,
    ).returncode == 1
    zombie = os.fork()
    if zombie == 0:
        os._exit(0)
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            result = subprocess.run(
                [native_swap_helper, "--pid-alive", str(zombie)], check=False,
            )
            if result.returncode == 1:
                break
            time.sleep(0.02)
        assert result.returncode == 1
    finally:
        os.waitpid(zombie, 0)


def test_native_helper_defers_for_a_running_bundled_game(native_swap_helper: Path, tmp_path: Path):
    executable = tmp_path / "openbfme-v1.2.3-macos-arm64" / "OpenBFME.app" / "Contents" / "MacOS" / "OpenBFME"
    executable.parent.mkdir(parents=True)
    source = tmp_path / "game.c"
    source.write_text("#include <unistd.h>\nint main(void) { for (;;) pause(); }\n", encoding="ascii")
    subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
    process = subprocess.Popen([executable])
    try:
        result = subprocess.run(
            [native_swap_helper, "--game-running", executable.parents[3], str(os.getpid())],
            check=False, capture_output=True, text=True,
        )
        assert result.returncode == 3
        result = subprocess.run(
            [native_swap_helper, "--process-under", str(process.pid), executable.parents[2]],
            check=False, capture_output=True, text=True,
        )
        assert result.returncode == 3
        outside = tmp_path / "outside"
        outside.mkdir()
        result = subprocess.run(
            [native_swap_helper, "--process-under", str(process.pid), outside],
            check=False, capture_output=True, text=True,
        )
        assert result.returncode == 0
    finally:
        process.terminate()
        process.wait(timeout=10)
