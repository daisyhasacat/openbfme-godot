# OpenBFME online server

A Linux server for the game's existing Online mode. `server.mjs` runs the existing HTTP lobby and supervises the C++ UDP relay as one service. It generates persistent signing keys on first launch, saves rooms and replay records to disk, uploads finished relay logs automatically, and stops accepting lobby traffic if its relay exits.

No client protocol changes or game assets are required on the server. The lobby uses the version 1 API consumed by `OnlineLobbyClient` and `OnlineStart`; the relay uses version 3 of `RelayProtocol`, including reconnect and spectator tickets. The deterministic game simulation runs on the clients. A server simulation verifier is still future work.

## Run locally

Requirements: Linux, Node.js 18.2 or newer, CMake 3.20 or newer, and a C++20 compiler. There are no npm dependencies.

From the repository root:

```bash
services/online/run-local.sh
```

This builds only the standalone relay, starts HTTP on `127.0.0.1:8787` and UDP on `127.0.0.1:7350`, and stores state in `services/online/.state/`. Both the state and build directories are ignored by Git. `CMAKE=/path/to/cmake` selects a different CMake executable.

Point each local game client at the service:

```bash
godot --path godot -- --online-lobby=http://127.0.0.1:8787
```

For a packaged game, pass `--online-lobby=http://127.0.0.1:8787` to its executable. Open Online, sign in as a guest, create or join a room, and ready/start normally. Players must have matching game profiles and map data. The existing peace-time option is carried in the lobby setup and START unchanged.

Check the service:

```bash
curl http://127.0.0.1:8787/healthz
curl http://127.0.0.1:8787/v1/health
```

`/healthz` reports the supervised process state and the last upload batch. The existing `/v1/health` remains available to clients and monitoring. Neither response contains signing keys or session tokens.

## Configure a host

After the relay has been built, start the program directly:

```bash
node services/online/server.mjs \
  --data /var/lib/openbfme-online \
  --http 127.0.0.1:8787 \
  --relay-bind 0.0.0.0:7350 \
  --relay-public 203.0.113.10:7350 \
  --public-url https://lobby.example.com \
  --motd "Welcome to OpenBFME"
```

Replace the example relay address and URL with the host's reachable IPv4 address and HTTPS origin. Put an HTTPS reverse proxy in front of the HTTP listener, forward `/v1/*`, `/j/*` and `/healthz`, and allow UDP traffic to the relay's advertised port. Long polls can last 25 seconds, so the proxy's response timeout must exceed that. For a LAN-only host, HTTP and the host's LAN IPv4 address can be used instead.

`--relay-bin FILE` selects a separately built relay; the default is `services/online/.build/relay/openbfme_relay`. `--upload-ms N` sets the finished-log scan interval (default 15000). Run `node services/online/server.mjs --help` for all options. Addresses currently require numeric IPv4, matching the relay and client START bindings.

The chosen data directory must be writable by the service user. Run one instance per directory. A process manager such as systemd can restart the entire program on failure; keep its default control-group termination so both Node and its relay are stopped together. No hosting or deployment is performed by these commands until run on the chosen host.

## Online web interface

Open the lobby’s root URL in a browser, for example `http://127.0.0.1:8787/` after starting the local server. `/online/` opens the same interface. It is served by both the Node lobby and the Cloudflare Worker, with no frontend build or npm dependencies. When using a reverse proxy, forward `/`, `/online/`, `/web/*`, `/v1/*` and `/healthz` to the lobby.

The public dashboard lists hosted rooms and unfinished games in progress, with host, map, roster, open slots and password status. Search and status filters narrow the list. Join links launch the existing `openbfme://join/<game>` handler. The players page shows display names and estimated presence from activity in the last 150 seconds or unfinished matches (up to three hours). It exposes no account IDs, session tokens, relay tickets, IP addresses or game passwords. The page refreshes every ten seconds while visible.

