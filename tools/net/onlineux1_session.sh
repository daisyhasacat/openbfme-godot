#!/bin/bash
# Lane ONLINEUX-1: a real-use session of the ONLINE menus on a CI host: a local lobby (services/online/lobby/src/server.mjs under Node.js), a local
# relay (openbfme_relay), the platform API's fake (tools/platform_fake/fake_server.py) and N windowed game clients, each on its own X server (Xvfb,
# Mesa's llvmpipe: --rendering-driver opengl3), started as a PLAYER starts the game (no scripted-run option: the lobby and the platform come from
# OPENBFME_LOBBY / OPENBFME_PLATFORM). The session then runs command files the operator drops into $S/cmd (NNN.sh, run in order with the helpers below;
# output in NNN.out, captures in $S/shots) until a command file says `quit` or the session's time is up. Never touches a live service.
#   tools/net/onlineux1_session.sh <session dir> <clients> [minutes]      (from the worktree; needs XVFB=<xvfb.sh>, XDO=<dir with xdotool, node>, $B)
# Helpers for the command files (client c = 1..N):
#   click c X Y [button]   a real X pointer move + press / release at window pixel (X, Y)
#   dclick c X Y           a double click
#   typetext c TEXT        real key presses for TEXT;  key c KEYSYM...   (Return, Tab, BackSpace, Escape, ctrl+a ...)
#   shot c NAME            the client's screen as $S/shots/NAME.png
#   log c [N]              the client's last N log lines;   lobby / relay / platform: their logs
#   kill_client c / start_client c / kill_lobby / start_lobby / kill_relay / start_relay   (failure injection: a lost lobby / relay)
set -u
S=${1:?session dir}; N=${2:?clients}; MIN=${3:-90}
WT=$(pwd)
RELAY=${RELAY:-$B/relay/openbfme_relay}
NODE=${NODE:-$XDO/node}
mkdir -p "$S/cmd" "$S/shots" "$S/data" "$S/relaylog"
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM); FPORT=$(port SOCK_STREAM); RPORT=$(port SOCK_DGRAM)
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' $KEY > "$S/keys.txt"
W=1280; H=800
declare -A CPID XPID
start_relay() { "$RELAY" --bind 127.0.0.1:$RPORT --keys "$S/keys.txt" --log-dir "$S/relaylog" --status "$S/rs.json" --stats-s 0 --idle-s 120 >> "$S/relay.log" 2>&1 & RPID=$!; }
kill_relay() { kill $RPID 2>/dev/null; wait $RPID 2>/dev/null; echo "relay stopped"; }
start_lobby() {
  LOBBY_BIND=127.0.0.1:$LPORT LOBBY_DATA="$S/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="local=127.0.0.1:$RPORT" \
    MOTD="${MOTD:-Welcome to the OpenBFME staging lobby (ONLINEUX-1 audit).}" "$NODE" "$WT/services/online/lobby/src/server.mjs" >> "$S/lobby.log" 2>&1 & LPID=$!
  for _ in $(seq 1 50); do curl -s "http://127.0.0.1:$LPORT/v1/health" > /dev/null && break; sleep 0.2; done
}
kill_lobby() { kill $LPID 2>/dev/null; wait $LPID 2>/dev/null; echo "lobby stopped"; }
start_platform() { python3 -S "$WT/tools/platform_fake/fake_server.py" --admin-token ux1 --port $FPORT >> "$S/platform.log" 2>&1 & FPID=$!; }
start_x() { local c=$1; "$XVFB" :$((70 + c)) -screen 0 ${W}x${H}x24 -nolisten tcp > "$S/x$c.log" 2>&1 & XPID[$c]=$!; sleep 1; }
start_client() {
  local c=$1; mkdir -p "$S/home$c"
  ( cd "$WT/godot" && DISPLAY=:$((70 + c)) HOME="$S/home$c" XDG_DATA_HOME="$S/home$c/.local/share" XDG_CONFIG_HOME="$S/home$c/.config" \
    OPENBFME_LOBBY=http://127.0.0.1:$LPORT OPENBFME_PLATFORM=http://127.0.0.1:$FPORT/v1 \
    exec godot --path . --rendering-driver opengl3 --audio-driver Dummy --position 0,0 -- --res=${W}x${H} --no-intro --no-update-check ${CLIENT_ARGS:-} \
    >> "$S/client$c.log" 2>&1 ) & CPID[$c]=$!
}
kill_client() { kill ${CPID[$1]} 2>/dev/null; wait ${CPID[$1]} 2>/dev/null; echo "client $1 stopped"; }
xdo() { local c=$1; shift; DISPLAY=:$((70 + c)) LD_LIBRARY_PATH="$XDO/root/usr/lib/x86_64-linux-gnu" "$XDO/root/usr/bin/xdotool" "$@"; }
click() { xdo $1 mousemove $2 $3; sleep 0.15; xdo $1 mousedown ${4:-1}; sleep 0.12; xdo $1 mouseup ${4:-1}; sleep 0.3; }
dclick() { click "$@"; sleep 0.05; click "$@"; }
typetext() { local c=$1; shift; xdo $c type --delay 60 -- "$*"; }
key() { local c=$1; shift; xdo $c key --delay 80 "$@"; }
shot() { ffmpeg -loglevel error -y -f x11grab -video_size ${W}x${H} -i :$((70 + $1)) -frames:v 1 "$S/shots/$2.png" && echo "shot $2"; }
log() { tail -n ${2:-30} "$S/client$1.log"; }
lobby() { tail -n ${1:-30} "$S/lobby.log"; }
relay() { tail -n ${1:-30} "$S/relay.log"; }
platform() { tail -n ${1:-30} "$S/platform.log"; }
cleanup() { for c in "${!CPID[@]}"; do kill ${CPID[$c]} 2>/dev/null; done; for c in "${!XPID[@]}"; do kill ${XPID[$c]} 2>/dev/null; done
  kill $LPID $RPID $FPID 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT
start_relay; start_lobby; start_platform
echo "lobby http://127.0.0.1:$LPORT relay 127.0.0.1:$RPORT platform http://127.0.0.1:$FPORT/v1" | tee "$S/endpoints"
for c in $(seq 1 $N); do start_x $c; start_client $c; done
echo "session up: $N clients" > "$S/up"
END=$(( $(date +%s) + MIN * 60 )); next=1
while [ "$(date +%s)" -lt $END ]; do
  f=$(printf '%s/cmd/%03d.sh' "$S" $next)
  if [ -f "$f" ]; then
    if grep -qx quit "$f"; then echo quit > "$S/cmd/$(printf %03d $next).out"; break; fi
    source "$f" > "$f.tmp" 2>&1; mv "$f.tmp" "${f%.sh}.out"; next=$((next + 1))
  else
    sleep 0.3
  fi
done
echo "session over"
