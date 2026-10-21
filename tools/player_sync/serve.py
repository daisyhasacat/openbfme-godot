#!/usr/bin/env python3
"""Standalone, username-scoped OpenBFME player sync service (trusted LAN)."""
import argparse
import hashlib
import json
import re
import shutil
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

LIMIT = 128 * 1024 * 1024

def safe_path(value):
    return bool(value) and len(value) <= 1024 and all(p not in ('', '.', '..') for p in value.split('/')) and not any(c in value for c in '\\:\x00')

def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.part')
    temp.write_bytes(data)
    temp.replace(path)

class Store:
    """Per user: blobs/<sha256> (every version ever uploaded, never deleted), manifest.json (the current version of each file) and
    history.jsonl (one line per change: time, file, previous and new sha256). With `backup`, every change is mirrored into that folder as
    well (blobs, manifest, history) plus manifests/<UTC date>.json, the file list as it stood at the end of each day."""

    def __init__(self, root, backup=None):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.backup = Path(backup) if backup else None
        self.lock = threading.Lock()
        if self.backup:
            # bring the mirror up to date with whatever the data folder holds (a first run, or one without --backup)
            for folder in self.root.iterdir():
                if folder.is_dir() and re.fullmatch(r'[A-Za-z0-9_-]{1,64}', folder.name):
                    self._mirror(folder.name, None)

    def manifest(self, user):
        path = self.root / user / 'manifest.json'
        return json.loads(path.read_text()) if path.exists() else {}

    def history(self, user, name=None):
        path = self.root / user / 'history.jsonl'
        entries = [json.loads(line) for line in path.read_text().splitlines() if line.strip()] if path.exists() else []
        return [e for e in entries if name is None or e['file'] == name]

    def put(self, user, name, base, data):
        if not safe_path(name):
            raise ValueError('Invalid file name')
        digest = hashlib.sha256(data).hexdigest()
        with self.lock:
            manifest = self.manifest(user)
            if manifest.get(name, '') != base:
                return False, manifest.get(name, '')
            blob = self.root / user / 'blobs' / digest
            if not blob.exists():
                write_atomic(blob, data)
            self._commit(user, manifest, name, digest)
            return True, digest

    def restore(self, user, name, digest):
        """Make an earlier version (any blob this user uploaded) the current one; recorded in the history like an upload."""
        if not safe_path(name) or not re.fullmatch(r'[0-9a-f]{64}', digest):
            raise ValueError('Invalid file name or sha256')
        with self.lock:
            if not (self.root / user / 'blobs' / digest).exists():
                raise ValueError('No stored version ' + digest + ' for ' + user)
            self._commit(user, self.manifest(user), name, digest)

    def _commit(self, user, manifest, name, digest):
        previous = manifest.get(name, '')
        manifest[name] = digest
        folder = self.root / user
        write_atomic(folder / 'manifest.json', json.dumps(manifest, sort_keys=True).encode())
        entry = {'time': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'file': name, 'previous': previous, 'sha256': digest}
        with open(folder / 'history.jsonl', 'a') as log:
            log.write(json.dumps(entry, sort_keys=True) + '\n')
        if self.backup:
            self._mirror(user, entry)

    def _mirror(self, user, entry):
        source = self.root / user
        target = self.backup / user
        (target / 'blobs').mkdir(parents=True, exist_ok=True)
        blobs = source / 'blobs'
        for blob in (blobs.iterdir() if entry is None and blobs.exists() else [blobs / entry['sha256']] if entry else []):
            if not blob.name.endswith('.part') and not (target / 'blobs' / blob.name).exists():
                write_atomic(target / 'blobs' / blob.name, blob.read_bytes())
        for name in ('manifest.json', 'history.jsonl'):
            if (source / name).exists():
                shutil.copyfile(source / name, target / (name + '.part'))
                (target / (name + '.part')).replace(target / name)
        if (source / 'manifest.json').exists():
            write_atomic(target / 'manifests' / (time.strftime('%Y-%m-%d', time.gmtime()) + '.json'), (source / 'manifest.json').read_bytes())

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
    parser.add_argument('--backup', type=Path, default=Path('workspace/player-sync-backup'),
                        help='mirror folder for every change (put it on another disk); "none" disables it')
    parser.add_argument('--history', nargs='+', metavar=('USER', 'FILE'), help='print the change history of a user (or one file) and exit')
    parser.add_argument('--restore', nargs=3, metavar=('USER', 'FILE', 'SHA256'), help='make an earlier version current and exit (stop the server first)')
    args = parser.parse_args()
    backup = None if str(args.backup).lower() == 'none' else args.backup
    if backup and backup.resolve() == args.data.resolve():
        parser.error('--backup must be a different folder from --data')
    store = Store(args.data, backup)
    if args.history:
        for entry in store.history(args.history[0], args.history[1] if len(args.history) > 1 else None):
            print(entry['time'], entry['file'], (entry['previous'] or '-')[:12], '->', entry['sha256'])
    elif args.restore:
        store.restore(*args.restore)
        print('Restored', args.restore[1], 'to', args.restore[2])
    else:
        print(f'Player sync listening on {args.bind}:{args.port}; backup: {backup or "off"}', flush=True)
        ThreadingHTTPServer((args.bind, args.port), handler(store)).serve_forever()
