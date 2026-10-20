#!/usr/bin/env python3
"""Standalone, username-scoped OpenBFME player sync service (trusted LAN)."""
import argparse
import hashlib
import json
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

LIMIT = 128 * 1024 * 1024

def safe_path(value):
    return bool(value) and len(value) <= 1024 and all(p not in ('', '.', '..') for p in value.split('/')) and not any(c in value for c in '\\:\x00')

class Store:
    def __init__(self, root):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()

    def manifest(self, user):
        path = self.root / user / 'manifest.json'
        return json.loads(path.read_text()) if path.exists() else {}

    def put(self, user, name, base, data):
        if not safe_path(name):
            raise ValueError('Invalid file name')
        digest = hashlib.sha256(data).hexdigest()
        with self.lock:
            manifest = self.manifest(user)
            if manifest.get(name, '') != base:
                return False, manifest.get(name, '')
            folder = self.root / user
            blobs = folder / 'blobs'
            blobs.mkdir(parents=True, exist_ok=True)
            target = blobs / digest
            if not target.exists():
                temp = blobs / (digest + '.part')
                temp.write_bytes(data)
                temp.replace(target)
            manifest[name] = digest
            temp = folder / 'manifest.part'
            temp.write_text(json.dumps(manifest, sort_keys=True))
            temp.replace(folder / 'manifest.json')
            return True, digest

def handler(store):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, code, data, content='application/json'):
            if not isinstance(data, bytes):
                data = json.dumps(data).encode()
            self.send_response(code)
            self.send_header('Content-Type', content)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def route(self):
            parts = self.path.split('?', 1)[0].split('/')
            if len(parts) != 5 or parts[1] != 'v1' or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', parts[2]):
                raise ValueError('Expected /v1/username/manifest/current or /v1/username/file/path')
            return parts[2], parts[3], unquote(parts[4])

        def do_GET(self):
            try:
                user, kind, key = self.route()
                manifest = store.manifest(user)
                if kind == 'manifest' and key == 'current':
                    self.reply(200, manifest)
                elif kind == 'file' and key in manifest:
                    self.reply(200, (store.root / user / 'blobs' / manifest[key]).read_bytes(), 'application/octet-stream')
                else:
                    self.reply(404, {'error': 'Unknown file'})
            except ValueError as e:
                self.reply(400, {'error': str(e)})

        def do_PUT(self):
            try:
                user, kind, key = self.route()
                size = int(self.headers.get('Content-Length', '-1'))
                if kind != 'file' or not 0 <= size <= LIMIT:
                    raise ValueError('Invalid upload or file exceeds 128 MiB')
                data = self.rfile.read(size)
                if len(data) != size:
                    raise ValueError('Incomplete upload')
                ok, digest = store.put(user, key, self.headers.get('If-Match', ''), data)
                self.reply(200 if ok else 409, {'sha256': digest})
            except ValueError as e:
                self.reply(400, {'error': str(e)})
    return Handler

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--data', type=Path, default=Path('workspace/player-sync'))
    args = parser.parse_args()
    print(f'Player sync listening on {args.bind}:{args.port}', flush=True)
    ThreadingHTTPServer((args.bind, args.port), handler(Store(args.data))).serve_forever()
