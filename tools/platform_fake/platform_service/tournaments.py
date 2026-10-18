from __future__ import annotations

import secrets
from collections import Counter
from typing import Any

from platform_service import stats
from platform_service.protocol import (
    RequestError,
    canonical_path,
    client_metadata,
    integer,
    normalize_install,
    page_cursor,
    required,
    text,
)
from platform_service.service import DAY, ServiceError
from platform_service.storage import key

TOURNAMENT_TTL = 365 * DAY
_TERMINAL = {"finished", "admin_resolved", "no_show", "bye"}


def _tournament_id(value: str) -> str:
    if len(value) != 32 or any(char not in "0123456789abcdef" for char in value):
        raise ServiceError(404, "tournament_not_found")
    return value


def _read(service, tournament_id: str, now: int):
    tournament_id = _tournament_id(tournament_id)
    row, doc = service._get(f"TOURNAMENT#{tournament_id}", "DOC")
    if row and int(row.get("expires_at", 0)) <= now:
        return None, {}
    return row, doc


def _summary(doc: dict[str, Any]) -> dict[str, Any]:
    return {
        **{name: doc[name] for name in ("id", "name", "mode", "size", "start", "max_players", "state")},
        "registered": len(doc["registrants"]),
    }


def _put_pair(store, pk: str, sk: str, payload: dict[str, Any], expires_at: int):
    row = store.get(pk, sk)
    return (
        {key(pk, sk): row["version"] if row else None},
        {"pk": pk, "sk": sk, "payload": payload, "expires_at": expires_at},
    )


def _persist_tournament(service, row, doc, now: int, extra_expected=None, extra_writes=None, activity=None) -> bool:
    expected = {key(row["pk"], row["sk"]): row["version"] if row else None}
    writes = [{"pk": f"TOURNAMENT#{doc['id']}", "sk": "DOC", "payload": doc,
               "expires_at": now + TOURNAMENT_TTL}]
    index_key = key("TOURNAMENTS", doc["id"])
    index_row = service.store.get(*index_key)
    expected[index_key] = index_row["version"] if index_row else None
    writes.append({"pk": "TOURNAMENTS", "sk": doc["id"], "payload": _summary(doc),
                   "expires_at": now + TOURNAMENT_TTL})
    if extra_expected:
        expected.update(extra_expected)
    if extra_writes:
        writes.extend(extra_writes)
    if activity is not None:
        install, client = activity
        return service.metrics.transact(expected, writes, install, client, now)
    return service.store.transact(expected, writes)


def _participant(install: str, nick: str, seed: int) -> dict[str, Any]:
    return {"install": install, "nick": nick, "seed": seed}


def _match_id(tournament_id: str) -> str:
    return f"{tournament_id}.{secrets.token_hex(12)}"


def _open_match(match: dict[str, Any], now: int) -> None:
    match.update({
        "status": "open",
        "opened_at": now,
        "room_code": secrets.token_hex(8).upper(),
        "checked_in": [],
        "winner": None,
    })


def _seed_bracket(doc: dict[str, Any], now: int) -> None:
    entrants = list(doc["registrants"])
    if len(entrants) < 2:
        doc["state"] = "cancelled"
        doc["reason"] = "insufficient_players"
        doc["matches"] = []
        return
    slots = entrants + [None] * (doc["max_players"] - len(entrants))
    matches = []
    round_ids = []
    current = []
    for index in range(0, len(slots), 2):
        left, right = slots[index], slots[index + 1]
        match = {
            "id": _match_id(doc["id"]),
            "round": 0,
            "index": index // 2,
            "participants": [left, right],
            "feeders": [],
            "winner": None,
        }
        if left and right:
            _open_match(match, now)
        else:
            match["status"] = "bye"
            match["winner"] = left or right
        matches.append(match)
        current.append(match["id"])
    round_ids.append(current)
    round_number = 1
    while len(current) > 1:
        next_round = []
        for index in range(0, len(current), 2):
            match = {
                "id": _match_id(doc["id"]),
                "round": round_number,
                "index": index // 2,
                "participants": [None, None],
                "feeders": [current[index], current[index + 1]],
                "winner": None,
                "status": "waiting",
            }
            matches.append(match)
            next_round.append(match["id"])
        current = next_round
        round_ids.append(current)
        round_number += 1
    doc["matches"] = matches
    doc["rounds"] = round_ids
    doc["state"] = "running"
    _advance(doc, now)


