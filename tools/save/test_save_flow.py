# Lane SAVE-4e review r2 (Sol): the load menu's flow (godot/scripts/save_flow.gd) with a stand-in GameWorld: godot/tests/save4e_flow_test.gd checks that
# a running game which cannot be kept is never torn down for a load (the load stops, named, on the load page) and that a kept game is left as before.
import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def test_a_running_game_that_cannot_be_kept_is_not_torn_down(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the load menu's flow")
    env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
    r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/save4e_flow_test.gd"], capture_output=True,
                       text=True, timeout=240, env=env)
    out = r.stdout + r.stderr
    assert "FLOW FAIL" not in out, out[-4000:]
    assert out.count("FLOW ok") == 8 and "FLOW PASS" in out and r.returncode == 0, out[-4000:]
