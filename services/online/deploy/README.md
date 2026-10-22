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

The passkey integration release is `/opt/openbfme-online/releases/20261020-passkey/`;
`current` points there. The original release remains available for rollback.

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

Open `https://admin.secondage.dev/online/` and sign in with the existing
administrator passkey. An active full administrator session connects automatically.
No new passkey enrollment or server-key copying is needed. Sign out revokes the
same session used by the original Second Age admin control panel.

`openbfme-online-admin.service` runs a separate loopback gateway on port 8791 as
`openbfme-online-admin`. Its systemd `LoadCredential` entries supply the lobby
signing key and the pinned authentication-service CA without giving the gateway
access to the persistent data directory or original TLS private key. It verifies
`https://127.0.0.1:54233/api/auth` with the admin hostname and pinned certificate;
only an unexpired full `admin` session may use management routes. Enrollment roles,
bypass mode, invalid CSRF, a wrong origin, or an unavailable authenticator fail
closed. It never forwards browser cookies or supplied signatures to the lobby.

Nginx's `openbfme-online-admin-{locations,proxy}.conf` snippets live only in the
admin.secondage.dev TLS server. Existing `/api/auth/*` and `/api/logout` still
use the original passkey service. Public `/v1/admin/*` on secondage.dev returns
404, including requests with a valid raw signing key. Server operators may still
use signed requests directly against `127.0.0.1:8787` over SSH for maintenance.
Do not paste signing keys into browser fields, tickets, chat, manifests or Git.
Production accounts and keys remain independent of development-machine state.

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

Initial server deployment validation: all 85 tests passed on Linux/Node 24. Two external UDP
clients completed the HTTPS guest/create/join/ready/start flow, received
WELCOME packets, forwarded a valid frame, sent BYE, and downloaded the
uploaded replay. Completed history survived a service restart, signed admin
requests succeeded, and unsigned admin requests were refused. The retained
`Internet deployment verification` record is a synthetic test, not a retail
match or a simulation-correctness result. Full graphical cross-machine play
is a separate acceptance check. No off-host backup destination is configured.


Passkey integration validation includes real gateway-to-lobby signing, full-session
and CSRF enforcement, enrollment/expiry/bypass rejection, authentication outages,
route allowlisting, key/cookie isolation, and public-API blocking. Completing the
production passkey ceremony requires the administrator's existing authenticator.
The integration does not enroll credentials, mint test production sessions, or
modify the existing passkey database.


Client statistics use `POST /v1/games/<id>/statistics` through the existing
public `/v1/` proxy, with a 512 KiB application limit. The admin gateway's route
allowlist includes `/v1/admin/users/<encoded-id>/statistics`. Preserve
`lobby/meta/match-statistics:*` and `match-report:*` records in full backups.
Legacy clients and previously uploaded relay logs do not supply these counters;
see [MATCH_STATISTICS.md](../../../docs/MATCH_STATISTICS.md) for client rollout,
result-sharing, confidence labels and counter coverage limits.

Desync diagnostics release: `/opt/openbfme-online/releases/20261020-desync-849c1458ed/`
is now the active `current` target; the previous `20261020-statistics` release
remains available for rollback. It contains server source from `849c1458ed` and
the existing relay build. All 99 server tests passed on the host before activation;
no open or running games were present at the restart. Afterwards the service was
active, `/v1/health` reported healthy, and an unauthenticated diagnostic submission
returned 401. Updated game clients submit `POST /v1/games/<id>/desync`; preserve
`lobby/meta/match-desync:*` records in backups. Reports are shown in the admin
match detail view. Mac and iOS client rebuilds and a physical-device desync
upload remain separate rollout checks.

Unattended testing release: `/opt/openbfme-online/releases/20261020-testing-3dee3c0dd6/`
is the active `current` target. Both the lobby/relay supervisor and passkey admin
gateway were restarted with no open or running games. All 102 server tests passed
on the host before activation; the prior desync release is retained for rollback.
The signed `tools/net/remote_testing.py list` command succeeds against the live
server. Unsigned client polls and unauthenticated gateway controls return 401.
Back up `testing-run:*` metadata along with match diagnostics.

Local end-to-end validation used two isolated Godot clients and a real local
lobby/UDP relay on Carrock: both automatically hosted/joined, readied, entered
lockstep and reached frame 310 with three matching CRC checks and 36/38 scripted
commands, then returned to the menu. Network-policy checks, 13 policy tests and
Godot control/reporting/sync checks passed. This validates the runner on two Mac
processes; a physical Mac/iPhone run requires rebuilt clients and remains a
separate check. Updated clients register automatically from the main menu when
foregrounded; no updated production clients were registered at deployment.