For **User management**, connect using the hexadecimal admin key from `<data-directory>/secrets/admin.key` (the local default is `services/online/.state/secrets/admin.key`). Standalone lobby/Worker deployments use their existing `ADMIN_KEY` setting. Use HTTPS or localhost: browser request signing requires WebCrypto. The key is imported into memory for this tab only; it is never stored in browser storage or sent as a raw key. Disconnect clears it. This is the same powerful operator key used for backups; do not share it with ordinary players.

Administrators can search and page through the account directory, rename an account, suspend or restore it, and revoke its existing lobby sessions. Suspension and revocation remove users from waiting rooms (closing a room if its host is removed). They prevent further authenticated lobby actions but cannot disconnect an already established UDP relay connection. Rename updates waiting rooms; running matches keep their original START identities. Revision checks reject stale or repeated moderation writes.

The current identity system is guest-only. A suspended guest can create a new identity; these controls are account-level, not IP bans or registered-user authentication. Restoring an account does not revive revoked tokens. Account metadata and moderation survive restarts; live presence is reconstructed from new activity and unfinished matches. Existing valid guest tokens are adopted into the directory when first seen. The directory is stored in Node’s `lobby/meta/` or Worker Durable Object `user:` records; retain it with the signing keys when backing up.

API additions: public `GET /v1/dashboard`; signed-admin `GET /v1/admin/users?search=&limit=50&after=`; signed-admin `POST /v1/admin/users/<encoded-account-id>` with `{action, revision, name?}`. Actions are `rename`, `ban`, `unban` and `revoke`. Existing game-client APIs remain compatible.

## Persistence and shutdown

The data directory contains:

| Path | Contents |
|---|---|
| `secrets/` | Shared relay ticket keys, guest-session key, replay-upload key, admin key; files mode 600, directory mode 700 |
| `lobby/` | Rooms, account directory and moderation, public replay START records, player ownership records, uploaded command logs |
| `relay-logs/` | Active `.part` logs and finished logs awaiting upload |
| `relay-logs/uploaded/` | Successfully uploaded local log copies |
| `relay-status.json` | Relay counters |
| `server.lock` | Prevents concurrent instances using the same state |

Back up the whole data directory together. Keys are reused on restart, preserving saved guest sessions. Once initialized, a missing key is an error requiring restoration; the server does not silently replace it. Keep this directory private. Uploaded local copies are retained, so storage retention needs to be managed by the operator.

SIGINT/Ctrl-C and SIGTERM stop new game starts, ask the relay to finalize logs, make one bounded final upload attempt, release held lobby polls, and close HTTP. Failed uploads stay on disk for retry. An unexpected relay exit shuts down the lobby and returns exit code 1. If the whole program is killed without cleanup, verify its old Node and relay processes are stopped before removing the stale `server.lock` and restarting. Restarting this first version does not guarantee that an interrupted running match can resume; it preserves the lobby records and logs.

## Tests

Build the relay and run the suite:

```bash
cmake -S services/online/relay -B services/online/.build/relay
cmake --build services/online/.build/relay --parallel 2
npm --prefix services/online test
```

`OPENBFME_RELAY_BIN=/path/to/openbfme_relay` can select another build for the integration tests. The server tests use real HTTP and UDP sockets: guest sign-in, profile and slot setup, peace time in START, relay HELLO/WELCOME, forwarding a transport frame, refusing slot takeover, replay upload/download, restart persistence, lock exclusion, startup failures and relay failure shutdown. They require no retail game files. A live game through the graphical client and an internet/NAT deployment remain separate acceptance checks.


### Lua authority

The lobby stamps `setup.luaAuthoritySlot` from the actual host slot in every START, spectator/rejoin setup and archived replay START. The host runs gameplay Lua; resulting bounded typed actions travel through the command relay and command log. Clients check their stamped sender against that fixed authority. Peers and replays apply actions through the portable engine adapter without loading host scripts.

The lobby/relay currently has no world simulation or Lua VM. It transports and records commands; it does not take over Lua on host disconnect. A future dedicated simulation server can use the same portable executor, but needs world loading, ticking and an authenticated server authority assignment first. Do not execute uploaded scripts in the lobby process.
