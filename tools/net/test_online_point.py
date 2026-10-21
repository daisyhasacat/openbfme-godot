"""Exercise the Godot device's real button targeting against an isolated local lobby."""
import os
from pathlib import Path
import shutil
import socket
import subprocess

import pytest

REPO = Path(__file__).resolve().parents[2]


def isolated_project(tmp_path):
    """Keep autoload settings, sync configuration, and session files away from the player's real user folder."""
    project = tmp_path / 'project'
    project.mkdir()
    for source in (REPO / 'godot').iterdir():
        if source.name in ('project.godot', '.godot', 'override.cfg'):
            continue
        (project / source.name).symlink_to(source, target_is_directory=source.is_dir())
    shutil.copytree(REPO / 'godot/.godot', project / '.godot')
    text = (REPO / 'godot/project.godot').read_text()
    text = text.replace('config/name="OpenBFME"', f'config/name="OpenBFME-Online-Tests-{tmp_path.name}"')
    (project / 'project.godot').write_text(text)
    return project


def test_online_walkthrough_targets_button_hit_areas(tmp_path):
    godot = os.environ.get('GODOT', 'godot')
    if not shutil.which(godot) or not shutil.which('node'):
        pytest.skip('SKIP LOUDLY: Godot and Node are required for the real online point test')
    if not os.environ.get('ROTWK_INSTALL') or not os.environ.get('BFME2_INSTALL'):
        pytest.skip('SKIP LOUDLY: retail mounts are required for the real online point test')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    env = dict(os.environ, LOBBY_BIND=f'127.0.0.1:{port}', LOBBY_DATA=str(tmp_path / 'lobby'),
               TICKET_KEYS='1:' + '01' * 32, SESSION_KEY='02' * 32, RELAYS='')
    with (tmp_path / 'lobby.log').open('w') as log:
        lobby = subprocess.Popen(['node', str(REPO / 'services/online/lobby/src/server.mjs')], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            result = subprocess.run([godot, '--headless', '--path', str(isolated_project(tmp_path)), '--script', 'res://tests/online_point_test.gd',
                                     '--', f'--lobby=http://127.0.0.1:{port}'], env=env, capture_output=True, text=True, timeout=120)
            output = result.stdout + result.stderr
            assert result.returncode == 0, output[-6000:]
            assert 'SCRIPT ERROR' not in output, output[-6000:]
            assert 'ONLINE POINT PASS:' in output, output[-6000:]
        finally:
            lobby.terminate()
            lobby.wait(timeout=10)


def test_network_replay_preserves_initial_simulation_settings(tmp_path):
    godot = os.environ.get('GODOT', 'godot')
    if not shutil.which(godot) or not os.environ.get('ROTWK_INSTALL') or not os.environ.get('BFME2_INSTALL'):
        pytest.skip('SKIP LOUDLY: Godot and retail mounts are required for the network replay test')
    result = subprocess.run([godot, '--headless', '--path', str(isolated_project(tmp_path)), '--script', 'res://tests/online_replay_settings_test.gd',
                             '--', f'--record={tmp_path / "network.replay"}'], capture_output=True, text=True, timeout=60)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output[-6000:]
    assert 'SCRIPT ERROR' not in output, output[-6000:]
    assert 'ONLINE REPLAY SETTINGS PASS:' in output, output[-6000:]


def test_replay_check_exits_on_a_stopped_mismatch(tmp_path):
    godot = os.environ.get('GODOT', 'godot')
    if not shutil.which(godot):
        pytest.skip('SKIP LOUDLY: Godot is required for the replay-check failure test')
    result = subprocess.run([godot, '--headless', '--path', str(isolated_project(tmp_path)), '--script', 'res://tests/replay_check_failure_test.gd'],
                            capture_output=True, text=True, timeout=30)
    output = result.stdout + result.stderr
    assert result.returncode == 1, output[-4000:]
    assert 'SCRIPT ERROR' not in output, output[-4000:]
    assert 'REPLAY CHECK FAILURE TEST timed out' not in output, output[-4000:]
    assert 'GAME REPLAY RESULT: FAILED' in output
    assert 'GAME REPLAY mismatch at frame 2' in output
