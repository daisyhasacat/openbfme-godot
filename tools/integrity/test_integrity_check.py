# OpenBFME tests (lane ANTICHEAT-1): tools/integrity/integrity_check.py, the automatic check of finished games, with a stand-in openbfme_peer (the real
# peer's reports come from engine/tests/test_orders_into_fog_retail.cpp and the fixtures): one game gives the kept replay, the three detector reports and a
# summary; watch checks every new game of a log directory or of the lobby's worker read routes once, posts the findings, and remembers what it did.
from __future__ import annotations

import hashlib
import hmac
import io
import json
import os
import stat
import sys
import tempfile
import threading
import unittest
import urllib.parse
from contextlib import redirect_stderr, redirect_stdout
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import integrity_check as ic  # noqa: E402

ALERT = json.loads((HERE / "fixtures" / "d1-evendim-alert.json").read_text())

FAKE_PEER = r'''#!/usr/bin/env python3
# a stand-in for openbfme_peer: --relay-log ... --record OUT writes OUT; --replay ... writes the three reports (D1 from $FAKE_D1)
import json, os, sys
a = sys.argv[1:]
open(os.environ["FAKE_ARGS"], "a").write(" ".join(a) + "\n")
def arg(k):
    return a[a.index(k) + 1] if k in a else None
if "--relay-log" in a:
    open(arg("--record"), "wb").write(b"OBFMEREP fake")
    sys.exit(0)
d1 = json.loads(os.environ["FAKE_D1"])
gid = arg("--game-id")
d1["game_id"] = gid
def other(det, players):
    return {"v": 1, "detector": det, "detector_version": 1, "game_id": gid, "status": d1["status"], "abstain_reason": d1["abstain_reason"],
            "frames": d1["frames"], "crc_parity": d1["crc_parity"], "settings": {}, "players": players}
p = d1["players"][0]
d5 = other("D5-input-rate", [{"player_index": p["player_index"], "slot": p["slot"], "name": p["name"], "band": "review", "score": 1.0, "encounters": 1,
                              "commands_total": 400, "peak_5s": 140, "peak_frame": 500,
                              "events": [{"frame": 500, "time_s": 100.0, "command": "MSG_DO_MOVETO", "count": 140, "start_frame": 490, "end_frame": 505,
                                          "encounter": 1}]}])
d6 = other("D6-command-integrity", [{"player_index": p["player_index"], "slot": p["slot"], "name": p["name"], "band": "none", "score": 0.0, "encounters": 0,
                                     "commands_total": 400, "events": []}])
for flag, rep in (("--fog-check", d1), ("--input-rate-check", d5), ("--command-check", d6)):
    if arg(flag):
        open(arg(flag), "w").write(json.dumps(rep))
open(arg("--report"), "w").write("fake\n")
sys.exit(0)
'''


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.peer = self.dir / "peer.py"
        self.peer.write_text(FAKE_PEER)
        self.peer.chmod(self.peer.stat().st_mode | stat.S_IEXEC)
        self.args = self.dir / "args.txt"
        self.env = {"FAKE_ARGS": str(self.args), "FAKE_D1": json.dumps(ALERT)}
        self.old = {k: os.environ.get(k) for k in self.env}
        os.environ.update(self.env)

    def tearDown(self):
        for k, v in self.old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self.tmp.cleanup()

    def quiet(self, argv):
        with redirect_stdout(io.StringIO()) as out:
            rc = ic.main(argv)
        return rc, out.getvalue()


