#!/bin/bash
# Lane ONLINEHUB-1: Quick Match end to end on a CI host (no display: headless clients): a local lobby (services/online/lobby/src/server.mjs), a
# relay (openbfme_relay) and the platform's fake (tools/platform_fake/fake_server.py), then two game clients that sign in, open the TOURNAMENT page,
# choose an army and press JOIN GAME (game.gd --online-walk=qm). The platform matches them; the match's host makes the private room by its code, the
# other joins it, both ready, the host starts, and both play the game through the relay for N logic frames.
# Needs Node.js beside the game (the CI hosts have none: the doctest "onlinehub1 retail e2e" plays the same match with two openbfme_peer
# processes wherever Node.js is). Pass: both clients reach the START and play every frame, their state hashes agree at every printed frame, no desync.
#   tools/net/onlinehub1_qm.sh <out dir> [frames]      (from the worktree; RELAY=<openbfme_relay>, default $B/relay/openbfme_relay)
set -u
WT=$(pwd)
OUT=${1:?usage: onlinehub1_qm.sh <out dir> [frames]}
FRAMES=${2:-150}
RELAY=${RELAY:-${B:-}/relay/openbfme_relay}
GODOT=${GODOT:-godot}
mkdir -p "$OUT"
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM); FPORT=$(port SOCK_STREAM); RPORT=$(port SOCK_DGRAM)
D=$(mktemp -d /tmp/openbfme-hub1-qm.XXXX)
mkdir -p "$D/data" "$D/home-a" "$D/home-b" "$D/relaylog"
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' $KEY > "$D/keys.txt"
"$RELAY" --bind 127.0.0.1:$RPORT --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 120 > "$OUT/relay.log" 2>&1 & RPID=$!
LOBBY_BIND=127.0.0.1:$LPORT LOBBY_DATA="$D/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="local=127.0.0.1:$RPORT" \
  node "$WT/services/online/lobby/src/server.mjs" > "$OUT/lobby.log" 2>&1 & LPID=$!
python3 -S "$WT/tools/platform_fake/fake_server.py" --admin-token qm-e2e --port $FPORT > "$OUT/platform.log" 2>&1 & FPID=$!
sleep 2
cd "$WT/godot"
COMMON="--online-lobby=http://127.0.0.1:$LPORT --platform-url=http://127.0.0.1:$FPORT/v1 --net-frames=$FRAMES --net-script --movies=off --res=1024x768"
HOME="$D/home-a" XDG_DATA_HOME="$D/home-a/.local/share" timeout 900 "$GODOT" --headless --audio-driver Dummy --path . -- --online-walk=qm --online-name=Elrond \
  --online-qm-side=1 $COMMON > "$OUT/a.log" 2>&1 & AP=$!
sleep 8
HOME="$D/home-b" XDG_DATA_HOME="$D/home-b/.local/share" timeout 900 "$GODOT" --headless --audio-driver Dummy --path . -- --online-walk=qm --online-name=Gothmog \
  --online-qm-side=0 $COMMON > "$OUT/b.log" 2>&1 & BP=$!
wait $AP; AR=$?; wait $BP; BR=$?
kill $LPID $RPID $FPID 2>/dev/null; wait 2>/dev/null
curl -s "http://127.0.0.1:$FPORT/__test/log" > "$OUT/platform-log.json" 2>/dev/null || true
rm -rf "$D"
grep -h 'GAME ONLINE WALK\|GAME NET frame\|desync' "$OUT/a.log" | sed 's/^/A: /' | head -40
grep -h 'GAME ONLINE WALK\|GAME NET frame\|desync' "$OUT/b.log" | sed 's/^/B: /' | head -40
ok=1
for c in a b; do
  grep -q 'GAME ONLINE WALK QM: the START arrived' "$OUT/$c.log" || { echo "QM E2E: client $c never got the START"; ok=0; }
  grep -q "GAME NET frame $FRAMES " "$OUT/$c.log" || grep -q "GAME NET status: .*\"frame\":$FRAMES" "$OUT/$c.log" || { echo "QM E2E: client $c did not play $FRAMES frames"; ok=0; }
done
HA=$(grep -o 'GAME NET frame [0-9]* hash [0-9A-F]*' "$OUT/a.log" | sort -u); HB=$(grep -o 'GAME NET frame [0-9]* hash [0-9A-F]*' "$OUT/b.log" | sort -u)
[ -n "$HA" ] && [ "$HA" = "$HB" ] || { echo "QM E2E: the clients' hashes differ"; diff <(echo "$HA") <(echo "$HB") | head; ok=0; }
grep -q 'desyncs [1-9]' "$OUT/a.log" "$OUT/b.log" && { echo "QM E2E: desync"; ok=0; }
echo "client exits: $AR $BR"
if [ $ok = 1 ]; then echo "QM E2E PASS ($(echo "$HA" | wc -l) equal hashes)"; else echo "QM E2E FAIL"; exit 1; fi