def _advance(doc: dict[str, Any], now: int) -> None:
    by_id = {match["id"]: match for match in doc.get("matches", [])}
    for match in sorted(doc.get("matches", []), key=lambda m: (m["round"], m["index"])):
        if match["status"] != "waiting":
            continue
        feeders = [by_id[item] for item in match["feeders"]]
        if any(feeder.get("status") not in _TERMINAL for feeder in feeders):
            continue
        participants = [feeder.get("winner") for feeder in feeders]
        match["participants"] = participants
        if participants[0] and participants[1]:
            _open_match(match, now)
        else:
            match["status"] = "bye"
            match["winner"] = participants[0] or participants[1]
    final = doc.get("matches", [])[-1] if doc.get("matches") else None
    if final and final.get("status") in _TERMINAL:
        doc["state"] = "finished"


def _sweep_match(doc: dict[str, Any], match: dict[str, Any], now: int) -> bool:
    if match.get("status") not in ("open", "ready") or now < int(match["opened_at"]) + 900:
        return False
    checked = set(match.get("checked_in", []))
    participants = [p for p in match["participants"] if p]
    if len(checked) == 1:
        match["winner"] = next(p for p in participants if p["install"] in checked)
        match["status"] = "no_show"
    elif not checked:
        match["status"] = "disputed"
    else:
        return False
    _advance(doc, now)
    return True


def _public_match(match: dict[str, Any], *, viewer_install: str | None) -> dict[str, Any]:
    participants = [p["nick"] if p else None for p in match.get("participants", [])]
    public = {
        "id": match["id"],
        "round": match["round"],
        "state": match["status"],
        "players": participants,
        "winner": match.get("winner", {}).get("nick") if match.get("winner") else None,
    }
    if "opened_at" in match:
        public["opened_at"] = match["opened_at"]
    if viewer_install and any(p and p["install"] == viewer_install for p in match.get("participants", [])):
        public["room_code"] = match.get("room_code")
    return public


def dispatch_tournament(service, method: str, path: str, query: dict[str, Any], body: dict[str, Any],
                        *, now: int, authorization: str, is_admin: bool) -> tuple[int, dict[str, Any]]:
    if method == "GET" and path == "/v1/tournaments":
        rows, cursor = service.store.query_page("TOURNAMENTS", cursor=page_cursor(query.get("cursor")))
        return 200, {
            "tournaments": [
                row["payload"] for row in rows
                if int(row.get("expires_at", 0)) > now
            ], "next_cursor": cursor,
        }

    try:
        parts = canonical_path(path)
    except RequestError as error:
        raise ServiceError(error.status, error.code, error.field) from None
    if len(parts) >= 3 and parts[:2] == ("v1", "admin") and parts[2] == "tournaments":
        service._authorize_admin(authorization)
        if method == "POST" and len(parts) == 3:
            return 201, _admin_create(service, body, now)
        if method == "POST" and len(parts) == 4:
            return 200, _admin_edit(service, parts[3], body, now)
        if method == "POST" and len(parts) == 5 and parts[4] == "cancel":
            return 200, _admin_cancel(service, parts[3], body, now)
        if method == "POST" and len(parts) == 7 and parts[4] == "matches" and parts[6] == "resolve":
            return 200, _admin_resolve(service, parts[3], parts[5], body, now)

    if len(parts) >= 3 and parts[:2] == ("v1", "tournaments"):
        tournament_id = parts[2]
        if len(parts) == 3 and method == "GET":
            row, doc = _read(service, tournament_id, now)
            if not row:
                raise ServiceError(404, "tournament_not_found")
            viewer = query.get("install")
            if viewer is not None:
                viewer = normalize_install(viewer)
            return 200, {
                "id": doc["id"], "name": doc["name"], "mode": doc["mode"], "size": doc["size"],
                "start": doc["start"], "max_players": doc["max_players"], "registered": len(doc["registrants"]),
                "state": doc["state"], "rules": doc.get("rules", ""),
                "rounds": doc.get("rounds", []),
                "matches": [_public_match(match, viewer_install=viewer) for match in doc.get("matches", [])],
                **({"reason": doc["reason"]} if doc.get("reason") else {}),
            }
        if len(parts) == 4 and parts[3] == "register":
            if method == "POST":
                return 200, _register(service, tournament_id, body, now)
            if method == "DELETE":
                return 200, _unregister(service, tournament_id, body, now)
        if len(parts) == 6 and parts[3] == "matches" and parts[5] == "checkin" and method == "POST":
            return 200, _checkin(service, tournament_id, parts[4], body, now)
        if len(parts) == 6 and parts[3] == "matches" and parts[5] == "result" and method == "POST":
            raise ServiceError(404, "not_found")
    raise ServiceError(404, "not_found")


