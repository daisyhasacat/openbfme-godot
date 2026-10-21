#!/usr/bin/env python3
"""Exercise the real Godot statistics uploader against isolated HTTP fixtures."""
import argparse
import http.server
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', default=os.environ.get('GODOT', 'godot'))
    args = parser.parse_args()
    seen = []
    authorizations = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', '0')))
            seen.append(self.path)
            authorizations.append(self.headers.get('Authorization'))
            code = 302 if self.path.startswith('/redirect/') else 503 if len(seen) == 1 else 200
            body = json.dumps({'v': 1, 'stored': code == 200}).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            if code == 302:
                self.send_header('Location', '/trap')
            self.end_headers()
            self.wfile.write(body)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='openbfme-statistics-') as folder:
            env = dict(os.environ, REPORT_TEST_URL='http://127.0.0.1:' + str(server.server_port), REPORT_TEST_DIR=folder)
            result = subprocess.run([args.godot, '--headless', '--path', str(ROOT / 'godot'), '--script',
                                     'res://tests/match_statistics_test.gd'], env=env, capture_output=True, text=True, timeout=60)
            print(result.stdout, result.stderr)
            assert result.returncode == 0
            assert '/trap' not in seen
            assert '/diagnostics/v1/games/0123456789abcdef/desync' in seen
            assert len([p for p in seen if p.startswith('/retry/')]) == 2
            assert all(value == 'Bearer test-secret' for value in authorizations)
            if os.name == 'posix':
                for path in Path(folder).glob('*.json'):
                    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    print('Statistics transport: retry, persistence, opt-out, redirect isolation and permissions passed')


if __name__ == '__main__':
    main()
