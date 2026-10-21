#!/usr/bin/env python3
"""Control unattended test builds through the server's signed administrator API."""
import argparse
import json
import shlex
import subprocess

# Runs as the server service account. Signing keys never leave the host.
REMOTE = '''import sys,json,time,hmac,hashlib,urllib.request,urllib.error
from pathlib import Path
request=json.load(sys.stdin)
body=json.dumps(request["body"],separators=(",",":")) if request["body"] is not None else ""
method="POST" if body else "GET"
path="/v1/admin/testing"
stamp=str(int(time.time()))
key=bytes.fromhex(Path("/var/lib/openbfme-online/secrets/admin.key").read_text().strip())
signature=hmac.new(key,(method+" "+path+"?\\n"+stamp+"\\n"+body).encode(),hashlib.sha256).hexdigest()
req=urllib.request.Request("http://127.0.0.1:8787"+path,data=body.encode() if body else None,method=method,headers={"Content-Type":"application/json","X-OpenBFME-Admin":stamp+"."+signature})
try:
 with urllib.request.urlopen(req,timeout=15) as response: print(response.read().decode())
except urllib.error.HTTPError as error:
 print(error.read().decode());sys.exit(1)
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='hailey@165.22.13.12')
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('list')
    run = commands.add_parser('run')
    run.add_argument('--clients', nargs=2, required=True, metavar=('HOST', 'JOINER'))
    run.add_argument('--map', required=True)
    run.add_argument('--frames', type=int, default=1500)
    run.add_argument('--timeout-seconds', type=int, default=600)
    commands.add_parser('cancel').add_argument('run')
    args = parser.parse_args()
    body = None if args.action == 'list' else {'action': args.action}
    if args.action == 'run':
        body.update(clients=args.clients, map=args.map, frames=args.frames, timeoutSeconds=args.timeout_seconds)
    elif args.action == 'cancel':
        body['run'] = args.run
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', args.host,
                             'sudo -u openbfme-online python3 -c ' + shlex.quote(REMOTE)],
                            input=json.dumps({'body': body}), capture_output=True, text=True, timeout=30)
    if result.stdout:
        try: print(json.dumps(json.loads(result.stdout), indent=2))
        except json.JSONDecodeError: print(result.stdout)
    if result.stderr: print(result.stderr)
    raise SystemExit(result.returncode)

if __name__ == '__main__':
    main()
