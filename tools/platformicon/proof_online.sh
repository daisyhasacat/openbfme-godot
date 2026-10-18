#!/bin/bash
# Lane PLATFORMICON-1: proof captures of the ONLINE screens on a local lobby (node services/online/lobby/src/server.mjs) with fake players
# (tools/platformicon/fake_online.py). ONE windowed Godot client; everything else is headless. Needs DISPLAY, node, the retail installs.
#   tools/platformicon/proof_online.sh host|join WIDTHxHEIGHT OUT_DIR BUILD_DIR
#   host: the client hosts; Alice (Windows) and Bob (macOS) join and say ready  -> online-staging-host-*.png, online-staging-start-refused-*.png (chat tab)
#   join: Hosty (Linux) hosts and Cleo (macOS) joins; the client opens the browser (the host's icon before the game's name), joins -> online-browser-*.png, staging
set -u
MODE=${1:?host|join}; RES=${2:?WxH}; OUT=${3:?dir}; BUILD=${4:?build dir}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d); pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done; wait 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
PORT=$((48000 + RANDOM % 2000)); mkdir -p "$WORK/data" "$OUT"
LOBBY_BIND=127.0.0.1:$PORT LOBBY_DATA=$WORK/data TICKET_KEYS="1:$(printf '%064d' 7)
" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="" node "$ROOT/services/online/lobby/src/server.mjs" > "$WORK/lobby.log" 2>&1 &
pids+=($!)
for _ in $(seq 1 50); do curl -s "http://127.0.0.1:$PORT/v1/health" >/dev/null && break; sleep 0.2; done
URL=http://127.0.0.1:$PORT
PROFILE=$("$BUILD/openbfme_peer" --rotwk "$ROTWK_INSTALL" --bfme2 "$BFME2_INSTALL" --print-profile | head -1)
export HOME=$WORK/home; mkdir -p "$HOME"
if [ "$MODE" = host ]; then
  ( sleep 40; python3 "$ROOT/tools/platformicon/fake_online.py" $URL "$PROFILE" join Alice windows 150 > "$WORK/alice.log" 2>&1 ) & pids+=($!)
  ( sleep 44; python3 "$ROOT/tools/platformicon/fake_online.py" $URL "$PROFILE" join Bob macos 150 > "$WORK/bob.log" 2>&1 ) & pids+=($!)
  ARGS=(--online-walk=host --online-players=3)
else
  python3 "$ROOT/tools/platformicon/fake_online.py" $URL "$PROFILE" host Hosty linux 150 > "$WORK/hosty.log" 2>&1 & pids+=($!)
  sleep 3
  python3 "$ROOT/tools/platformicon/fake_online.py" $URL "$PROFILE" join Cleo macos 150 > "$WORK/cleo.log" 2>&1 & pids+=($!)
  ARGS=(--online-walk=join)
fi
cd "$ROOT/godot" || exit 2
timeout 400 godot --path . -- --online-lobby=$URL --online-name=Tester "${ARGS[@]}" --online-shots="$OUT" --res="$RES" --no-intro --no-update-check > "$WORK/client.log" 2>&1
echo "client exit $?"; ls "$OUT" | head -30; grep -a "ONLINE WALK" "$WORK/client.log" | tail -5
