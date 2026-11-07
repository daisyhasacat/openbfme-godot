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
                    if name == 'manifest.json': name = 'cdn-upload.json'
                    result = {'ContentLength': (root / name).stat().st_size, 'Metadata': {'sha256': publish.sha256(root / name)}}
                else: result = {}
                return subprocess.CompletedProcess(command, 0, json.dumps(result))
            with patch.dict('os.environ', {'OPENBFME_CDN_BUCKET':'fixture', 'OPENBFME_CDN_RELEASE':'test-build', 'AWS_ACCESS_KEY_ID':'fixture-secret'}, clear=True), patch.object(publish.subprocess, 'run', side_effect=execute):
                result = publish.publish(root)
            self.assertEqual(result['release'], 'test-build')
            self.assertEqual(calls[-1][-1].split('/')[-1], 'manifest.json')
            self.assertFalse((root / 'OpenBFME-content.tar').exists())
            self.assertTrue((root / 'cdn-upload.json').exists())
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

    def test_cleanup_refuses_changed_artifacts_and_preserves_unrelated_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in publish.FILES: (root/name).write_text('fixture')
            rows = [{'name':name,'bytes':(root/name).stat().st_size,'sha256':publish.sha256(root/name)} for name in publish.FILES]
            (root/'unrelated.txt').write_text('keep')
            (root/publish.FILES[-1]).write_text('changed')
            with self.assertRaisesRegex(ValueError,'changed'): publish.cleanup_local(root, {'files':rows})
            self.assertTrue(all((root/name).exists() for name in publish.FILES))
            (root/publish.FILES[-1]).write_text('fixture')
            publish.cleanup_local(root, {'files':rows})
            self.assertTrue((root/'unrelated.txt').exists())

    def test_failed_upload_retains_all_local_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in publish.FILES: (root/name).write_text('fixture')
            (root/'README.txt').write_text(publish.sha256(root/publish.FILES[0]))
            with patch.object(publish.subprocess,'run',side_effect=subprocess.CalledProcessError(1,'aws')):
                with self.assertRaises(subprocess.CalledProcessError): publish.publish(root,'fixture')
            self.assertTrue(all((root/name).exists() for name in publish.FILES))
