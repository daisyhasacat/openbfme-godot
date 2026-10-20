import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.request
import urllib.error
import zipfile

spec = importlib.util.spec_from_file_location('lan_serve', Path(__file__).with_name('serve.py'))
serve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serve)


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        mod = self.root / 'Texture pack'
        (mod / 'art/textures').mkdir(parents=True)
        (mod / 'art/textures/fixture.txt').write_text('synthetic texture fixture')
        (mod / 'run.exe').write_text('excluded')
        (mod / '.secret').write_text('excluded')
        (mod / 'outside').symlink_to(self.root)
        self.catalog = serve.Catalogue(self.root / 'cache')
        self.catalog.add(mod, textures=True)
        self.server = serve.http.server.ThreadingHTTPServer(('127.0.0.1', 0), serve.handler(self.catalog))
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}'

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()

    def get(self, path):
        return urllib.request.urlopen(self.url + path).read()

    def test_catalog_download_checksum_and_changes(self):
        catalog = json.loads(self.get('/catalog.json'))
        self.assertEqual(catalog['version'], 1)
        pack = catalog['packs'][0]
        self.assertTrue(pack['textures_only'])
        self.assertNotIn('_root', pack)
        meta = json.loads(self.get('/packs/' + pack['id']))
        payload = self.get(meta['download'])
        self.assertEqual(len(payload), meta['bytes'])
        self.assertEqual(hashlib.sha256(payload).hexdigest(), meta['sha256'])
        import io
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            self.assertEqual(archive.namelist(), ['art/textures/fixture.txt'])
        (self.root / 'Texture pack/art/textures/fixture.txt').write_text('changed content')
        newer = json.loads(self.get('/packs/' + pack['id']))
        self.assertNotEqual(newer['sha256'], meta['sha256'])

    def test_only_catalogued_endpoints_are_served(self):
        for path in ('/../secret', '/catalog.json?path=/etc/passwd', '/files/'+'f'*64+'.zip'):
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.get(path)
            self.assertEqual(error.exception.code, 404)

    def test_aotr_dependency_and_edain_archive_selection(self):
        root = self.root / 'AotR'
        (root / 'aotr/data').mkdir(parents=True)
        (root / 'rotwk').mkdir()
        (root / 'aotr/data/fixture.ini').write_text('synthetic')
        (root / 'rotwk/#aotr_patch202.big').write_text('synthetic dependency')
        (root / 'rotwk/INI.big').write_text('excluded base archive')
        self.catalog.add(root, 'aotr', game='aotr')
        pack = next(p for p in self.catalog.public() if p['kind'] == 'aotr')
        self.catalog.prepare(pack['id'])
        with zipfile.ZipFile(self.catalog.packs[pack['id']]['_file']) as archive:
            self.assertEqual(set(archive.namelist()), {'aotr/data/fixture.ini', 'rotwk/#aotr_patch202.big'})
            self.assertEqual({i.compress_type for i in archive.infolist()}, {zipfile.ZIP_STORED})
        (root / 'rotwk/__edain_data.big').write_text('synthetic mod')
        self.assertEqual([name for name, _ in serve.pack_files(root / 'rotwk', 'edain')],
                         ['__edain_data.big'])

    def test_map_sidecars_and_builtin_discovery(self):
        user = self.root / 'user'
        (user / 'mods').mkdir(parents=True)
        (user / 'mods/wall_towers.big').write_text('synthetic big fixture')
        maps = self.root / 'maps/Example'
        maps.mkdir(parents=True)
        (maps / 'Example.map').write_text('synthetic map fixture')
        (maps / 'map.ini').write_text('synthetic sidecar')
        self.catalog.packs.clear()
        serve.discover(self.catalog, user, self.root / 'repo', [(maps.parent, 'bfme2')])
        packs = self.catalog.public()
        self.assertEqual({p['kind'] for p in packs}, {'builtin', 'map'})
        pack = next(p for p in packs if p['kind'] == 'map')
        self.assertEqual(pack['entry'], 'Example.map')
        meta = self.catalog.prepare(pack['id'])
        with zipfile.ZipFile(self.catalog.packs[pack['id']]['_file']) as archive:
            self.assertEqual(set(archive.namelist()), {'Example.map', 'map.ini'})


if __name__ == '__main__':
    unittest.main()
