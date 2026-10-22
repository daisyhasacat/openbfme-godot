import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from publish_appimage import publish

class PublishTest(unittest.TestCase):
    def test_publish_and_reject_preserves_latest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); image = root / 'input'; out = root / 'public'
            payload = b'\x7fELF\x02\x01\x01\x00AI\x02test'
            image.write_bytes(payload)
            first = publish(image, out, 'test', 'commit')
            self.assertEqual(json.loads((out / 'latest.json').read_text()), first)
            self.assertEqual((out / (hashlib.sha256(payload).hexdigest() + '.AppImage')).read_bytes(), payload)
            image.write_bytes(b'broken')
            with self.assertRaises(ValueError): publish(image, out, 'bad', 'bad')
            self.assertEqual(json.loads((out / 'latest.json').read_text()), first)

if __name__ == '__main__': unittest.main()
