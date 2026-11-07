"""Existing debug exports must opt in to native trackpad click events."""
import plistlib
from pathlib import Path
import tempfile
import unittest

from sync_ios_export import configure_pointer_input


class PointerInputTest(unittest.TestCase):
    def test_upgrades_existing_exports_without_replacing_custom_settings(self):
        for existing in (None, False, True):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as temp:
                export = Path(temp)
                info = export / 'OpenBFME/OpenBFME-Info.plist'
                info.parent.mkdir()
                settings = {'CFBundleIdentifier': '$(PRODUCT_BUNDLE_IDENTIFIER)',
                            'UIFileSharingEnabled': True}
                if existing is not None:
                    settings['UIApplicationSupportsIndirectInputEvents'] = existing
                info.write_bytes(plistlib.dumps(settings))
                configure_pointer_input(export)
                expected = dict(settings, UIApplicationSupportsIndirectInputEvents=True)
                self.assertEqual(plistlib.loads(info.read_bytes()), expected)
                first = info.read_bytes()
                configure_pointer_input(export)
                self.assertEqual(info.read_bytes(), first)


if __name__ == '__main__':
    unittest.main()
