"""Bounded install summaries with write-time exact rankings and streaks."""
from __future__ import annotations

import hashlib
import hmac
import json

from .identity import aggregate_install_hash
from .protocol import RequestError

DISPLAY_LIMIT = 20
EMPTY = {"games": 0, "wins": 0, "prefix": 0, "suffix": 0, "longest": 0}


def partition(server_key: bytes, install: str) -> str:
    return "PSTATS#" + aggregate_install_hash(server_key, install)


def join(left: dict, right: dict) -> dict:
    return {
        "games": left["games"] + right["games"],
        "wins": left["wins"] + right["wins"],
        "prefix": left["prefix"] + (right["prefix"] if left["prefix"] == left["games"] else 0),
        "suffix": right["suffix"] + (left["suffix"] if right["suffix"] == right["games"] else 0),
        "longest": max(left["longest"], right["longest"], left["suffix"] + right["prefix"]),
    }


def prepare(store, server_key: bytes, report: dict):
    """At most 37 CAS operations, independent of the install's history length."""
    pk = partition(server_key, report["install"])
    game = report["game"]
    digest = hmac.new(server_key, ("game:" + game["id"]).encode(), hashlib.sha256).hexdigest()
    fingerprint = hmac.new(server_key, json.dumps(
        {"game": game, "me": report["me"]}, sort_keys=True, ensure_ascii=False, separators=(",", ":"),
    ).encode(), hashlib.sha256).hexdigest()
    ledger_sk = "ID#" + digest
    ledger = store.get(pk, ledger_sk)
    if ledger:
        if ledger["payload"]["fingerprint"] != fingerprint:
            raise RequestError(409, "report_conflict")
        return None
    summary_row = store.get(pk, "SUMMARY")
    summary = dict(summary_row["payload"]) if summary_row else {}
    sequence = summary.get("games", 0) + 1
    if not 0 <= game["end"] < 2**64 or sequence >= 2**64:
        raise RequestError(409, "personal_summary_limit")
    path = f"{game['end']:016x}{sequence:016x}"
    expected = {(pk, "SUMMARY"): summary_row["version"] if summary_row else None,
                (pk, ledger_sk): None, (pk, "SEQ#" + path): None}
    player = next(p for p in game["players"] if p["slot"] == report["me"]["slot"])
    won = int(game["winner_team"] is not None and player["team"] == game["winner_team"])
    segment = {"games": 1, "wins": won, "prefix": won, "suffix": won, "longest": won}
    writes = [{"pk": pk, "sk": ledger_sk, "payload": {"fingerprint": fingerprint}},
              {"pk": pk, "sk": "SEQ#" + path, "payload": segment}]
    for depth in reversed(range(len(path))):
        sk = "SEQ#" + path[:depth]
        row = store.get(pk, sk)
        children = dict(row["payload"]["children"]) if row else {}
        children[path[depth]] = segment
        segment = dict(EMPTY)
        for digit in sorted(children):
            segment = join(segment, children[digit])
        expected[(pk, sk)] = row["version"] if row else None
        writes.append({"pk": pk, "sk": sk, "payload": {"children": children}})
    summary.update(games=segment["games"], wins=segment["wins"], longest_win_streak=segment["longest"])
    for kind, name, label, count_field, collection in (
        ("faction", player["faction"], "faction", "picks", "factions"),
        ("map", game["map"], "map", "games", "maps"),
    ):
        sk = "DIM#" + kind + "#" + hashlib.sha256(name.encode()).hexdigest()
        row = store.get(pk, sk)
        data = dict(row["payload"]) if row else {
            label: name,
            count_field: 0,
            **({"wins": 0} if kind == "faction" else {}),
        }
        if data[label] != name:
            raise RequestError(409, "dimension_hash_collision")
        data[count_field] += 1
        if kind == "faction":
            data["wins"] += won
        expected[(pk, sk)] = row["version"] if row else None
        writes.append({"pk": pk, "sk": sk, "payload": data})
        ranked = [entry for entry in summary.get(collection, []) if entry[label] != name] + [data]
        summary[collection] = sorted(ranked, key=lambda entry: (-entry[count_field], entry[label]))[:DISPLAY_LIMIT]
    writes.append({"pk": pk, "sk": "SUMMARY", "payload": summary})
    return expected, writes, sequence


def view(summary: dict | None) -> dict:
    summary = summary or {"games": 0, "wins": 0, "longest_win_streak": 0, "factions": [], "maps": []}
    games, wins = summary["games"], summary["wins"]
    factions = [dict(row) for row in summary["factions"]]
    overflow = games - sum(row["picks"] for row in factions)
    if overflow:
        factions.append({"faction": "other", "picks": overflow,
                         "wins": wins - sum(row["wins"] for row in factions), "overflow": True})
    for row in factions:
        row["win_rate"] = row["wins"] / row["picks"] if row["picks"] else 0
    return {"v": 1, "games": games, "wins": wins, "factions": factions,
            "favourite_faction": summary["factions"][0]["faction"] if summary["factions"] else None,
            "favourite_map": summary["maps"][0]["map"] if summary["maps"] else None,
            "longest_win_streak": summary["longest_win_streak"], "totals_scope": "install"}
