#!/usr/bin/env python3
"""Upload a complete Windows content installer to the configured DigitalOcean Space."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

BUCKET = 'maclauncher'
ENDPOINT = 'https://sfo3.digitaloceanspaces.com'
BASE_URL = 'https://maclauncher.sfo3.digitaloceanspaces.com'
FILES = ('OpenBFME-content.tar', 'OpenBFME-Setup.exe', 'README.txt')


def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): result.update(block)
    return result.hexdigest()


def publish(folder, release=None, profile=None, public=False, aws='aws', keep_local=False):
    bucket = os.environ.get('OPENBFME_CDN_BUCKET', BUCKET)
    endpoint = os.environ.get('OPENBFME_CDN_ENDPOINT', ENDPOINT)
    base_url = os.environ.get('OPENBFME_CDN_URL', BASE_URL).rstrip('/')
    if not endpoint.startswith('https://') or not base_url.startswith('https://'): raise ValueError('CDN URLs must use HTTPS')
    folder = Path(folder).resolve()
    release = release or os.environ.get('OPENBFME_CDN_RELEASE') or datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,99}', release): raise ValueError('Invalid release folder name')
    rows = []
    for name in FILES:
        path = folder / name
        if not path.is_file(): raise ValueError(f'Installer is incomplete: missing {name}')
        rows.append({'name': name, 'bytes': path.stat().st_size, 'sha256': sha256(path)})
    # Check that the installer README describes this exact sidecar before uploading.
    if rows[0]['sha256'] not in (folder / 'README.txt').read_text():
        raise ValueError('Installer README does not match the content archive checksum')
    prefix = f'openbfme/windows-installers/{release}'
    base = [aws, '--endpoint-url', endpoint, '--region', 'us-east-1', '--no-cli-pager']
    profile = profile or os.environ.get('AWS_PROFILE')
    if profile: base += ['--profile', profile]
    def call(*args):
        return subprocess.run(base + list(args), check=True, capture_output=True, text=True).stdout
    # Listing errors (including missing credentials) stop before any upload.
    listing = json.loads(call('s3api', 'list-objects-v2', '--bucket', bucket, '--prefix', prefix + '/', '--max-keys', '1'))
    if listing.get('KeyCount', 0): raise ValueError('Remote release folder already exists; choose a new release name')
    for row in rows:
        key = prefix + '/' + row['name']
        args = ['s3', 'cp', str(folder / row['name']), f's3://{bucket}/{key}', '--only-show-errors',
                '--metadata', 'sha256=' + row['sha256'], '--cache-control', 'public,max-age=31536000,immutable']
        if public: args += ['--acl', 'public-read']
        print('Uploading', row['name'], flush=True)
        call(*args)
        remote = json.loads(call('s3api', 'head-object', '--bucket', bucket, '--key', key))
        if remote['ContentLength'] != row['bytes'] or remote.get('Metadata', {}).get('sha256') != row['sha256']:
            raise ValueError('Remote size/checksum metadata verification failed: ' + row['name'])
    manifest = {'version': 1, 'release': release, 'public': public, 'files': rows,
                'base_url': base_url + '/' + quote(prefix, safe='/')}
    receipt = folder / 'cdn-upload.json'
    receipt.write_text(json.dumps(manifest, indent=2) + '\n')
    receipt_hash = sha256(receipt)
    # Completion marker goes last: partial uploads never look like complete releases.
    args = ['s3', 'cp', str(receipt), f's3://{bucket}/{prefix}/manifest.json', '--only-show-errors', '--content-type', 'application/json', '--metadata', 'sha256=' + receipt_hash]
    if public: args += ['--acl', 'public-read']
    call(*args)
    remote = json.loads(call('s3api', 'head-object', '--bucket', bucket, '--key', prefix + '/manifest.json'))
    if remote['ContentLength'] != receipt.stat().st_size or remote.get('Metadata', {}).get('sha256') != receipt_hash:
        raise ValueError('Remote completion manifest verification failed; local files retained')
    if not keep_local:
        cleanup_local(folder, manifest)
    print('Uploaded:', manifest['base_url'] + '/OpenBFME-Setup.exe')
    return manifest


def cleanup_local(folder, manifest):
    """Delete only the three unchanged uploaded artifacts; retain receipt and other files."""
    folder = Path(folder)
    rows = manifest['files']
    if {row['name'] for row in rows} != set(FILES) or len(rows) != len(FILES):
        raise ValueError('Invalid upload inventory; local files retained')
    for row in rows:
        path = folder / row['name']
        if path.is_symlink() or path.stat().st_size != row['bytes'] or sha256(path) != row['sha256']:
            raise ValueError('Local artifact changed after upload; local files retained')
    for row in rows: (folder / row['name']).unlink()
    print('Deleted local installer artifacts; retained cdn-upload.json and build cache.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--keep-local', action='store_true', help='Retain local installer artifacts after verified upload')
    parser.add_argument('--release')
    parser.add_argument('--profile', help='AWS CLI profile holding Spaces credentials')
    parser.add_argument('--public', action='store_true', help='Make installer and included game assets publicly downloadable')
    args = parser.parse_args()
    publish(args.folder, args.release, args.profile, args.public, keep_local=args.keep_local)


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError):
            raise SystemExit('CDN upload failed. Check Spaces credentials, bucket permissions, and network access; existing uploads were retained.')
        raise SystemExit(str(error))