class GameTest(Base):
    def test_a_relay_game_keeps_its_replay_three_reports_and_a_summary(self):
        log, start = self.dir / "g.obfmerlog", self.dir / "g.start.json"
        log.write_bytes(b"log")
        start.write_text("{}")
        out = self.dir / "out"
        rc, text = self.quiet(["game", "--peer", str(self.peer), "--relay-log", str(log), "--online-start", str(start), "--game-id", "0123456789abcdef",
                               "--out-dir", str(out), "--no-render"])
        self.assertEqual(rc, 0)
        self.assertTrue((out / "game.replay").read_bytes().startswith(b"OBFMEREP"))
        for name in ("d1.json", "d5.json", "d6.json", "summary.json", "summary.txt"):
            self.assertTrue((out / name).exists(), name)
        calls = self.args.read_text().splitlines()
        self.assertEqual(len(calls), 2)  # assemble, then ONE re-simulation for the three checks
        for flag in ("--fog-check", "--input-rate-check", "--command-check", "--game-id 0123456789abcdef"):
            self.assertIn(flag, calls[1])
        s = json.loads((out / "summary.json").read_text())
        self.assertEqual(s["replay"], "game.replay")
        bands = {det: [p["band"] for p in ps] for det, ps in s["detectors"].items()}
        self.assertEqual(bands, {"D1-orders-into-fog": ["alert"], "D5-input-rate": ["review"], "D6-command-integrity": ["none"]})
        self.assertIn("could not see", text)
        self.assertIn("macros are allowed", text)


class WatchTest(Base):
    def serve(self, key: bytes, games: list[str]):
        got = []

        class H(BaseHTTPRequestHandler):
            def check(self, body: bytes) -> bool:
                ts, mac = self.headers["x-openbfme-admin"].split(".")
                path, _, query = self.path.partition("?")
                query = urllib.parse.urlencode(urllib.parse.parse_qsl(query))  # as the lobby does: its own serialization of the query (":" as %3A)
                want = hmac.new(key, f"{self.command} {path}?{query}\n{ts}\n".encode() + body, hashlib.sha256).hexdigest()
                return hmac.compare_digest(mac, want)

            def reply(self, code, body=b""):
                self.send_response(code)
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if not self.check(b""):
                    return self.reply(401)
                path = self.path.partition("?")[0]
                if path == "/v1/internal/integrity/games":
                    # the lobby's shape (b8e395a05): two pages, the second the last (next_cursor null); a game without its START is skipped
                    after = dict(p.split("=", 1) for p in self.path.partition("?")[2].split("&") if "=" in p).get("after")
                    if after is None:
                        return self.reply(200, json.dumps({"v": 1, "items": [{"game_id": games[0], "log_stored_at": 1, "log_bytes": 3, "ended_at": 1,
                                                                              "has_start": True}], "next_cursor": "1000:" + games[0],
                                                           "resume_cursor": "1000:" + games[0]}).encode())
                    # f27dd93a3: resume_cursor on every non-empty page (the last one's is where the next run starts)
                    return self.reply(200, json.dumps({"v": 1, "items": [{"game_id": "00000000000000ee", "log_stored_at": 2, "log_bytes": 3,
                                                                          "ended_at": None, "has_start": False}], "next_cursor": None,
                                                       "resume_cursor": "2000:00000000000000ee"}).encode())
                if path.endswith("/log"):
                    return self.reply(200, b"log")
                if path.endswith("/start"):
                    return self.reply(200, b"{}")
                return self.reply(404)

            def do_POST(self):
                body = self.rfile.read(int(self.headers["content-length"]))
                if not self.check(body):
                    return self.reply(401)
                got.append(json.loads(body))
                self.reply(201, b"{}")

            def log_message(self, *a):
                pass

        srv = HTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(srv.shutdown)
        return f"http://127.0.0.1:{srv.server_port}", got

    def test_watch_a_log_directory_once_checks_every_new_game_and_remembers(self):
        inc = self.dir / "incoming"
        inc.mkdir()
        for gid in ("00000000000000a1", "00000000000000a2"):
            (inc / f"{gid}.obfmerlog").write_bytes(b"log")
            (inc / f"{gid}.start.json").write_text("{}")
        (inc / "00000000000000a3.obfmerlog").write_bytes(b"no start yet")
        root = self.dir / "root"
        argv = ["watch", "--once", "--peer", str(self.peer), "--root", str(root), "--incoming", str(inc), "--no-render"]
        rc, _ = self.quiet(argv)
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads((root / "state.json").read_text())["done"], ["00000000000000a1", "00000000000000a2"])
        self.assertTrue((root / "00000000000000a2" / "game.replay").exists())
        before = len(self.args.read_text().splitlines())
        rc, _ = self.quiet(argv)  # nothing new
        self.assertEqual(rc, 0)
        self.assertEqual(len(self.args.read_text().splitlines()), before)

    def test_watch_the_lobby_fetches_signed_and_posts_only_the_intakes_detectors(self):
        key = bytes(range(32))
        lobby, got = self.serve(key, ["00000000000000b1"])
        kf = self.dir / "k"
        kf.write_text(key.hex())
        root = self.dir / "root"
        rc, _ = self.quiet(["watch", "--once", "--peer", str(self.peer), "--root", str(root), "--lobby", lobby, "--read-key-file", str(kf),
                            "--url", lobby + "/v1/internal/integrity/findings", "--key-file", str(kf), "--no-render"])
        self.assertEqual(rc, 0)
        self.assertEqual([(b["detector"], b["band"], b["game_id"]) for b in got], [("D1-orders-into-fog", "alert", "00000000000000b1")])
        state = json.loads((root / "state.json").read_text())
        self.assertEqual((state["done"], state["cursor"]), (["00000000000000b1"], "2000:00000000000000ee"))


