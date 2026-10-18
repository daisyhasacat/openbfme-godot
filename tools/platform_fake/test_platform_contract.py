"""Lane ONLINEHUB-1 (review r1): the wire shapes the game's platform client depends on, pinned against the backend's own service (tools/platform_fake,
source commit 192a0fca). A backend change that breaks the client fails here by name; the client's own tests (engine/tests/test_onlinehub1_*.cpp) then say
what to change. No network beyond 127.0.0.1; Python's standard library only."""
from __future__ import annotations

import json
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOKEN = "contract-test-admin-token"
A, B, C = "a" * 32, "b" * 32, "c" * 32
CLIENT = {"platform": "linux", "version": "0.4.5-test"}


class Fake:
    def __init__(self):
        self.proc = subprocess.Popen([sys.executable, "-S", str(HERE / "fake_server.py"), "--admin-token", TOKEN],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.port = json.loads(self.proc.stdout.readline())["port"]

    def call(self, method, path, body=None, *, token=None, raw=None, content_type="application/json"):
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        headers = {"Content-Type": content_type} if content_type else {}
        if token:
            headers["Authorization"] = "Bearer " + token
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def admin(self, path, body):
        return self.call("POST", path, body, token=TOKEN)

    def advance(self, seconds):
        return self.call("POST", "/__test/advance", {"seconds": seconds})

    def close(self):
        self.proc.kill()
        self.proc.wait()


@pytest.fixture()
def fake():
    f = Fake()
    yield f
    f.close()


def ticket(install, **extra):
    return {"v": 1, "install": install, "nick": "Nick", "client": CLIENT, "mode": "faithful", "size": "1v1", "factions": ["random"], **extra}


def test_every_write_needs_client_metadata_and_json_content_type_and_the_error_envelope_is_an_object(fake):
    body = ticket(A)
    del body["client"]
    status, err = fake.call("POST", "/v1/qm/tickets", body)
    assert (status, err) == (400, {"v": 1, "error": {"code": "missing_field", "field": "client"}})
    status, err = fake.call("POST", "/v1/qm/tickets", ticket(A, client={"platform": "mac", "version": "1"}))
    assert (status, err["error"]) == (400, {"code": "invalid_field", "field": "client.platform"})
    status, err = fake.call("POST", "/v1/qm/tickets", ticket(A), content_type="text/plain")
    assert (status, err["error"]["code"]) == (415, "unsupported_content_type")
    status, err = fake.call("POST", "/v1/qm/tickets", ticket(A, mode="ranked"))
    assert (status, err["error"]) == (400, {"code": "invalid_field", "field": "mode"})


def test_quick_match_ticket_is_201_with_a_32_hex_ticket_and_poll_after_5_and_polls_omit_the_interval(fake):
    status, created = fake.call("POST", "/v1/qm/tickets", ticket(A))
    assert status == 201
    assert created["v"] == 1 and re.fullmatch(r"[0-9a-f]{32}", created["ticket"]) and created["poll_after_s"] == 5
    assert set(created) == {"v", "ticket", "poll_after_s"}
    status, polled = fake.call("GET", f"/v1/qm/tickets/{created['ticket']}")
    assert (status, polled) == (200, {"v": 1, "state": "searching"})  # no poll_after_s: the client keeps the one it was given
    # the same install and preferences again: the same ticket (201 again); other preferences: 409 active_ticket_conflict
    status, again = fake.call("POST", "/v1/qm/tickets", ticket(A))
    assert (status, again["ticket"]) == (201, created["ticket"])
    status, conflict = fake.call("POST", "/v1/qm/tickets", ticket(A, size="2v2"))
    assert (status, conflict["error"]["code"]) == (409, "active_ticket_conflict")
    # cancel is 200 {state:"cancelled"} and idempotent; an unknown ticket polls 404
    assert fake.call("DELETE", f"/v1/qm/tickets/{created['ticket']}") == (200, {"v": 1, "state": "cancelled"})
    assert fake.call("DELETE", f"/v1/qm/tickets/{created['ticket']}") == (200, {"v": 1, "state": "cancelled"})
    assert fake.call("GET", "/v1/qm/tickets/" + "0" * 32)[0] == 404


def test_a_match_names_the_oldest_ticket_as_host_with_a_16_hex_upper_room_code_and_a_ticket_expires_300_s_after_its_last_poll(fake):
    _, first = fake.call("POST", "/v1/qm/tickets", ticket(A, nick="One"))
    _, second = fake.call("POST", "/v1/qm/tickets", ticket(B, nick="Two"))
    for t in (first, second):
        status, polled = fake.call("GET", f"/v1/qm/tickets/{t['ticket']}")
        assert status == 200 and polled["state"] == "matched"
        m = polled["match"]
        assert set(m) == {"id", "room_code", "host_install", "players", "mode"}
        assert re.fullmatch(r"[0-9a-f]{32}", m["id"]) and re.fullmatch(r"[0-9A-F]{16}", m["room_code"])
        assert m["host_install"] == A and m["mode"] == "faithful"
        assert m["players"] == [{"nick": "One", "team": 0}, {"nick": "Two", "team": 1}]
    # an unpolled ticket is not matched after 300 s
    fake.advance(1000)
    _, old = fake.call("POST", "/v1/qm/tickets", ticket(C, mode="uber"))
    fake.advance(301)
    _, late = fake.call("POST", "/v1/qm/tickets", ticket("d" * 32, mode="uber"))
    assert fake.call("GET", f"/v1/qm/tickets/{old['ticket']}")[1]["state"] == "expired"
    assert fake.call("GET", f"/v1/qm/tickets/{late['ticket']}")[1]["state"] == "searching"


def make_tournament(fake, start_in=60, players=2):
    now = fake.call("POST", "/__test/advance", {"seconds": 0})[1]["now"]
    status, created = fake.admin("/v1/admin/tournaments", {"v": 1, "name": "Contract Cup", "mode": "faithful", "size": "1v1", "start": now + start_in,
                                                          "max_players": players, "rules": "Best of one.\nNo rushing."})
    assert status == 201
    return created["id"]


def register(fake, tid, install, nick):
    return fake.call("POST", f"/v1/tournaments/{tid}/register", {"v": 1, "install": install, "nick": nick, "client": CLIENT})


def test_tournament_list_and_detail_shapes_registration_and_a_1v1_only_v1(fake):
    assert fake.admin("/v1/admin/tournaments", {"v": 1, "name": "Teams", "mode": "faithful", "size": "2v2", "start": 2_000_000_000, "max_players": 4})[1]["error"] == {
        "code": "invalid_field", "field": "size"}
    tid = make_tournament(fake)
    assert re.fullmatch(r"[0-9a-f]{32}", tid)
    status, listing = fake.call("GET", "/v1/tournaments")
    assert status == 200 and set(listing) == {"v", "tournaments", "next_cursor"} and listing["next_cursor"] is None
    assert set(listing["tournaments"][0]) == {"id", "name", "mode", "size", "start", "max_players", "state", "registered"}
    assert listing["tournaments"][0]["state"] == "upcoming" and listing["tournaments"][0]["size"] == "1v1"
    status, detail = fake.call("GET", f"/v1/tournaments/{tid}?install={A}")
    assert status == 200
    assert set(detail) == {"v", "id", "name", "mode", "size", "start", "max_players", "registered", "state", "rules", "rounds", "matches"}
    assert detail["rules"] == "Best of one.\nNo rushing." and detail["rounds"] == [] and detail["matches"] == []
    assert register(fake, tid, A, "Alpha")[0] == 200 and register(fake, tid, A, "Alpha")[1] == {"v": 1, "registered": True}
    assert register(fake, tid, A, "Other")[1]["error"]["code"] == "registration_conflict"
    assert fake.call("DELETE", f"/v1/tournaments/{tid}/register", {"v": 1, "install": A, "client": CLIENT}) == (200, {"v": 1, "registered": False})
    body = {"v": 1, "install": A, "nick": "Alpha"}
    assert fake.call("POST", f"/v1/tournaments/{tid}/register", body)[1]["error"] == {"code": "missing_field", "field": "client"}


def test_a_started_tournament_has_matches_with_ids_nick_players_a_room_code_only_for_their_players_and_a_checkin_route(fake):
    tid = make_tournament(fake, start_in=60)
    register(fake, tid, A, "Alpha")
    register(fake, tid, B, "Bravo")
    assert register(fake, tid, C, "Charlie")[1]["error"]["code"] == "tournament_full"
    fake.advance(61)
    status, detail = fake.call("GET", f"/v1/tournaments/{tid}?install={A}")
    assert detail["state"] == "running" and len(detail["rounds"]) == 1 and len(detail["matches"]) == 1
    m = detail["matches"][0]
    assert re.fullmatch(tid + r"\.[0-9a-f]{24}", m["id"]) and detail["rounds"] == [[m["id"]]]
    assert m["state"] == "open" and m["players"] == ["Alpha", "Bravo"] and m["round"] == 0 and m["winner"] is None
    assert re.fullmatch(r"[0-9A-F]{16}", m["room_code"])
    assert "room_code" not in fake.call("GET", f"/v1/tournaments/{tid}?install={C}")[1]["matches"][0]
    assert "room_code" not in fake.call("GET", f"/v1/tournaments/{tid}")[1]["matches"][0]
    checkin = f"/v1/tournaments/{tid}/matches/{m['id']}/checkin"
    assert fake.call("POST", checkin, {"v": 1, "install": C, "client": CLIENT})[1]["error"]["code"] == "not_a_match_participant"
    assert fake.call("POST", checkin, {"v": 1, "install": A, "client": CLIENT}) == (200, {"v": 1, "state": "open"})
    assert fake.call("POST", checkin, {"v": 1, "install": B, "client": CLIENT}) == (200, {"v": 1, "state": "ready"})
    # one side checked in only: after 900 s that side wins a no-show
    fake.advance(901)
    assert fake.call("GET", f"/v1/tournaments/{tid}?install={A}")[1]["matches"][0]["state"] == "ready"  # both checked in: the match stays ready (no no-show)
    status, closed = fake.call("POST", checkin, {"v": 1, "install": A, "client": CLIENT})
    assert (status, closed["error"]["code"]) == (409, "checkin_closed")


def test_results_need_client_two_players_and_teams_0_to_7(fake):
    now = fake.advance(0)[1]["now"]

    def report(install, slot, game_id, *, players=2, team1=1, winner=0):
        plist = [{"slot": 0, "nick": "Alpha", "faction": "FactionMen", "team": 0, "human": True},
                 {"slot": 1, "nick": "Bravo", "faction": "FactionMordor", "team": team1, "human": True}][:players]
        return {"v": 1, "install": install, "nick": plist[slot]["nick"], "client": CLIENT,
                "game": {"id": game_id, "mode": "faithful", "map": "maps/x/x.map", "players": plist, "start": now - 400, "end": now - 100, "duration_s": 300,
                         "winner_team": winner}, "me": {"slot": slot}}

    assert fake.call("POST", "/v1/results", report(A, 0, "g-one", players=1))[1]["error"] == {"code": "invalid_field", "field": "game.players"}
    assert fake.call("POST", "/v1/results", report(A, 0, "g-two", team1=8))[1]["error"] == {"code": "invalid_field", "field": "game.players.1.team"}
    no_client = report(A, 0, "g-three")
    del no_client["client"]
    assert fake.call("POST", "/v1/results", no_client)[1]["error"] == {"code": "missing_field", "field": "client"}
    assert fake.call("POST", "/v1/results", report(A, 0, "g-four")) == (201, {"v": 1, "accepted": True, "confirmed": False})
    assert fake.call("POST", "/v1/results", report(A, 0, "g-four")) == (200, {"v": 1, "accepted": True, "confirmed": False})
    assert fake.call("POST", "/v1/results", report(B, 1, "g-four")) == (201, {"v": 1, "accepted": True, "confirmed": True})
    fake.advance(0)
    status, stats = fake.call("GET", "/v1/stats/global?mode=&period=all")
    assert status == 200 and stats["games"] == 1 and set(stats) >= {"v", "as_of", "games", "factions", "matchups", "maps", "avg_duration_s", "vs_ai"}
    assert stats["factions"][0].keys() == {"faction", "picks", "pick_rate", "wins", "win_rate"}


def test_global_stats_errors_and_the_unavailable_cache_are_enveloped(fake):
    assert fake.call("GET", "/v1/stats/global?mode=ranked&period=all")[1]["error"]["code"] == "invalid_mode"
    assert fake.call("GET", "/v1/stats/global?mode=&period=1y")[1]["error"]["code"] == "invalid_period"
    assert fake.call("GET", "/v1/health") == (200, {"v": 1, "status": "ok"})


def test_the_test_surface_injects_a_failure_for_one_body_and_logs_every_v1_request(fake):
    assert fake.call("POST", "/__test/fail", {"prefix": "/v1/results", "status": 503, "count": 1, "body_contains": "only-this"})[0] == 200
    body = {"v": 1, "install": A, "nick": "N", "client": CLIENT}
    assert fake.call("POST", "/v1/results", {**body, "marker": "other"})[1]["error"]["code"] != "injected"
    assert fake.call("POST", "/v1/results", {**body, "marker": "only-this"}) == (503, {"v": 1, "error": {"code": "injected"}})
    assert fake.call("POST", "/v1/results", {**body, "marker": "only-this"})[1]["error"]["code"] != "injected"  # once
    status, log = fake.call("GET", "/__test/log")
    assert [(e["method"], e["path"], e["status"]) for e in log["log"]] == [("POST", "/v1/results", 400), ("POST", "/v1/results", 503), ("POST", "/v1/results", 400)]
    assert all(e["content_type"] == "application/json" for e in log["log"])
