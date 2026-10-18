#!/bin/bash
# Lane PLATFORMICON-1: a proof capture of the LAN staging room with the three icons. ONE windowed Godot client (the host, a Linux build here) and two headless
# openbfme_peer joiners that tell "windows" and "macos" (fake peers). Needs a display (DISPLAY, the Deck's :1) and the retail installs.
#   tools/platformicon/proof_lan.sh WIDTHxHEIGHT OUT.png [BUILD_DIR]
# Everything it starts is stopped by PID at the end; HOME is a throwaway so no user name or path is on screen.
set -u
RES=${1:?WIDTHxHEIGHT}; OUT=${2:?OUT.png}; BUILD=${3:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PEER=${BUILD:+$BUILD/openbfme_peer}; PEER=${PEER:-${OPENBFME_PEER:?set OPENBFME_PEER or pass the build dir}}
PORT=$((46000 + RANDOM % 2000))
HOMEDIR=$(mktemp -d)
pids=()
cleanup() { [ -n "${KEEP_LOG:-}" ] && cp "$HOMEDIR/host.log" "$KEEP_LOG" 2>/dev/null; for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done; wait 2>/dev/null; rm -rf "$HOMEDIR"; }
trap cleanup EXIT
export HOME=$HOMEDIR
cd "$ROOT/godot" || exit 2
godot --path . -- --lan=host --lan-players=3 --lan-ai=1 --lan-targets=127.0.0.1 --lan-port-base=$PORT --lan-shot="$OUT" ${PLAYERS_SHOT:+--net-players-shot="$PLAYERS_SHOT"} ${SCORE_SHOT:+--net-score-shot="$SCORE_SHOT"} --res="$RES" --no-intro --no-update-check \
  > "$HOMEDIR/host.log" 2>&1 &
pids+=($!)
sleep 25
for who in "Alice:windows" "Bob:macos"; do
  "$PEER" --rotwk "$ROTWK_INSTALL" --bfme2 "$BFME2_INSTALL" --lan-join --name "${who%%:*}" --platform "${who##*:}" --lan-port-base $PORT --lan-targets 127.0.0.1 \
    > "$HOMEDIR/${who%%:*}.log" 2>&1 &
  pids+=($!)
done
for _ in $(seq 1 120); do [ -s "$OUT" ] && break; sleep 1; done
if [ -n "${PLAYERS_SHOT:-}" ]; then # the running game's players screen (Tab) at a logic frame: PLAYERS_SHOT=FILE:FRAME
  for _ in $(seq 1 300); do [ -s "${PLAYERS_SHOT%:*}" ] && break; sleep 1; done
  [ -s "${PLAYERS_SHOT%:*}" ] && echo "captured ${PLAYERS_SHOT%:*}" || { echo "no players capture"; tail -20 "$HOMEDIR/host.log"; exit 1; }
fi
if [ -n "${SCORE_SHOT:-}" ]; then # the score screen after the host forfeits: SCORE_SHOT=FILE:FRAME
  for _ in $(seq 1 400); do [ -s "${SCORE_SHOT%:*}" ] && break; sleep 1; done
  [ -s "${SCORE_SHOT%:*}" ] && echo "captured ${SCORE_SHOT%:*}" || { echo "no score capture"; tail -20 "$HOMEDIR/host.log"; exit 1; }
fi
sleep 2
[ -s "$OUT" ] && echo "captured $OUT" || { echo "no capture"; tail -20 "$HOMEDIR/host.log"; exit 1; }
