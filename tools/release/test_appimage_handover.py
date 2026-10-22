"""Exercise the real update helper with harmless executables and hostile filenames."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (ROOT / 'godot/scripts/release/appimage_handover.gd').read_text().split('"""', 2)[1]

class HandoverTest(unittest.TestCase):
    def run_helper(self, corrupt=False, fail=False, backup_directory=False):
        with tempfile.TemporaryDirectory(prefix="update space '$ ") as tmp:
            root = Path(tmp)
            image = root / 'Steam shortcut.AppImage'
            image.write_text('#!/bin/sh\nprintf "old\\n" > "$0.started"\n')
            image.chmod(0o755)
            old = image.read_bytes()
            if backup_directory: Path(str(image) + '.previous').mkdir()
            lock = Path(str(image) + '.update-lock'); lock.mkdir()
            candidate = lock / 'new.AppImage'
            candidate.write_text('#!/bin/sh\nexit 1\n' if fail else '#!/bin/sh\nprintf "%s\\n" "$@" > "$0.started"\nsleep 3\n')
            sha = hashlib.sha256(candidate.read_bytes()).hexdigest()
            helper = lock / 'finish.sh'; helper.write_text(SCRIPT)
            # Portable sha256sum shim for macOS tests; the Deck has GNU coreutils.
            shim = root / 'sha256sum'
            shim.write_text('#!/bin/sh\n[ "$1" != -- ] || shift\nexec shasum -a 256 "$@"\n')
            shim.chmod(0o755)
            if sys.platform == 'darwin':
                mv = root / 'mv'
                mv.write_text('#!/bin/sh\n[ \"$1\" != -fT ] || { shift; set -- -f \"$@\"; }\nexec /bin/mv \"$@\"\n')
                mv.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['/bin/sh', str(helper), str(image), str(candidate), '99999999', '0'*64 if corrupt else sha, str(lock), 'one two', '$(touch injected)'], env=env, timeout=15)
            self.assertFalse(lock.exists())
            self.assertFalse((root / 'injected').exists())
            if backup_directory:
                self.assertEqual(result.returncode, 1)
                self.assertEqual(image.read_bytes(), old)
            elif corrupt:
                self.assertEqual(result.returncode, 1)
                self.assertEqual(image.read_bytes(), old)
                self.assertFalse(Path(str(image) + '.previous').exists())
            elif fail:
                self.assertEqual(result.returncode, 0, Path(str(image) + ".update.log").read_text())
                self.assertEqual(image.read_bytes(), old)
                self.assertEqual(Path(str(image) + '.started').read_text(), 'old\n')
            else:
                self.assertEqual(result.returncode, 0, Path(str(image) + ".update.log").read_text())
                self.assertEqual(Path(str(image) + '.previous').read_bytes(), old)
                self.assertEqual(Path(str(image) + '.started').read_text().splitlines(), ['one two', '$(touch injected)'])
    def test_backup_directory_rejected(self): self.run_helper(backup_directory=True)
    def test_replace_and_launch(self): self.run_helper()
    def test_bad_checksum_preserves_original(self): self.run_helper(corrupt=True)
    def test_failed_launch_restores_original(self): self.run_helper(fail=True)

if __name__ == '__main__': unittest.main()
