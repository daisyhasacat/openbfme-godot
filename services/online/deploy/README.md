# Second Age online deployment

Host: `hailey@165.22.13.12` (Ubuntu 24.04). Dashboard:
`https://secondage.dev/online/`. Client lobby origin: `https://secondage.dev`.
The UDP relay advertises `165.22.13.12:7350`; this must remain directly
reachable over UDP, independently of Cloudflare's HTTPS proxy.

The existing secondage.dev website, images, videos, accounts and exact
`/v1/sync` route remain in their original nginx server block. Only the paths
in `nginx-locations.conf` are added. The HTTP lobby binds to loopback, behind
nginx TLS; its port 8787 is not exposed publicly.

## Installed layout

- `/opt/openbfme-online/releases/20261020-0ef0a1b5d0/`: server source, tests and
  Linux relay build, based on commit `0ef0a1b5d0` plus this deployment change.
  No game assets or development-machine state are uploaded.
- `/opt/openbfme-online/current`: active release symlink, owned by root.
- `/opt/openbfme-online/node`: dedicated Node 24 LTS runtime. Initial version
  24.22.0, downloaded from nodejs.org and verified against its HTTPS-published
  SHA-256 checksum. Other applications' runtime paths are unchanged.
- `/var/lib/openbfme-online/`: persistent state and signing keys, owned by
  the unprivileged `openbfme-online` system user. Directory permissions 0700,
  key permissions 0600. Retain this entire directory with private backups.
- `/etc/systemd/system/openbfme-online.service`: enabled at boot, restarts
  on failure. The supervisor owns and stops both the lobby and relay.
- `/etc/nginx/snippets/openbfme-online-{locations,proxy,realip}.conf`:
  proxy configuration. The realip file contains only validated networks
  from `https://api.cloudflare.com/client/v4/ips` and uses
  `real_ip_header CF-Connecting-IP`. Refresh when Cloudflare changes ranges.
  Headers from callers outside those networks cannot set the client IP.
- `/etc/nginx/sites-available/secondage.before-openbfme-20261020`: original
  site configuration backup from before installing the include.

The service uses `--trust-proxy loopback`: Node honors a single valid
`X-Real-IP` address only when the immediate peer is loopback. Nginx overwrites
that header with its verified client address. Never enable this option for
an untrusted local proxy. Without it all proxied players would share rate
limits. Normal standalone deployments ignore forwarded headers.

## Operation

```sh
ssh hailey@165.22.13.12 'sudo systemctl status openbfme-online --no-pager'
ssh hailey@165.22.13.12 'sudo journalctl -u openbfme-online -n 100 --no-pager'
curl --fail https://secondage.dev/healthz
curl --fail https://secondage.dev/v1/health
```

Retrieve the administrator key privately over SSH, then enter it in
**User management** at the HTTPS dashboard:

```sh
ssh hailey@165.22.13.12 'sudo cat /var/lib/openbfme-online/secrets/admin.key'
```

Do not paste this key into tickets, chat, deployment manifests or Git.
It grants full server administration and replay-blob backup access.
The deployment creates independent production keys and accounts; it does
not migrate the development machine's guest sessions or history.

For updates, upload only the required source/config files into a new,
root-owned release directory, build the relay with CMake, and run the tests
with the dedicated runtime before switching `current`. The relative script
path in `ExecStart` is intentional: Node's ESM entry-point check must resolve
through the service's working directory, rather than a symlink in argv.
Keep state outside releases. Restarting interrupts active relay matches;
choose a maintenance window once players are using the server.

```sh
cd /opt/openbfme-online/current
/opt/openbfme-online/node/bin/node --test services/online/lobby/test/*.test.mjs services/online/test/*.test.mjs
sudo nginx -t
sudo systemd-analyze verify /etc/systemd/system/openbfme-online.service
sudo systemctl restart openbfme-online
```

Initial validation: all 85 tests passed on Linux/Node 24. Two external UDP
clients completed the HTTPS guest/create/join/ready/start flow, received
WELCOME packets, forwarded a valid frame, sent BYE, and downloaded the
uploaded replay. Completed history survived a service restart, signed admin
requests succeeded, and unsigned admin requests were refused. The retained
`Internet deployment verification` record is a synthetic test, not a retail
match or a simulation-correctness result. Full graphical cross-machine play
is a separate acceptance check. No off-host backup destination is configured.
