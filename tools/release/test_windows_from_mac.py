"""Synthetic tests for the private Windows content installer; no game data needed."""
import contextlib
import io
import sys
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('windows_from_mac', Path(__file__).with_name('windows_from_mac.py'))
workflow = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(workflow)


class WindowsBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.user = self.root / 'settings'; self.user.mkdir()
        self.rotwk = self.root / 'rotwk'; self.rotwk.mkdir()
        self.bfme2 = self.root / 'bfme2'; self.bfme2.mkdir()
        for folder in (self.rotwk, self.bfme2): (folder / 'fixture.big').write_bytes(b'generated test content')
        (self.user / 'install-paths.cfg').write_text(f'ROTWK_INSTALL={self.rotwk}\nBFME2_INSTALL={self.bfme2}\n')

    def select(self, enabled, textures=(), hd=True):
        (self.user / 'mods.cfg').write_text('[mods]\nenabled=Array[String](' + json.dumps(enabled) + ')\ntextures_only=' + json.dumps(list(textures)) + '\nhd_textures=' + json.dumps(hd))

    def test_editor_errors_fail_even_when_its_exit_code_is_zero(self):
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ValueError, 'Godot import/export reported errors'):
                workflow.checked_editor(sys.executable, '-c', 'print("SCRIPT ERROR: fixture failure")')

    def test_active_mods_keep_order_and_texture_filter_but_not_source_paths(self):
        a = self.root / 'Mod with spaces'; a.mkdir(); (a / 'a.ini').write_text('authored fixture')
        b = self.root / 'textures.big'; b.write_bytes(b'generated texture fixture')
        self.select([str(a), str(b)], [str(b)])
        profile, sources = workflow.inventory(self.user)
        self.assertEqual([m['path'] for m in profile['mods']], ['content/mods/000', 'content/mods/001.big'])
        self.assertEqual([m['textures_only'] for m in profile['mods']], [False, True])
        self.assertNotIn(str(self.root), json.dumps(profile))
        self.assertIn((b.resolve(), 'content/mods/001.big'), sources)

    def test_disabled_hd_pack_and_inactive_mod_are_excluded(self):
        self.select([str(self.root / 'not installed')], [str(self.root / 'not installed')], hd=False)
        self.assertEqual(workflow.inventory(self.user)[0]['mods'], [])
        self.select([str(self.root / 'not installed')])
        with self.assertRaisesRegex(ValueError, 'Active mod does not exist'): workflow.inventory(self.user)

    def test_installed_maps_are_included(self):
        maps = self.user / 'installed-maps'; maps.mkdir(); (maps / 'fixture.txt').write_text('map fixture')
        _, sources = workflow.inventory(self.user)
        self.assertIn('content/installed-maps/fixture.txt', [target for _, target in workflow.files(sources)])

    def test_windows_filename_collisions_and_reserved_names_are_refused(self):
        folder = self.root / 'files'; folder.mkdir()
        (folder / 'CON.txt').write_text('fixture')
        with self.assertRaisesRegex(ValueError, 'Windows-incompatible'): list(workflow.files([(folder, 'content')]))
        (folder / 'CON.txt').unlink()
        (folder / 'one.txt').write_text('fixture')
        with self.assertRaisesRegex(ValueError, 'collision'): list(workflow.files([(folder, 'content'), (folder, 'content')]))

    def test_directory_links_are_copied_and_cycles_are_rejected(self):
        target = self.root / 'target'; target.mkdir(); (target / 'fixture.txt').write_text('fixture')
        folder = self.root / 'links'; folder.mkdir(); (folder / 'linked').symlink_to(target, target_is_directory=True)
        self.assertEqual([name for _, name in workflow.files([(folder, 'content')])], ['content/linked/fixture.txt'])
        (target / 'cycle').symlink_to(folder, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Cyclic'): list(workflow.files([(folder, 'content')]))

    def test_external_map_library_uses_a_portable_bundle_root(self):
        library = self.root / 'library'; library.mkdir(); (library / 'fixture.txt').write_text('fixture')
        with self.assertRaisesRegex(ValueError, 'Explicit map library'):
            workflow.inventory(self.user, map_library=self.root / 'missing-library')
        _, sources = workflow.inventory(self.user, map_library=library)
        self.assertIn('content/map-library/bundles/mac-snapshot/rotwk/fixture.txt', [target for _, target in workflow.files(sources)])

    @unittest.skipUnless(shutil.which('makensis'), 'NSIS required for installer compilation')
    def test_real_installer_compilation_and_symlink_content_is_self_contained(self):
        exported = self.root / 'export'; exported.mkdir()
        for name in ('OpenBFME.exe', 'OpenBFME.pck', 'openbfme.windows.template_debug.x86_64.dll'):
            (exported / name).write_bytes(b'synthetic exported file')
        original = self.root / 'original.txt'; original.write_text('content reached through link')
        (self.rotwk / 'linked.txt').symlink_to(original)
        profile, sources = workflow.inventory(self.user)
        output = self.root / 'installer'
        workflow.package(exported, output, profile, sources)
        self.assertTrue((output / 'OpenBFME-Setup.exe').is_file())
        with tarfile.open(output / 'OpenBFME-content.tar') as archive:
            member = archive.getmember('content/rotwk/linked.txt')
            self.assertTrue(member.isfile())
            self.assertEqual(archive.extractfile(member).read(), original.read_bytes())
            manifest = json.load(archive.extractfile('content-manifest.json'))
            self.assertEqual(next(row['sha256'] for row in manifest if row['path'] == member.name), workflow.digest(original))
        with self.assertRaisesRegex(ValueError, 'already exists'): workflow.package(exported, output, profile, sources)


if __name__ == '__main__': unittest.main()
