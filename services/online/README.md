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

## Persistence and shutdown

The data directory contains:

| Path | Contents |
|---|---|
| `secrets/` | Shared relay ticket keys, guest-session key, replay-upload key, admin key; files mode 600, directory mode 700 |
| `lobby/` | Rooms, public replay START records, player ownership records, uploaded command logs |
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
