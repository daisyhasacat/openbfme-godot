import concurrent.futures
import tempfile
import unittest
from pathlib import Path
from serve import Store, safe_path

class SyncTests(unittest.TestCase):
    def test_two_devices_conflict_and_versions(self):
        with tempfile.TemporaryDirectory() as root:
            store = Store(root)
            ok, first = store.put('alice', 'saves/hero', '', b'original')
            self.assertTrue(ok)
            ok, second = store.put('alice', 'saves/hero', first, b'device A')
            self.assertTrue(ok)
            self.assertEqual(store.put('alice', 'saves/hero', first, b'device B'), (False, second))
            self.assertEqual(store.manifest('alice'), {'saves/hero': second})
            self.assertEqual((Path(root)/'alice'/'blobs'/first).read_bytes(), b'original')
            self.assertEqual(store.manifest('bob'), {})
            self.assertEqual(Store(root).manifest('alice'), store.manifest('alice'))

    def test_concurrent_first_upload_only_one_wins(self):
        with tempfile.TemporaryDirectory() as root:
            store=Store(root)
            with concurrent.futures.ThreadPoolExecutor() as pool:
                results=list(pool.map(lambda data: store.put('alice','player/Skirmish.ini','',data), [b'A',b'B']))
            self.assertEqual(sum(ok for ok, _ in results), 1)

    def test_paths(self):
        for path in ['', '../outside', 'saves/../bad', '/absolute', 'x\\y', 'C:/file', 'x//y']:
            self.assertFalse(safe_path(path), path)
        self.assertTrue(safe_path('saves/My hero.cah'))

if __name__=='__main__': unittest.main()