def _admin_create(service, body: dict[str, Any], now: int) -> dict[str, Any]:
    if body.get("size") != "1v1":
        raise RequestError(400, "invalid_field", "size")
    name = text(required(body, "name"), "name", maximum=128)
    mode = required(body, "mode")
    if mode not in ("faithful", "uber"):
        raise RequestError(400, "invalid_field", "mode")
    start = integer(required(body, "start"), "start", maximum=now + TOURNAMENT_TTL)
    maximum = integer(required(body, "max_players"), "max_players", minimum=2, maximum=128)
    if maximum & (maximum - 1):
        raise RequestError(400, "invalid_field", "max_players")
    rules = text(body.get("rules", ""), "rules", minimum=0, maximum=4096)
    tournament_id = secrets.token_hex(16)
    doc = {
        "id": tournament_id, "name": name, "mode": mode, "size": "1v1", "start": start,
        "max_players": maximum, "rules": rules, "state": "upcoming", "registrants": [],
        "matches": [], "created_at": now,
    }
    for _ in range(3):
        expected, write = _put_pair(service.store, f"TOURNAMENT#{tournament_id}", "DOC", doc, now + TOURNAMENT_TTL)
        index_expected, index_write = _put_pair(service.store, "TOURNAMENTS", tournament_id, _summary(doc),
                                                 now + TOURNAMENT_TTL)
        expected.update(index_expected)
        if service.store.transact(expected, [write, index_write]):
            return {"id": tournament_id, "state": "upcoming"}
    raise ServiceError(503, "storage_busy")


