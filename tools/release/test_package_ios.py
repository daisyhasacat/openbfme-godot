"""iOS export must configure its own template even after presets are reordered."""
from pathlib import Path
import re
import unittest
import package_ios


def section(text, name):
    return re.search(r'^\[' + re.escape(name) + r'\]\n(.*?)(?=^\[|\Z)', text, re.M | re.S).group(1)


class PackageIosTest(unittest.TestCase):

    def test_real_presets_only_change_ios(self):
        source = (package_ios.ROOT / 'godot/export_presets.cfg').read_text()
        result = package_ios.configure_ios_preset(source, Path('/tmp/arm64-simulator.zip'), 'TEAM123')
        ios_number = re.search(r'^\[preset\.(\d+)\]\n\nname="iOS"', source, re.M).group(1)
        for name in re.findall(r'^\[([^\]]+)\]', source, re.M):
            if name == 'preset.' + ios_number + '.options':
                options = section(result, name)
                assert 'custom_template/debug="/tmp/arm64-simulator.zip"' in options
                assert 'custom_template/release="/tmp/arm64-simulator.zip"' in options
                assert 'application/app_store_team_id="TEAM123"' in options
            else:
                assert section(result, name) == section(source, name)


    def test_reordered_ios_preset_and_escaped_path(self):
        source = '[preset.19]\nname="iOS"\nplatform="iOS"\n[preset.19.options]\ncustom_template/debug=""\ncustom_template/release=""\napplication/app_store_team_id=""\n'
        result = package_ios.configure_ios_preset(source, '/tmp/with "quotes"/template.zip', 'TEAM')
        assert 'custom_template/debug="/tmp/with \\"quotes\\"/template.zip"' in result
        assert 'application/app_store_team_id="TEAM"' in result


    def test_missing_or_ambiguous_ios_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'exactly one iOS'):
            package_ios.configure_ios_preset('[preset.3]\nname="macOS"\nplatform="macOS"\n', '/tmp/a.zip', 'TEAM')
        with self.assertRaisesRegex(ValueError, 'exactly one iOS'):
            package_ios.configure_ios_preset('[preset.1]\nname="iOS"\nplatform="iOS"\n[preset.2]\nname="iOS"\nplatform="iOS"\n', '/tmp/a.zip', 'TEAM')
        with self.assertRaisesRegex(ValueError, 'no options'):
            package_ios.configure_ios_preset('[preset.1]\nname="iOS"\nplatform="iOS"\n', '/tmp/a.zip', 'TEAM')


if __name__ == "__main__":
    unittest.main()
