"""Lane ONLINEHUB-1: the game's results reporting end to end (game.gd --onlinehub1-check, scripts/onlinehub1_check.gd) against the backend's own service
(tools/platform_fake): sent once (and retried once after a failure), never for a skirmish, never with the option off, never twice for one game, and a retry
waiting when the player turns sharing off is dropped; the service's log is the proof. Needs Godot with the openbfme extension (GODOT, else `godot`) and the
retail installs (ROTWK_INSTALL, BFME2_INSTALL): SKIP LOUDLY otherwise."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def _skip_reason() -> str | None:
    if not shutil.which(GODOT):
        return "Godot is not installed here (GODOT)"
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        return "ROTWK_INSTALL / BFME2_INSTALL are not set"
    if not list((REPO / "godot" / "bin").glob("openbfme.*")):
        return "the openbfme GDExtension is not built (godot/bin)"
    return None


def _get(port: int, path: str) -> dict:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=10) as r:
        return json.loads(r.read())


def _post(port: int, path: str, body: dict) -> dict:
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def test_results_are_sent_once_retried_once_and_never_for_single_player_or_when_off():
    why = _skip_reason()
    if why:
        pytest.skip("SKIP LOUDLY: " + why + ", so the game's results reporting test did not run")
    fake = subprocess.Popen([sys.executable, "-S", str(REPO / "tools/platform_fake/fake_server.py"), "--admin-token", "t", "--port=0"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    home = tempfile.mkdtemp(prefix="openbfme-hub1-home-")
    try:
        line = fake.stdout.readline()
        port = json.loads(line)["port"]
        # the first POST of g1 and the first of g5 answer 503 (each game's own body)
        _post(port, "/__test/fail", {"prefix": "/v1/results", "status": 503, "count": 1, "body_contains": "hub1-g1"})
        _post(port, "/__test/fail", {"prefix": "/v1/results", "status": 503, "count": 1, "body_contains": "hub1-g5"})
        env = {**os.environ, "HOME": home, "XDG_DATA_HOME": os.path.join(home, ".local/share"), "OPENBFME_PLATFORM_RETRY_S": "3"}
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--", "--onlinehub1-check", f"--platform-url=http://127.0.0.1:{port}/v1",
                            "--movies=off"], env=env, capture_output=True, text=True, timeout=600)
        out = r.stdout + r.stderr
        lines = [l for l in out.splitlines() if l.startswith("HUB1 ")]
        assert r.returncode == 0, "\n".join(lines) or out[-4000:]
        assert lines[-1] == "HUB1 RESULT: 12 ok, 0 failed", "\n".join(lines)
        entries = [e for e in _get(port, "/__test/log")["log"] if e["method"] == "POST" and e["path"] == "/v1/results"]
        posts = [json.loads(e["body"]) for e in entries]
        # g1 (503), the LAN game (201), g1 again after the retry time (201), g5 (503, its retry dropped when sharing went off): nothing else, nothing later
        assert [p["game"]["id"] for p in posts] == ["hub1-g1", "lan-0123456789abcdef0123456789abcdef", "hub1-g1", "hub1-g5"]
        assert [e["status"] for e in entries] == [503, 201, 201, 503]
        install = posts[0]["install"]
        assert len(install) == 32 and all(c in "0123456789abcdef" for c in install)
        assert all(p["install"] == install and p["nick"] == "Checker" and p["me"] == {"slot": 0} for p in posts)
        # every write carries the client metadata the service requires: this platform and the game's own version tag
        assert all(p["client"]["platform"] in ("linux", "windows", "macos") and 1 <= len(p["client"]["version"]) <= 64 for p in posts)
        assert posts[1]["game"]["winner_team"] == 1 and posts[1]["game"]["players"][1]["human"] is True
        # the install id is never printed whole (the logs name it by its first digits at most)
        assert install not in out
    finally:
        fake.kill()
        fake.wait()
        shutil.rmtree(home, ignore_errors=True)