PROFILE_A, PROFILE_B = "a" * 32, "b" * 32


class CheckerTest(Base):
    """One checker per release (--peers DIR/<profile>/openbfme_peer): a peer replays only its own build's games."""

    def install(self, peers: Path, profile: str, source: str = FAKE_PEER):
        d = peers / profile
        d.mkdir(parents=True)
        (d / "openbfme_peer").write_text(source)
        (d / "openbfme_peer").chmod(0o755)

    def incoming(self, games: dict[str, str]) -> Path:
        inc = self.dir / "incoming"
        inc.mkdir(exist_ok=True)
        for gid, profile in games.items():
            (inc / f"{gid}.obfmerlog").write_bytes(b"log")
            (inc / f"{gid}.start.json").write_text(json.dumps({"v": 1, "kind": "openbfme-replay-start", "profile": profile}))
        return inc

    def test_a_game_without_a_checker_for_its_profile_waits_and_is_checked_once_one_is_installed(self):
        peers, root = self.dir / "peers", self.dir / "root"
        self.install(peers, PROFILE_A)
        inc = self.incoming({"00000000000000d1": PROFILE_A, "00000000000000d2": PROFILE_B})
        argv = ["watch", "--once", "--peers", str(peers), "--root", str(root), "--incoming", str(inc), "--no-render"]
        rc, text = self.quiet(argv)
        self.assertEqual(rc, 0)  # not a failure
        state = json.loads((root / "state.json").read_text())
        self.assertEqual(state["done"], ["00000000000000d1"])
        self.assertEqual(state["unsupported"]["00000000000000d2"]["profile"], PROFILE_B)
        self.assertIn("no checker for its profile " + PROFILE_B, text)
        self.assertFalse((root / "00000000000000d2").exists())
        self.install(peers, PROFILE_B)
        rc, _ = self.quiet(argv)
        self.assertEqual(rc, 0)
        state = json.loads((root / "state.json").read_text())
        self.assertEqual((state["done"], state["unsupported"]), (["00000000000000d1", "00000000000000d2"], {}))
        self.assertEqual(json.loads((root / "00000000000000d2" / "summary.json").read_text())["peer"], str(peers / PROFILE_B / "openbfme_peer"))

    def test_an_unsupported_game_expires_after_the_lobbys_roster_window(self):
        peers, root = self.dir / "peers", self.dir / "root"
        peers.mkdir()
        inc = self.incoming({"00000000000000d3": PROFILE_B})
        argv = ["watch", "--once", "--peers", str(peers), "--root", str(root), "--incoming", str(inc), "--no-render"]
        self.quiet(argv)
        state = json.loads((root / "state.json").read_text())
        state["unsupported"]["00000000000000d3"]["since"] -= ic.ROSTER_SECONDS + 1
        (root / "state.json").write_text(json.dumps(state))
        rc, _ = self.quiet(argv)
        self.assertEqual(rc, 0)
        state = json.loads((root / "state.json").read_text())
        self.assertEqual((state["done"], state["unsupported"], state["expired"]), ([], {}, ["00000000000000d3"]))

    def test_an_older_releases_checker_runs_only_the_detectors_it_has(self):
        peers, root = self.dir / "peers", self.dir / "root"
        old = FAKE_PEER.replace(', ("--input-rate-check", d5), ("--command-check", d6)', ",")
        self.assertNotIn("--command-check", old)
        self.install(peers, PROFILE_A, old)
        inc = self.incoming({"00000000000000d4": PROFILE_A})
        rc, _ = self.quiet(["watch", "--once", "--peers", str(peers), "--root", str(root), "--incoming", str(inc), "--no-render"])
        self.assertEqual(rc, 0)
        out = root / "00000000000000d4"
        self.assertTrue((out / "d1.json").exists())
        self.assertFalse((out / "d5.json").exists() or (out / "d6.json").exists())
        self.assertNotIn("--input-rate-check", self.args.read_text())
        self.assertEqual(list(json.loads((out / "summary.json").read_text())["detectors"]), ["D1-orders-into-fog"])

    def test_watch_needs_exactly_one_of_peer_and_peers(self):
        root = self.dir / "root"
        for extra in ([], ["--peer", str(self.peer), "--peers", str(self.dir)]):
            with redirect_stderr(io.StringIO()):
                rc, _ = self.quiet(["watch", "--once", "--root", str(root), "--incoming", str(self.dir)] + extra)
            self.assertEqual(rc, 1)


