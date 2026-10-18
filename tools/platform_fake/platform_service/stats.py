"""Integer-only aggregate deltas; rates are computed only in API projections."""
from __future__ import annotations

import hashlib
import json
import re
import unicodedata
from collections import Counter, defaultdict

DAY = 86400
YEAR = 365 * DAY
DIMENSION_CAP = 64
DISPLAY_LIMIT = 20
MAX_COUNTER_KEY_BYTES = 255
OVERFLOW = "~overflow"


def dimension_token(name: str) -> str:
    name = unicodedata.normalize("NFC", name)
    if re.fullmatch(r"[A-Za-z0-9 _.-]{1,32}", name):
        return name
    prefix = re.sub(r"[^A-Za-z0-9_.-]", "_", name[:24])
    return prefix + "~" + hashlib.sha256(name.encode("utf-8")).hexdigest()


def agreement_key(game: dict) -> str:
    """Timing/replay metadata may differ between clients; roster/outcome may not."""
    value = {k: game[k] for k in ("id", "mode", "map", "winner_team")}
    value["players"] = sorted(
        ({k: p[k] for k in ("slot", "nick", "faction", "team", "human")} for p in game["players"]),
        key=lambda p: p["slot"],
    )
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def confirmed_game(reports: list[dict]) -> tuple[dict, str] | None:
    """Return the first quorum, counting distinct installs AND distinct human slots."""
    groups = defaultdict(list)
    for report in reports:
        game = report["game"]
        humans = [p for p in game["players"] if p["human"]]
        me = report["me"]["slot"]
        if me not in {p["slot"] for p in humans}:
            continue
        if len(humans) == 1 and any(not p["human"] for p in game["players"]):
            return game, "vs_ai"
        group = groups[agreement_key(game)]
        if any(r["install"] == report["install"] or r["me"]["slot"] == me for r in group):
            continue
        group.append(report)
        if len(group) >= 2:
            return group[0]["game"], "pvp"
    return None


def counter_key(kind: str, *names: str) -> str:
    return kind + "#" + json.dumps([dimension_token(n) for n in names], separators=(",", ":"))


def label_key(kind: str, names) -> str:
    return kind + "#" + json.dumps([unicodedata.normalize("NFC", n) for n in names],
                                   ensure_ascii=False, separators=(",", ":"))


def counter_writes(store, pk: str, game: dict, bucket: str, expires_at: int | None):
    """Fixed-size dimension documents share the result's conditional transaction."""
    names = sorted({p["faction"] for p in game["players"]})
    labels = {counter_key("map", game["map"]): [game["map"]]}
    labels.update({counter_key("faction", n): [n] for n in names})
    labels.update({counter_key("matchup", a, b): [a, b]
                   for i, a in enumerate(names) for b in names[i + 1:]})
    expected, documents = {}, {}
    for sk, delta in contribution(game, bucket).items():
        _, kind, *_ = sk.split("#", 2)
        document_sk = bucket + "#" + kind
        if document_sk not in documents:
            row = store.get(pk, document_sk)
            expected[pk, document_sk] = row["version"] if row else None
            documents[document_sk] = row["payload"] if row else (
                {"counts": {}} if kind == "total" else {"entries": {}, "overflow": {}}
            )
        document = documents[document_sk]
        if kind == "total":
            target = document["counts"]
        else:
            entries = document["entries"]
            if sk not in entries and len(entries) < DIMENSION_CAP:
                entries[sk] = {"names": labels[sk[len(bucket) + 1:]], "counts": {}}
            target = entries[sk]["counts"] if sk in entries else document["overflow"]
        for field, value in delta.items():
            target[field] = target.get(field, 0) + value
    return expected, [{"pk": pk, "sk": sk, "payload": payload,
                       **({"expires_at": expires_at} if expires_at is not None else {})}
                      for sk, payload in documents.items()]


def row_counters(row: dict) -> dict:
    payload = row["payload"]
    if "entries" not in payload:
        return {row["sk"]: payload["counts"]}
    bucket, kind = row["sk"].split("#")
    counters = {bucket + "#" + label_key(kind, entry["names"]): entry["counts"]
                for entry in payload["entries"].values()}
    if payload["overflow"]:
        counters[f"{bucket}#{kind}#{OVERFLOW}"] = payload["overflow"]
    return counters


def contribution(game: dict, bucket: str) -> dict[str, dict[str, int]]:
    """One game contributes once; matchups exclude mirror/ambiguous team factions."""
    out = {bucket + "#total": {"games": 1, "duration_s": game["duration_s"]}}
    factions = defaultdict(Counter)
    faction_teams = defaultdict(set)
    for player in game["players"]:
        name = player["faction"]
        factions[name]["picks"] += 1
        factions[name]["wins"] += int(game["winner_team"] is not None and player["team"] == game["winner_team"])
        faction_teams[name].add(player["team"])
    for name, counts in sorted(factions.items()):
        out[bucket + "#" + counter_key("faction", name)] = dict(counts)
    names = sorted(faction_teams)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            ta, tb = faction_teams[a], faction_teams[b]
            if len(ta) == len(tb) == 1 and ta != tb:
                out[bucket + "#" + counter_key("matchup", a, b)] = {
                    "games": 1, "a_wins": int(game["winner_team"] in ta),
                }
    out[bucket + "#" + counter_key("map", game["map"])] = {"games": 1}
    return out


