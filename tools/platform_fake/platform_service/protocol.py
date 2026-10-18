from __future__ import annotations

import base64
import binascii
import json
import unicodedata
from typing import Any

from platform_service import identity, stats

MAX_BODY = 16_384
YEAR = 365 * 86_400
MAX_DEPTH = 16


class RequestError(Exception):
    def __init__(self, status: int, code: str, field: str | None = None):
        super().__init__(code)
        self.status = status
        self.code = code
        self.field = field


def normalize_install(value: Any) -> str:
    try:
        return identity.normalize_install(value)
    except identity.IdentityError as error:
        raise RequestError(400, error.code, error.field) from None


def canonical_path(value: Any) -> tuple[str, ...]:
    if not isinstance(value, str) or not value.startswith("/") or value == "/" or value.endswith("/"):
        raise RequestError(404, "not_found")
    if "%" in value or "\\" in value or "//" in value:
        raise RequestError(404, "not_found")
    parts = tuple(value[1:].split("/"))
    if any(not part or part in (".", "..") for part in parts):
        raise RequestError(404, "not_found")
    return parts


def client_metadata(value: Any) -> dict:
    try:
        return identity.client_metadata(value)
    except identity.IdentityError as error:
        raise RequestError(400, error.code, error.field) from None


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate")
        result[key] = value
    return result


def _constant(value: str) -> None:
    raise ValueError("constant")


def _depth(value: Any, level: int = 0) -> None:
    if level > MAX_DEPTH:
        raise ValueError("depth")
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise ValueError("key")
            _depth(child, level + 1)
    elif isinstance(value, list):
        for child in value:
            _depth(child, level + 1)


def parse_event_body(event: dict[str, Any]) -> dict[str, Any]:
    headers = event.get("headers") or {}
    if not isinstance(headers, dict):
        raise RequestError(400, "invalid_headers")
    lowered = {}
    for key, value in headers.items():
        if not isinstance(key, str) or not isinstance(value, str) or key.lower() in lowered:
            raise RequestError(400, "invalid_headers")
        lowered[key.lower()] = value
    content_length = lowered.get("content-length")
    if content_length is not None:
        if not content_length.isascii() or not content_length.isdecimal() or len(content_length) > 8:
            raise RequestError(400, "invalid_content_length")
        if int(content_length) > MAX_BODY:
            raise RequestError(413, "body_too_large")
    content_type = lowered.get("content-type", "").split(";", 1)[0].strip().lower()
    if content_type != "application/json":
        raise RequestError(415, "unsupported_content_type")
    if lowered.get("content-encoding", "identity").lower() != "identity":
        raise RequestError(415, "unsupported_content_encoding")
    body = event.get("body")
    if not isinstance(body, str):
        raise RequestError(400, "invalid_json")
    try:
        if event.get("isBase64Encoded") is True:
            encoded = body.encode("ascii")
            if len(encoded) > 4 * ((MAX_BODY + 2) // 3):
                raise RequestError(413, "body_too_large")
            raw = base64.b64decode(encoded, validate=True)
            if base64.b64encode(raw) != encoded:
                raise RequestError(400, "invalid_base64")
        elif event.get("isBase64Encoded", False) is False:
            raw = body.encode("utf-8")
        else:
            raise RequestError(400, "invalid_json")
    except (UnicodeEncodeError, binascii.Error, ValueError):
        raise RequestError(400, "invalid_base64") from None
    if len(raw) > MAX_BODY:
        raise RequestError(413, "body_too_large")
    if content_length is not None and int(content_length) != len(raw):
        raise RequestError(400, "invalid_content_length")
    try:
        result = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_constant)
        _depth(result)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, RecursionError):
        raise RequestError(400, "invalid_json") from None
    if not isinstance(result, dict):
        raise RequestError(400, "invalid_json")
    if type(result.get("v")) is not int or result["v"] != 1:
        raise RequestError(400, "invalid_version", "v")
    return result


def required(value: dict[str, Any], name: str) -> Any:
    if name not in value:
        raise RequestError(400, "missing_field", name)
    return value[name]


def text(value: Any, field: str, *, minimum: int = 1, maximum: int = 128) -> str:
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        raise RequestError(400, "invalid_field", field)
    return value


def integer(value: Any, field: str, *, minimum: int = 0, maximum: int | None = None) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or (maximum is not None and value > maximum)
    ):
        raise RequestError(400, "invalid_field", field)
    return value


def dimension(value: Any, kind: str, field: str) -> str:
    if isinstance(value, str):
        value = unicodedata.normalize("NFC", value)
    value = text(value, field)
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        raise RequestError(400, "invalid_field", field) from None
    if len(("vs_ai#" + stats.counter_key(kind, value)).encode("utf-8")) > stats.MAX_COUNTER_KEY_BYTES:
        raise RequestError(400, "invalid_field", field)
    return value


