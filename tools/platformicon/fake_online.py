#!/usr/bin/env python3
"""Lane PLATFORMICON-1: a fake online player for the proof captures (standard library only).

  fake_online.py LOBBY_URL PROFILE host NAME PLATFORM [SECONDS]    sign in as a guest, create a game, keep it open
  fake_online.py LOBBY_URL PROFILE join NAME PLATFORM [SECONDS]    sign in, wait for a game in the list, join it, say ready, stay

PLATFORM is windows, linux, macos or `none` (an older client: no field). Prints one line per step; exits 0 when it stayed to the end, 1 when a step failed.
"""
import json
import sys
import time
import urllib.request

MAP = "maps/map mp evendim/map mp evendim.map"


def call(base, method, path, token=None, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{method} {path}: HTTP {e.code} {e.read().decode()[:200]}")


def main(argv):
    base, profile, mode, name, platform = argv[:5]
    stay = float(argv[5]) if len(argv) > 5 else 120.0
    extra = {} if platform == "none" else {"platform": platform}
    token = call(base, "POST", "/v1/auth/guest", body={"name": name})["token"]
    if mode == "host":
        setup = {"map": MAP, "startingCash": 1500, "slots": [{"state": "player", "faction": 0, "color": 0, "startPos": 0, "team": 0}] + [{"state": "open"}] * 3}
        game = call(base, "POST", "/v1/games", token, {"name": f"{name}'s game", "profile": profile, "setup": setup, **extra})
        print("created", game["id"], flush=True)
        gid = game["id"]
    else:
        gid = None
        deadline = time.time() + 90
        while gid is None and time.time() < deadline:
            games = [g for g in call(base, "GET", "/v1/games")["games"] if g["state"] == "open" and g["openSlots"] > 0]
            gid = games[0]["id"] if games else None
            time.sleep(1)
        if gid is None:
            print("no game to join", flush=True)
            return 1
        call(base, "POST", f"/v1/games/{gid}/join", token, {"profile": profile, **extra})
        call(base, "POST", f"/v1/games/{gid}/ready", token, {"ready": True})
        print("joined and ready", gid, flush=True)
    end = time.time() + stay
    while time.time() < end:
        try:
            call(base, "GET", f"/v1/games/{gid}", token)
        except SystemExit:
            break
        time.sleep(2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
