import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

spec = importlib.util.spec_from_file_location('publish', Path(__file__).with_name('publish_windows_installer.py'))
publish = importlib.util.module_from_spec(spec); spec.loader.exec_module(publish)


class UploadTests(unittest.TestCase):
    def test_uploads_verified_files_then_completion_marker_using_environment(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in publish.FILES: (root / name).write_text('synthetic fixture')
            checksum = publish.sha256(root / publish.FILES[0])
            (root / 'README.txt').write_text(checksum)
            calls = []
            def execute(command, **kwargs):
                calls.append(command)
                if 'list-objects-v2' in command: result = {'KeyCount': 0}
                elif 'head-object' in command:
                    name = command[command.index('--key') + 1].split('/')[-1]
                    result = {'ContentLength': (root / name).stat().st_size, 'Metadata': {'sha256': publish.sha256(root / name)}}
                else: result = {}
                return subprocess.CompletedProcess(command, 0, json.dumps(result))
            with patch.dict('os.environ', {'OPENBFME_CDN_BUCKET':'fixture', 'OPENBFME_CDN_RELEASE':'test-build', 'AWS_ACCESS_KEY_ID':'fixture-secret'}, clear=True), patch.object(publish.subprocess, 'run', side_effect=execute):
                result = publish.publish(root)
            self.assertEqual(result['release'], 'test-build')
            self.assertTrue(calls[-1][-4].endswith('manifest.json'))
            self.assertFalse(any('--acl' in c for c in calls))
            self.assertNotIn('fixture-secret', str(calls))
            self.assertTrue(any('s3://fixture/openbfme/windows-installers/test-build/OpenBFME-content.tar' in c for c in calls))

    def test_mismatched_payload_is_rejected_before_network(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for name in publish.FILES: (root/name).write_text('fixture')
            with patch.object(publish.subprocess,'run') as call:
                with self.assertRaisesRegex(ValueError,'checksum'): publish.publish(root,'fixture')
                call.assert_not_called()