def _admin_edit(service, tournament_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            raise ServiceError(404, "tournament_not_found")
        if doc["state"] != "upcoming":
            raise ServiceError(409, "tournament_not_editable")
        if "name" in body:
            doc["name"] = text(body["name"], "name", maximum=128)
        if "mode" in body:
            if body["mode"] not in ("faithful", "uber"):
                raise RequestError(400, "invalid_field", "mode")
            doc["mode"] = body["mode"]
        if "size" in body and body["size"] != "1v1":
            raise RequestError(400, "invalid_field", "size")
        if "start" in body:
            doc["start"] = integer(body["start"], "start", maximum=now + TOURNAMENT_TTL)
        if "max_players" in body:
            maximum = integer(body["max_players"], "max_players", minimum=2, maximum=128)
            if maximum & (maximum - 1) or maximum < len(doc["registrants"]):
                raise RequestError(400, "invalid_field", "max_players")
            doc["max_players"] = maximum
        if "rules" in body:
            doc["rules"] = text(body["rules"], "rules", minimum=0, maximum=4096)
        if _persist_tournament(service, row, doc, now):
            return {"id": tournament_id, "state": doc["state"]}
    raise ServiceError(503, "storage_busy")


def _admin_cancel(service, tournament_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            raise ServiceError(404, "tournament_not_found")
        if doc["state"] != "upcoming":
            raise ServiceError(409, "tournament_not_cancellable")
        doc["state"] = "cancelled"
        if _persist_tournament(service, row, doc, now):
            return {"id": tournament_id, "state": "cancelled"}
    raise ServiceError(503, "storage_busy")


def _register(service, tournament_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    install, nick = service_install_nick(body)
    client = client_metadata(required(body, "client"))
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            raise ServiceError(404, "tournament_not_found")
        if doc["state"] != "upcoming" or now >= int(doc["start"]):
            raise ServiceError(409, "registration_closed")
        existing = next((p for p in doc["registrants"] if p["install"] == install), None)
        if existing:
            if existing["nick"] == nick:
                return {"registered": True}
            raise ServiceError(409, "registration_conflict")
        if len(doc["registrants"]) >= doc["max_players"]:
            raise ServiceError(409, "tournament_full")
        doc["registrants"].append(_participant(install, nick, len(doc["registrants"]) + 1))
        if _persist_tournament(service, row, doc, now, activity=(install, client)):
            return {"registered": True}
    raise ServiceError(503, "storage_busy")


def _unregister(service, tournament_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    install = normalize_install(required(body, "install"))
    client = client_metadata(required(body, "client"))
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            return {"registered": False}
        if doc["state"] != "upcoming":
            raise ServiceError(409, "registration_closed")
        updated = [p for p in doc["registrants"] if p["install"] != install]
        if len(updated) == len(doc["registrants"]):
            return {"registered": False}
        doc["registrants"] = [_participant(p["install"], p["nick"], i + 1) for i, p in enumerate(updated)]
        if _persist_tournament(service, row, doc, now, activity=(install, client)):
            return {"registered": False}
    raise ServiceError(503, "storage_busy")


def _checkin(service, tournament_id: str, match_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    install = normalize_install(required(body, "install"))
    client = client_metadata(required(body, "client"))
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            raise ServiceError(404, "tournament_not_found")
        match = next((m for m in doc.get("matches", []) if m["id"] == match_id), None)
        if not match:
            raise ServiceError(404, "match_not_found")
        changed = _sweep_match(doc, match, now)
        if match.get("status") not in ("open", "ready"):
            if changed and not _persist_tournament(service, row, doc, now):
                continue
            raise ServiceError(409, "checkin_closed")
        if now >= int(match["opened_at"]) + 900:
            if changed and not _persist_tournament(service, row, doc, now):
                continue
            raise ServiceError(409, "checkin_closed")
        participant = next((p for p in match["participants"] if p and p["install"] == install), None)
        if not participant:
            raise ServiceError(403, "not_a_match_participant")
        checked = list(match.get("checked_in", []))
        if install in checked:
            return {"state": match["status"]}
        checked.append(install)
        match["checked_in"] = checked
        if len(checked) == 2:
            match["status"] = "ready"
        if _persist_tournament(service, row, doc, now, activity=(install, client)):
            return {"state": match["status"]}
    raise ServiceError(503, "storage_busy")


def _advance_due(service, now: int) -> int:
    changed_count = 0
    checkpoint = service.store.get("TOURNAMENTS", "!SWEEP")
    rows, cursor = service.store.query_page(
        "TOURNAMENTS", cursor=checkpoint["payload"]["cursor"] if checkpoint else None,
    )
    for index_row in rows:
        if int(index_row.get("expires_at", 0)) <= now:
            continue
        tournament_id = index_row["sk"]
        for _ in range(3):
            row, doc = _read(service, tournament_id, now)
            if not row:
                break
            changed = False
            if doc["state"] == "upcoming" and now >= int(doc["start"]):
                _seed_bracket(doc, now)
                changed = True
            for match in doc.get("matches", []):
                if _sweep_match(doc, match, now):
                    changed = True
            if not changed:
                break
            if _persist_tournament(service, row, doc, now):
                changed_count += 1
                break
    service.store.put("TOURNAMENTS", "!SWEEP", {"cursor": cursor})
    return changed_count


def prepare_result_updates(store, report: dict[str, Any], reports: dict[str, dict[str, Any]], now: int):
    game_id = report["game"]["id"]
    if "." not in game_id:
        return {}
    tournament_id, suffix = game_id.split(".", 1)
    if len(tournament_id) != 32 or len(suffix) != 24:
        return {}
    row = store.get(f"TOURNAMENT#{tournament_id}", "DOC")
    if not row or int(row.get("expires_at", 0)) <= now:
        return {}
    doc = dict(row["payload"])
    match = next((m for m in doc.get("matches", []) if m["id"] == game_id), None)
    if not match:
        return {}
    participants = [p for p in match["participants"] if p]
    participant_ids = {p["install"] for p in participants}
    incoming = report["install"]
    if incoming not in participant_ids:
        raise ServiceError(403, "not_a_match_participant")
    if match.get("status") not in ("open", "ready"):
        return {}
    if _sweep_match(doc, match, now):
        return _tournament_writes(store, row, doc, now)

    def valid_game(candidate):
        game = candidate["game"]
        players = game["players"]
        own = next((p for p in participants if p["install"] == candidate["install"]), None)
        own_player = next((player for player in players if player["slot"] == candidate["me"]["slot"]), None)
        return (
            game["id"] == match["id"]
            and game["mode"] == doc["mode"]
            and len(players) == 2
            and all(player["human"] for player in players)
            and len({player["team"] for player in players}) == 2
            and Counter(player["nick"] for player in players) == Counter(p["nick"] for p in participants)
            and own is not None
            and own_player is not None
            and own["nick"] == own_player["nick"]
        )

    if not valid_game(report):
        match["status"] = "disputed"
        return _tournament_writes(store, row, doc, now)
    valid_reports = [r for install, r in reports.items() if install in participant_ids]
    if len(valid_reports) < 2 or {r["install"] for r in valid_reports} != participant_ids:
        return {}
    if not all(valid_game(candidate) for candidate in valid_reports):
        match["status"] = "disputed"
        return _tournament_writes(store, row, doc, now)
    quorum = stats.confirmed_game(valid_reports)
    if quorum is None:
        match["status"] = "disputed"
        return _tournament_writes(store, row, doc, now)
    canonical, _ = quorum
    if canonical["winner_team"] is None:
        match["status"] = "disputed"
    else:
        winner_slot = next(p["slot"] for p in canonical["players"] if p["team"] == canonical["winner_team"])
        winner_report = next(r for r in valid_reports if r["me"]["slot"] == winner_slot)
        match["winner"] = next(p for p in participants if p["install"] == winner_report["install"])
        match["status"] = "finished"
        match["resolved_at"] = now
        _advance(doc, now)
    return _tournament_writes(store, row, doc, now)


def quorum_reports(store, reports: dict[str, dict[str, Any]], game_id: str):
    """Restrict a reserved tournament match ID to its registered participants and roster."""
    if "." not in game_id:
        return None
    tournament_id, suffix = game_id.split(".", 1)
    if len(tournament_id) != 32 or len(suffix) != 24:
        return None
    row = store.get(f"TOURNAMENT#{tournament_id}", "DOC")
    if not row:
        return None
    doc = row["payload"]
    match = next((item for item in doc.get("matches", []) if item["id"] == game_id), None)
    if not match:
        return None
    participants = [p for p in match.get("participants", []) if p]
    participant_ids = {p["install"] for p in participants}

    def valid(candidate):
        game = candidate["game"]
        players = game["players"]
        own = next((p for p in participants if p["install"] == candidate["install"]), None)
        own_player = next((p for p in players if p["slot"] == candidate["me"]["slot"]), None)
        return bool(
            own and game["mode"] == doc["mode"] and len(players) == 2
            and all(player["human"] for player in players)
            and len({player["team"] for player in players}) == 2
            and Counter(player["nick"] for player in players) == Counter(p["nick"] for p in participants)
            and own_player is not None and own_player["nick"] == own["nick"]
        )

    return [candidate for install, candidate in reports.items()
            if install in participant_ids and valid(candidate)]


def _tournament_writes(store, row, doc, now):
    index_key = key("TOURNAMENTS", doc["id"])
    index_row = store.get(*index_key)
    return {
        key(row["pk"], row["sk"]): (row["version"], {
            "pk": row["pk"], "sk": row["sk"], "payload": doc, "expires_at": now + TOURNAMENT_TTL,
        }),
        index_key: (index_row["version"] if index_row else None, {
            "pk": "TOURNAMENTS", "sk": doc["id"], "payload": _summary(doc),
            "expires_at": now + TOURNAMENT_TTL,
        }),
    }


def _admin_resolve(service, tournament_id: str, match_id: str, body: dict[str, Any], now: int) -> dict[str, Any]:
    winner_install = normalize_install(required(body, "winner"))
    for _ in range(3):
        row, doc = _read(service, tournament_id, now)
        if not row:
            raise ServiceError(404, "tournament_not_found")
        match = next((m for m in doc.get("matches", []) if m["id"] == match_id), None)
        if not match:
            raise ServiceError(404, "match_not_found")
        if match.get("status") != "disputed":
            raise ServiceError(409, "match_not_disputed")
        winner = next((p for p in match["participants"] if p and p["install"] == winner_install), None)
        if winner is None:
            raise RequestError(400, "invalid_field", "winner")
        match["winner"] = winner
        match["status"] = "admin_resolved"
        match["resolved_at"] = now
        _advance(doc, now)
        if _persist_tournament(service, row, doc, now):
            return {"state": match["status"]}
    raise ServiceError(503, "storage_busy")


def _install_and_nick(body):
    install, nick = service_install_nick(body)
    return install, nick


def service_install_nick(body):
    install = normalize_install(required(body, "install"))
    nick = text(required(body, "nick"), "nick", maximum=64)
    return install, nick
