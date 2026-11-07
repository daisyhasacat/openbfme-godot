"""A rebuild must leave the pack of a running game untouched; the game content it carries is a complete, verified install without programs."""
import json
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import package_macos_standalone
from package_macos_standalone import check_content, copy_content, ensure_bundle_stopped


class RunningBundleTest(unittest.TestCase):
    def test_only_the_target_bundle_blocks_replacement(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'Game With Spaces.app'
            (app / 'Contents/MacOS').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'OpenBFME'}))
            executable = str((app / 'Contents/MacOS/OpenBFME').resolve())
            with patch('package_macos_standalone.subprocess.run') as run:
                run.return_value.stdout = executable + '-other\n/usr/bin/python3\n'
                ensure_bundle_stopped(app)
                run.return_value.stdout = executable + '\n'
                with self.assertRaisesRegex(RuntimeError, 'Quit Game With Spaces.app'):
                    ensure_bundle_stopped(app)
            self.assertTrue((app / 'Contents/Info.plist').is_file())

    def test_process_inspection_failure_cannot_allow_replacement(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'Game.app'
            (app / 'Contents').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'OpenBFME'}))
            with patch('package_macos_standalone.subprocess.run', side_effect=subprocess.CalledProcessError(1, 'ps')):
                with self.assertRaises(subprocess.CalledProcessError):
                    ensure_bundle_stopped(app)


class ContentTest(unittest.TestCase):
    def _policy(self, temp):
        policy = Path(temp) / 'policy.json'
        policy.write_text(json.dumps({'game': 'bfme2', 'patch': '1.06', 'archives': [
            {'path': 'INI.big', 'size': 3}, {'path': 'lang/English.big', 'size': 2}]}))
        return policy

    def test_an_incomplete_or_changed_install_is_refused_by_name(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'BFME2'
            (source / 'lang').mkdir(parents=True)
            (source / 'ini.big').write_bytes(b'abc')  # found case-insensitively, as the game finds it
            with patch.dict(package_macos_standalone.CONTENT, {'bfme2': self._policy(temp)}):
                with self.assertRaisesRegex(RuntimeError, 'missing lang/English.big'):
                    check_content('bfme2', source)
                (source / 'lang/english.big').write_bytes(b'xyz')
                with self.assertRaisesRegex(RuntimeError, 'size of lang/English.big'):
                    check_content('bfme2', source)
                (source / 'lang/english.big').write_bytes(b'xy')
                self.assertEqual(check_content('bfme2', source), (2, 5))

    def test_the_copy_keeps_the_data_and_leaves_the_programs_out(self):
        with tempfile.TemporaryDirectory() as temp:
            source, target = Path(temp) / 'BFME2', Path(temp) / 'app/retail/bfme2'
            (source / 'data/movies').mkdir(parents=True)
            for name in ['ini.big', 'gi.dat', 'data/movies/intro.vp6', 'game.exe', 'binkw32.DLL', 'Worldbuilder.dbgcmd', '.DS_Store']:
                (source / name).write_bytes(b'x')
            copy_content(source, target)
            kept = sorted(str(p.relative_to(target)) for p in target.rglob('*') if p.is_file())
            self.assertEqual(kept, ['data/movies/intro.vp6', 'gi.dat', 'ini.big'])


if __name__ == '__main__':
    unittest.main()
