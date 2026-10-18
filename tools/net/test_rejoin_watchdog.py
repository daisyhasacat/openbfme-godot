# Lane ONLINEUX-1 r2 (Claude F3, Sol 2): the rejoin catch-up's watchdog (godot/scripts/rejoin_watch.gd): godot/tests/rejoin_watch_test.gd checks that
# every attempt in one run is watched (a second failure, a stall after a live attempt), each said once.
import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def test_every_rejoin_attempt_is_watched(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the rejoin watch")
    env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
    r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/rejoin_watch_test.gd"], capture_output=True,
                       text=True, timeout=240, env=env)
    out = r.stdout + r.stderr
    assert "WATCH FAIL" not in out, out[-4000:]
    assert out.count("WATCH ok") == 15 and r.returncode == 0, out[-4000:]
