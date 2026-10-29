"""Check saved window mode at native startup, without touching player settings.

GODOT=/path/to/Godot python3 -m unittest discover -s tools/release -p test_macos_initial_window.py
Requires the built macOS extension. A tiny isolated project has no startup
scene or display scripts: the setting must come from CORE initialization.
"""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import uuid
import unittest
from contextlib import contextmanager



ROOT = Path(__file__).resolve().parents[2]
GODOT = os.environ.get('GODOT', '/Applications/Godot.app/Contents/MacOS/Godot')
LIBRARY = ROOT / f'godot/bin/openbfme.macos.template_debug.{platform.machine()}.dylib'



@contextmanager
def project(tmp_path):
    name = 'OpenBFME-initial-window-test-' + uuid.uuid4().hex
    userdata = Path.home() / 'Library/Application Support/Godot/app_userdata' / name
    userdata.mkdir(parents=True)
    (tmp_path / 'project.godot').write_text(
        'config_version=5\n[application]\nconfig/name=' + json.dumps(name) + '\n'
        '[display]\nwindow/size/viewport_width=1280\nwindow/size/viewport_height=720\n')
    (tmp_path / 'openbfme.gdextension').write_text(
        '[configuration]\nentry_symbol="openbfme_library_init"\ncompatibility_minimum="4.7"\n'
        '[libraries]\nmacos.debug=' + json.dumps(str(LIBRARY)) + '\n')
    (tmp_path / '.godot').mkdir()
    (tmp_path / '.godot/extension_list.cfg').write_text('res://openbfme.gdextension\n')
    (tmp_path / 'probe.gd').write_text(
        'extends SceneTree\nfunc _initialize() -> void:\n'
        '\tprint("NATIVE_INITIAL_WINDOW=",ProjectSettings.get_setting("display/window/size/mode",0))\n'
        '\tprint("NATIVE_WINDOW_MODE=",DisplayServer.window_get_mode())\n'
        '\tquit(0)\n')
    try:
        yield tmp_path, userdata
    finally:
        shutil.rmtree(userdata)


@unittest.skipUnless(platform.system() == 'Darwin' and LIBRARY.is_file(),
                     'requires macOS and its built GDExtension')
class InitialWindowTest(unittest.TestCase):
    def probe(self, saved, expected, args=(), native=False):
        with tempfile.TemporaryDirectory() as temporary, project(Path(temporary)) as (path, userdata):
            if saved is not None:
                (userdata / 'openbfme-display.cfg').write_text('[display]\nwindow_mode=' + saved + '\n')
            display_args = [] if native else ['--headless']
            result = subprocess.run([GODOT, *display_args, '--path', str(path), '--script', 'res://probe.gd', *args],
                                    capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn(f'NATIVE_INITIAL_WINDOW={expected}\n', output)
            self.assertNotIn('SCRIPT ERROR', output)
            if native:
                self.assertIn(f'NATIVE_WINDOW_MODE={expected}\n', output)

    def test_mode_is_selected_before_any_scene_scripts(self):
        for saved, expected in [('"windowed"', 0), ('"borderless"', 3), ('"fullscreen"', 4),
                                ('"invalid"', 0), ('7', 0), (None, 0)]:
            with self.subTest(saved=saved):
                self.probe(saved, expected)

    def test_display_check_stays_isolated_from_player_settings(self):
        self.probe('"borderless"', 0, ('--', '--display-check'))

    @unittest.skipUnless(os.environ.get('OPENBFME_TEST_NATIVE_WINDOW') == '1',
                         'set OPENBFME_TEST_NATIVE_WINDOW=1 to briefly open a fullscreen test window')
    def test_native_window_is_fullscreen_before_scene_setup(self):
        self.probe('"borderless"', 3, native=True)
