import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import windows_existing_zip as portable


class PortableTests(unittest.TestCase):
    def test_zip_has_only_runtime_and_instructions(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); exported=root/'export';exported.mkdir()
            for name in portable.NAMES: (exported/name).write_text('runtime fixture')
            (exported/'private.pdb').write_text('private fixture')
            output=root/'portable.zip'
            portable.package(exported,output)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), {'OpenBFME/'+name for name in portable.NAMES}|{'OpenBFME/README.txt'})
                self.assertIsNone(archive.testzip())
            with self.assertRaisesRegex(ValueError,'already exists'):portable.package(exported,output)

    def test_missing_runtime_fails_without_zip(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);output=root/'portable.zip'
            with self.assertRaisesRegex(ValueError,'Missing Windows export'):portable.package(root,output)
            self.assertFalse(output.exists())