def page_cursor(value: Any) -> str | None:
    if value is None:
        return None
    value = text(value, "cursor", maximum=256)
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        raise RequestError(400, "invalid_field", "cursor") from None
    return value


def validate_result(body: dict[str, Any], now: int) -> dict[str, Any]:
    install = normalize_install(required(body, "install"))
    client = client_metadata(required(body, "client"))
    nick = text(required(body, "nick"), "nick", maximum=64)
    raw_game = required(body, "game")
    raw_me = required(body, "me")
    if not isinstance(raw_game, dict):
        raise RequestError(400, "invalid_field", "game")
    if not isinstance(raw_me, dict):
        raise RequestError(400, "invalid_field", "me")
    game = {
        "id": text(required(raw_game, "id"), "game.id"),
        "mode": required(raw_game, "mode"),
        "map": dimension(required(raw_game, "map"), "map", "game.map"),
    }
    if game["mode"] not in ("faithful", "uber"):
        raise RequestError(400, "invalid_field", "game.mode")
    start = integer(required(raw_game, "start"), "game.start")
    end = integer(required(raw_game, "end"), "game.end")
    duration = integer(required(raw_game, "duration_s"), "game.duration_s", maximum=86_400)
    if end < start:
        raise RequestError(400, "invalid_field", "game.end")
    if end > now + 300 or end < now - YEAR:
        raise RequestError(400, "invalid_field", "game.end")
    if duration > end - start + 300:
        raise RequestError(400, "invalid_field", "game.duration_s")
    raw_players = required(raw_game, "players")
    if not isinstance(raw_players, list) or not 2 <= len(raw_players) <= 8:
        raise RequestError(400, "invalid_field", "game.players")
    players = []
    slots = set()
    teams = set()
    for index, raw in enumerate(raw_players):
        field = f"game.players.{index}"
        if not isinstance(raw, dict):
            raise RequestError(400, "invalid_field", field)
        slot = integer(required(raw, "slot"), f"{field}.slot", maximum=7)
        team = integer(required(raw, "team"), f"{field}.team", maximum=7)
        human = required(raw, "human")
        if not isinstance(human, bool):
            raise RequestError(400, "invalid_field", f"{field}.human")
        if slot in slots:
            raise RequestError(400, "invalid_field", f"{field}.slot")
        slots.add(slot)
        teams.add(team)
        players.append({
            "slot": slot,
            "nick": text(required(raw, "nick"), f"{field}.nick", maximum=64),
            "faction": dimension(required(raw, "faction"), "faction", f"{field}.faction"),
            "team": team,
            "human": human,
        })
    winner = required(raw_game, "winner_team")
    if winner is not None:
        winner = integer(winner, "game.winner_team", maximum=7)
        if winner not in teams:
            raise RequestError(400, "invalid_field", "game.winner_team")
    game.update({
        "players": players,
        "start": start,
        "end": end,
        "duration_s": duration,
        "winner_team": winner,
    })
    if any(len(counter.encode("utf-8")) > stats.MAX_COUNTER_KEY_BYTES for counter in stats.contribution(game, "vs_ai")):
        raise RequestError(400, "invalid_field", "game.players")
    replay_hash = raw_game.get("replay_hash")
    if replay_hash is not None:
        game["replay_hash"] = text(replay_hash, "game.replay_hash", maximum=128)
    me_slot = integer(required(raw_me, "slot"), "me.slot", maximum=7)
    if me_slot not in {player["slot"] for player in players if player["human"]}:
        raise RequestError(400, "invalid_field", "me.slot")
    return {"install": install, "nick": nick, "client": client, "game": game, "me": {"slot": me_slot}}


def validate_install_body(body: dict[str, Any]) -> tuple[str, str]:
    return normalize_install(required(body, "install")), text(required(body, "nick"), "nick", maximum=64)


def response(status: int, payload: dict[str, Any], *, headers: dict[str, str] | None = None) -> dict[str, Any]:
    body = {"v": 1, **payload}
    serialized = json.dumps(body, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    response_headers = {"content-type": "application/json"}
    if headers:
        response_headers.update(headers)
    return {"statusCode": status, "headers": response_headers, "body": serialized}


def error_response(error: RequestError) -> dict[str, Any]:
    detail: dict[str, Any] = {"code": error.code}
    if error.field is not None:
        detail["field"] = error.field
    headers = {"retry-after": "5"} if error.status in (429, 503) else None
    return response(error.status, {"error": detail}, headers=headers)