class RetryTest(Base):
    def test_an_undelivered_finding_stays_pending_and_is_sent_next_run_without_checking_again(self):
        inc = self.dir / "incoming"
        inc.mkdir()
        (inc / "00000000000000c1.obfmerlog").write_bytes(b"log")
        (inc / "00000000000000c1.start.json").write_text("{}")
        root = self.dir / "root"
        kf = self.dir / "k"
        kf.write_text(bytes(range(32)).hex())
        sent = []
        answers = ["HTTP 503: down"]
        original = ic.fc.post
        ic.fc.post = lambda url, body, key, **kw: (sent.append(json.loads(body)) or (answers.pop(0) if answers else ""))
        try:
            argv = ["watch", "--once", "--peer", str(self.peer), "--root", str(root), "--incoming", str(inc), "--no-render",
                    "--url", "http://lobby.invalid/v1/internal/integrity/findings", "--key-file", str(kf)]
            rc, _ = self.quiet(argv)
            self.assertEqual(rc, 1)
            state = json.loads((root / "state.json").read_text())
            self.assertEqual(state["done"], [])
            self.assertEqual(state["pending"]["00000000000000c1"]["stage"], "post")
            checks = len(self.args.read_text().splitlines())
            rc, _ = self.quiet(argv)
            self.assertEqual(rc, 0)
            state = json.loads((root / "state.json").read_text())
            self.assertEqual((state["done"], state["pending"]), (["00000000000000c1"], {}))
            self.assertEqual(len(self.args.read_text().splitlines()), checks)  # not checked again
            self.assertEqual(len(sent), 2)
        finally:
            ic.fc.post = original

    def test_an_unreadable_report_does_not_hide_the_others(self):
        out = self.dir / "out"
        rc, _ = self.quiet(["game", "--peer", str(self.peer), "--replay", str(self.peer), "--game-id", "0123456789abcdef", "--out-dir", str(out),
                            "--no-render"])
        self.assertEqual(rc, 0)
        (out / "d6.json").write_text('{"v":1,"players":[{"x":nan}]')  # broken
        sent = []
        original = ic.fc.post
        ic.fc.post = lambda url, body, key, **kw: sent.append(json.loads(body)) or ""
        try:
            failed = ic.post_dir(out, "http://lobby.invalid/x", b"k" * 32, ["D1-orders-into-fog", "D6-command-integrity"])
        finally:
            ic.fc.post = original
        self.assertEqual(failed, 1)                                          # the broken one is reported
        self.assertEqual([b["detector"] for b in sent], ["D1-orders-into-fog"])  # D1 still delivered


if __name__ == "__main__":
    unittest.main()
