#!/usr/bin/env python3
"""Unauthenticated LAN mod/map catalogue. Python standard library only."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import socket
import threading
import zipfile

PROGRAMS = {'.exe', '.dll', '.dylib', '.so', '.bat', '.cmd', '.sh', '.com', '.scr'}
BUILTINS = {'wall_towers.big', 'rotwk_202_maps.big'}

def pack_files(root, kind):
    if kind == 'edain':
        return [(p.name, p) for p in sorted(root.iterdir())
                if p.is_file() and not p.is_symlink() and p.suffix.lower() == '.big'
                and (p.name.startswith('_') or p.name.startswith('!'))
                and not p.name.startswith('!!!bfme-macos-')]
    if kind != 'aotr':
        return files(root)
    dependency = root / 'rotwk/#aotr_patch202.big'
    if not dependency.is_file():
        raise ValueError(f'AOTR dependency missing: {dependency}')
    return [('aotr/' + name, path) for name, path in files(root / 'aotr')] + [('rotwk/#aotr_patch202.big', dependency)]


def files(root):
    if root.is_file():
        return [(root.name, root)] if root.suffix.lower() not in PROGRAMS and not root.is_symlink() else []
    found = []
    for directory, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if not d.startswith('.') and not (Path(directory) / d).is_symlink())
        for name in sorted(names):
            path = Path(directory) / name
            if not name.startswith('.') and not path.is_symlink() and path.suffix.lower() not in PROGRAMS:
                found.append((path.relative_to(root).as_posix(), path))
    return found


def game_root(path):
    if path.is_file():
        return path if path.suffix.lower() == '.big' else None
    if not path.is_dir():
        return None
    children = list(path.iterdir())
    if any(p.name.lower() in {'data', 'art', 'maps', 'apt', 'libraries'} and p.is_dir() or p.suffix.lower() == '.big' for p in children):
        return path
    candidates = [p for p in children if p.is_dir() and not p.is_symlink() and any(c.name.lower() in {'data', 'art', 'maps', 'apt', 'libraries'} or c.suffix.lower() == '.big' for c in p.iterdir())]
    return candidates[0] if len(candidates) == 1 else None


class Catalogue:
    def __init__(self, cache):
        self.cache = cache
        self.packs = {}
        self.locks = {}

    def add(self, root, kind='mod', textures=False, game='bfme2', entry=''):
        root = Path(root).expanduser().resolve()
        if not root.exists():
            raise ValueError(f'Missing content: {root}')
        if kind == 'builtin' and any(p['kind'] == 'builtin' and p['name'] == root.stem for p in self.packs.values()):
            return
        key = hashlib.sha256(f'{kind}:{game}:{root}:{entry}'.encode()).hexdigest()
        listing = pack_files(root, kind)
        if not listing:
            return
        self.packs[key] = {'id': key, 'name': root.stem if root.is_file() else root.name,
                           'kind': 'mod' if kind == 'edain' else kind, 'game': game, 'textures_only': textures,
                           'bytes': sum(p.stat().st_size for _, p in listing), 'entry': entry,
                           '_root': root, '_kind': kind}
        self.locks[key] = threading.Lock()

    def public(self):
        return [{k: v for k, v in p.items() if not k.startswith('_')} for p in self.packs.values()]

    def prepare(self, key):
        with self.locks[key]:
            pack = self.packs[key]
            listing = pack_files(pack['_root'], pack['_kind'])
            stamps = [(name, p.stat().st_size, p.stat().st_mtime_ns) for name, p in listing]
            version = hashlib.sha256(('stored-v1:' + json.dumps(stamps)).encode()).hexdigest()
            target = self.cache / f'{key}-{version}.zip'
            self.cache.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                partial = target.with_suffix('.partial')
                try:
                    # Stored entries let the game stream extraction on memory-constrained devices.
                    with zipfile.ZipFile(partial, 'w', zipfile.ZIP_STORED, allowZip64=True) as z:
                        for name, path in listing:
                            z.write(path, name)
                    after = [(name, p.stat().st_size, p.stat().st_mtime_ns) for name, p in pack_files(pack['_root'], pack['_kind'])]
                    if after != stamps:
                        raise ValueError('Content changed while packing; retry when the files are idle.')
                    partial.replace(target)
                finally:
                    partial.unlink(missing_ok=True)
            with target.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            pack['_file'] = target
            return {'id': key, 'sha256': digest, 'bytes': target.stat().st_size,
                    'download': f'/files/{key}.zip'}


def handler(catalogue):
    class Handler(http.server.BaseHTTPRequestHandler):
        def json(self, value, status=200):
            body = json.dumps(value).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            try:
                if self.path == '/catalog.json':
                    return self.json({'version': 1, 'packs': catalogue.public()})
                match = re.fullmatch(r'/packs/([a-f0-9]{64})', self.path)
                if match and match[1] in catalogue.packs:
                    return self.json(catalogue.prepare(match[1]))
                match = re.fullmatch(r'/files/([a-f0-9]{64})\.zip', self.path)
                if match and match[1] in catalogue.packs:
                    path = catalogue.packs[match[1]].get('_file')
                    if path is None:
                        return self.json({'error': 'Prepare this pack first.'}, 409)
                    with path.open('rb') as source:
                        self.send_response(200)
                        self.send_header('Content-Type', 'application/zip')
                        self.send_header('Content-Length', str(path.stat().st_size))
                        self.end_headers()
                        while chunk := source.read(1024 * 1024):
                            self.wfile.write(chunk)
                    return
                self.json({'error': 'Not found'}, 404)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except Exception as error:
                self.json({'error': str(error)}, 500)
    return Handler


def discover(catalogue, user, repo, maps):
    textures = set()
    config = user / 'mods.cfg'
    if config.exists():
        text = config.read_text()
        for line in text.splitlines():
            if line.startswith('textures_only='):
                textures.update(re.findall(r'"([^"\n]+)"', line))
        for line in text.splitlines():
            if line.startswith(('order=', 'enabled=')):
                for path in re.findall(r'"([^"\n]+)"', line):
                    if Path(path).exists():
                        catalogue.add(path, textures=path in textures)
    for parent in (user / 'mods', repo / 'godot' / 'mods'):
        if parent.is_dir():
            for path in sorted(parent.iterdir()):
                root = game_root(path)
                if root:
                    catalogue.add(root, 'builtin' if root.name in BUILTINS else 'mod', str(root) in textures)
    for root, game in maps:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*.map')):
            if not path.is_symlink():
                catalogue.add(path.parent, 'map', game=game, entry=path.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8765)
    default_user = Path.home() / 'Library/Application Support/Godot/app_userdata/OpenBFME'
    if os.sys.platform == 'win32':
        default_user = Path(os.environ.get('APPDATA', Path.home())) / 'Godot/app_userdata/OpenBFME'
    elif os.sys.platform != 'darwin':
        default_user = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'godot/app_userdata/OpenBFME'
    parser.add_argument('--user-dir', type=Path, default=default_user)
    parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/openbfme-lan')
    parser.add_argument('--mod', action='append', type=Path, default=[], help='Additional mod root or BIG (repeatable)')
    parser.add_argument('--textures', action='append', type=Path, default=[], help='Additional texture-only mod (repeatable)')
    parser.add_argument('--maps', action='append', type=Path, default=[], help='Additional RotWK map library (repeatable)')
    args = parser.parse_args()
    catalogue = Catalogue(args.cache)
    library = Path.home() / 'Library/Application Support/BFMELauncher/Maps/Library'
    discover(catalogue, args.user_dir, Path(__file__).resolve().parents[2],
             [(library / 'rotwk', 'bfme2'), (library / 'bfme1', 'bfme1'),
              (args.user_dir / 'installed-maps', 'bfme2'), (args.user_dir / 'bfme1-installed-maps', 'bfme1')]
             + [(p, 'bfme2') for p in args.maps])
    for p in args.mod:
        catalogue.add(p)
    for p in args.textures:
        catalogue.add(p, textures=True)
    launcher = Path.home() / 'Library/Application Support/BFMELauncher/Installation/Mods'
    aotr = launcher / 'AgeOfTheRing/CrossOverPrefix/drive_c/AotR'
    if (aotr / 'aotr/AOTRSkirmish.big').is_file():
        catalogue.add(aotr, kind='aotr', game='aotr')
    edain = launcher / 'Edain/RotWKCrossOverPrefix/drive_c/BFME/RotWK'
    if (edain / '__edain_data.big').is_file():
        catalogue.add(edain, kind='edain')
        next(p for p in catalogue.packs.values() if p['_kind'] == 'edain')['name'] = 'Edain'
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler(catalogue))
    print(f'Sharing {len(catalogue.packs)} packs without authentication on port {server.server_port}.', flush=True)
    print('In game: Mods → LAN downloads. Stop with Ctrl+C. Archives are prepared on first download.', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