def merge_counters(groups) -> dict[str, dict[str, int]]:
    merged = defaultdict(Counter)
    retained = defaultdict(set)
    for group in groups:
        for key, counts in group.items():
            if not key.endswith("#total"):
                bucket, kind, names = key.split("#", 2)
                prefix = bucket + "#" + kind
                if names != OVERFLOW:
                    key = bucket + "#" + label_key(kind, json.loads(names))
                    if key not in retained[prefix] and len(retained[prefix]) >= DIMENSION_CAP:
                        key = prefix + "#" + OVERFLOW
                    else:
                        retained[prefix].add(key)
            merged[key].update(counts)
    return {key: dict(counts) for key, counts in merged.items()}


def _project(counters: dict[str, dict[str, int]], bucket: str) -> dict:
    totals = counters.get(bucket + "#total", {})
    games = totals.get("games", 0)
    factions, matchups, maps = [], [], []
    overflow = defaultdict(Counter)
    for key, counts in sorted(counters.items()):
        if not key.startswith(bucket + "#") or key == bucket + "#total":
            continue
        kind, names_json = key[len(bucket) + 1:].split("#", 1)
        if names_json == OVERFLOW:
            overflow[kind].update(counts)
            continue
        names = json.loads(names_json)
        if kind == "faction":
            picks, wins = counts["picks"], counts.get("wins", 0)
            factions.append({"faction": names[0], "picks": picks, "wins": wins,
                             "win_rate": wins / picks if picks else 0})
        elif kind == "matchup":
            matchups.append({"a": names[0], "b": names[1], "games": counts["games"],
                             "a_wins": counts.get("a_wins", 0)})
        elif kind == "map":
            maps.append({"map": names[0], "games": counts["games"]})
        else:
            raise ValueError("unknown aggregate counter")
    for kind, values, count_field, fields, labels in (
        ("faction", factions, "picks", ("picks", "wins"), ("faction",)),
        ("map", maps, "games", ("games",), ("map",)),
        ("matchup", matchups, "games", ("games", "a_wins"), ("a", "b")),
    ):
        values.sort(key=lambda value: (-value[count_field], *(value[label] for label in labels)))
        for value in values[DISPLAY_LIMIT:]:
            overflow[kind].update({field: value[field] for field in fields})
        del values[DISPLAY_LIMIT:]
        if overflow[kind]:
            values.append({**{label: "other" for label in labels},
                           **{field: overflow[kind][field] for field in fields}, "overflow": True})
    picks = sum(f["picks"] for f in factions)
    for faction in factions:
        faction["pick_rate"] = faction["picks"] / picks if picks else 0
        faction["win_rate"] = faction["wins"] / faction["picks"] if faction["picks"] else 0
    result = {"games": games, "factions": factions, "matchups": matchups, "maps": maps,
              "avg_duration_s": totals.get("duration_s", 0) / games if games else 0}
    if any(overflow.values()):
        result["dimension_policy"] = {"retained_limit": DIMENSION_CAP, "display_limit": DISPLAY_LIMIT,
                                      "selection": "first_seen", "named_counts_may_be_partial": True}
    return result


def global_view(counters: dict[str, dict[str, int]], as_of: int) -> dict:
    counters = merge_counters([counters])
    return {"v": 1, "as_of": as_of, **_project(counters, "pvp"), "vs_ai": _project(counters, "vs_ai")}


def personal_view(reports: list[dict], now: int) -> dict:
    """Only the requesting install's reports; retention checked independently of DDB TTL."""
    reports = sorted((r for r in reports if r["received_at"] + YEAR > now),
                     key=lambda r: (r["game"]["end"], r["game"]["id"]))
    factions, maps = defaultdict(Counter), Counter()
    wins = streak = longest = 0
    history = []
    for report in reports:
        game = report["game"]
        player = next(p for p in game["players"] if p["slot"] == report["me"]["slot"])
        won = game["winner_team"] is not None and player["team"] == game["winner_team"]
        wins += int(won)
        streak = streak + 1 if won else 0
        longest = max(longest, streak)
        factions[player["faction"]].update({"picks": 1, "wins": int(won)})
        maps[game["map"]] += 1
        history.append({"id": game["id"], "mode": game["mode"], "map": game["map"],
                        "end": game["end"], "duration_s": game["duration_s"],
                        "faction": player["faction"], "won": won, "winner_team": game["winner_team"]})
    favourite_faction = min(factions, key=lambda f: (-factions[f]["picks"], f)) if factions else None
    favourite_map = min(maps, key=lambda m: (-maps[m], m)) if maps else None
    return {"v": 1, "games": len(reports), "wins": wins,
            "factions": [{"faction": name, **dict(counts), "win_rate": counts["wins"] / counts["picks"]}
                         for name, counts in sorted(factions.items())],
            "favourite_faction": favourite_faction, "favourite_map": favourite_map,
            "longest_win_streak": longest, "last_games": list(reversed(history[-20:]))}
