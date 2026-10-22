#!/usr/bin/env python3
"""Atomically publish a verified AppImage into Second Age's static download directory."""
import argparse
import hashlib
import json
from pathlib import Path
import os
import shutil
import tempfile

DOWNLOAD_ROOT = 'https://secondage.dev/downloads/openbfme/linux-x86_64/'

def publish(image: Path, directory: Path, version: str, commit: str):
    with image.open('rb') as stream:
        header = stream.read(11)
        if header[:4] != b'\x7fELF' or header[8:11] != b'AI\x02':
            raise ValueError('not a type-2 AppImage')
        stream.seek(0)
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / (sha + '.AppImage')
    manifest = dict(schema=1, version=version, commit=commit, sha256=sha,
                    size=image.stat().st_size, url=DOWNLOAD_ROOT + target.name)
    # Both files are staged on the destination filesystem. Readers see either
    # the old complete manifest or the new one after its immutable image exists.
    with tempfile.TemporaryDirectory(prefix='.appimage-', dir=directory) as tmp:
        staged = Path(tmp) / target.name
        shutil.copyfile(image, staged)
        with staged.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != sha:
                raise ValueError('AppImage changed during publication')
        staged.chmod(0o644)
        os.replace(staged, target)
        latest = Path(tmp) / 'latest.json'
        latest.write_text(json.dumps(manifest, indent=2) + '\n')
        latest.chmod(0o644)
        os.replace(latest, directory / 'latest.json')
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    print(json.dumps(publish(args.image, args.directory, args.version, args.commit), indent=2))

if __name__ == '__main__': main()
