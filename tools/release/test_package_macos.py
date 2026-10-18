"""A rebuild must leave the pack of a running game untouched."""
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from package_macos_standalone import ensure_bundle_stopped


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


if __name__ == '__main__':
    unittest.main()
