from __future__ import annotations

import hashlib
import hmac
import re
import secrets
import time
from typing import Any, Callable

from platform_service import personal, stats
from platform_service.metrics import Metrics
from platform_service.protocol import (
    YEAR,
    RequestError,
    canonical_path,
    client_metadata,
    normalize_install,
    page_cursor,
    required,
    text,
    validate_install_body,
    validate_result,
)
from platform_service.storage import key

DAY = 86_400
MAX_GAME_REPORTS = 8
MAX_RETRIES = 3
TICKET_PATTERN = re.compile(r"^[0-9a-f]{32}$")


class ServiceError(Exception):
    def __init__(self, status: int, code: str, field: str | None = None):
        super().__init__(code)
        self.status = status
        self.code = code
        self.field = field


class Admission:
    def __init__(self, store, server_key: bytes):
        self.store = store
        self.server_key = server_key

    def _hash(self, kind: str, day: int, value: str) -> str:
        return hmac.new(self.server_key, f"{kind}:{day}:{value}".encode(), hashlib.sha256).hexdigest()

    def _increment(self, counters: list[tuple[str, str, int, int]], now: int) -> bool:
        for _ in range(MAX_RETRIES):
            expected = {}
            puts = []
            exceeded = False
            for pk, sk, maximum, ttl in counters:
                row = self.store.get(pk, sk)
                count = int(row["payload"].get("count", 0)) if row else 0
                if count >= maximum:
                    exceeded = True
                    break
                expected[(pk, sk)] = row["version"] if row else None
                puts.append({
                    "pk": pk, "sk": sk, "payload": {"count": count + 1},
                    "expires_at": now + ttl,
                })
            if exceeded:
                return False
            if self.store.transact(expected, puts):
                return True
        return False

    def source_and_global(self, source_ip: str, now: int, *, admin: bool = False) -> bool:
        day = now // DAY
        hour = now // 3600
        source_value = "admin" if admin else source_ip
        source_hash = self._hash("source", day, source_value)
        daily_key = f"LIMIT#ADMIN#{day}" if admin else f"LIMIT#GLOBAL#{day}"
        return self._increment([
            (f"LIMIT#SOURCE#{hour}", source_hash, 600, 48 * 3600),
            (daily_key, "COUNT", 1000 if admin else 10_000, 48 * 3600),
        ], now)

    def install(self, install: str, now: int, *, admin: bool = False) -> bool:
        day = now // DAY
        hour = now // 3600
        value = "admin" if admin else install
        digest = self._hash("install", day, value)
        return self._increment([(f"LIMIT#INSTALL#{hour}", digest, 120, 48 * 3600)], now)


class PlatformService:
    def __init__(self, store, limiter_store=None, *, server_key: bytes = b"", admin_token: str = "",
                 admin_token_provider: Callable[[], str] | None = None):
        self.store = store
        self.limiter_store = limiter_store or store
        self.server_key = server_key
        self.admin_token = admin_token
        self.admin_token_provider = admin_token_provider
        self.admission = Admission(self.limiter_store, server_key)
        self.metrics = Metrics(store, server_key)

    def _get(self, pk: str, sk: str) -> tuple[dict[str, Any] | None, dict[str, Any]]:
        row = self.store.get(pk, sk)
        return row, dict(row["payload"]) if row else {}

    def submit_result(self, body: dict[str, Any], now: int) -> tuple[int, dict[str, Any]]:
        report = validate_result(body, now)
        install = report["install"]
        game = report["game"]
        game_pk = f"GAME#{game['id']}"
        personal_pk = f"PERSONAL#{install}"
        report_sk = "REPORT#" + hmac.new(self.server_key, install.encode(), hashlib.sha256).hexdigest()
        for _ in range(MAX_RETRIES):
            game_row, game_doc = self._get(game_pk, "REPORTS")
            if game_row and int(game_row.get("expires_at", 0)) <= now:
                game_doc = {}
            references = {sk: expiry for sk, expiry in game_doc.get("reports", {}).items() if expiry > now}
            reports = {}
            for sk in references:
                stored = self.store.get(game_pk, sk)
                if stored is None or int(stored.get("expires_at", 0)) <= now:
                    raise ServiceError(503, "report_unavailable")
                prior = stored["payload"]["report"]
                reports[prior["install"]] = prior
            old = reports.get(install)
            if old:
                if self._same_report(old, report):
                    return 200, {"accepted": True, "confirmed": game_doc.get("canonical") is not None}
                raise ServiceError(409, "report_conflict")
            personal_updates = personal.prepare(self.store, self.server_key, report)
            if personal_updates is None:
                return 200, {"accepted": True, "confirmed": game_doc.get("canonical") is not None}
            personal_expected, personal_writes, sequence = personal_updates
            personal_sk = f"GAME#{now:012d}#{sequence:020d}"
            if len(reports) >= MAX_GAME_REPORTS:
                raise ServiceError(409, "game_report_limit")
            reports[install] = report
            references[report_sk] = now + YEAR
            from platform_service.tournaments import quorum_reports

            eligible_reports = quorum_reports(self.store, reports, game["id"])
            quorum = None if game_doc.get("canonical") else stats.confirmed_game(
                eligible_reports if eligible_reports is not None else list(reports.values())
            )
            canonical = game_doc.get("canonical")
            first_confirmation = canonical is None and quorum is not None
            if canonical is None and quorum is not None:
                canonical = {"game": quorum[0], "bucket": quorum[1]}
            game_doc = {"reports": references, "counters_written": bool(game_doc.get("counters_written"))}
            if canonical is not None:
                game_doc["canonical"] = True
            personal_row = self.store.get(personal_pk, personal_sk)
            raw_row = self.store.get(game_pk, report_sk)
            personal_payload = {"report": report, "received_at": now}
            expected: dict[tuple[str, str], int | None] = {
                key(game_pk, "REPORTS"): game_row["version"] if game_row else None,
                key(personal_pk, personal_sk): personal_row["version"] if personal_row else None,
                key(game_pk, report_sk): raw_row["version"] if raw_row else None,
            }
            writes = [
                {"pk": game_pk, "sk": "REPORTS", "payload": game_doc, "expires_at": max(references.values())},
                {"pk": game_pk, "sk": report_sk, "payload": personal_payload, "expires_at": now + YEAR},
                {"pk": personal_pk, "sk": personal_sk, "payload": personal_payload, "expires_at": now + YEAR},
            ]
            expected.update(personal_expected)
            writes.extend(personal_writes)
            if first_confirmation:
                bucket = canonical["bucket"]
                canonical_game = canonical["game"]
                for partition in ("all", str(canonical_game["end"] // DAY)):
                    pk = f"STATS#{canonical_game['mode']}#{partition}"
                    expiry = (int(partition) + 32) * DAY if partition != "all" else None
                    conditions, counters = stats.counter_writes(self.store, pk, canonical_game, bucket, expiry)
                    expected.update(conditions)
                    writes.extend(counters)
                game_doc["counters_written"] = True
                writes[0]["payload"] = game_doc
            personal_tournament = self._tournament_report_updates(report, reports, now)
            if personal_tournament:
                for pair, (expected_version, write) in personal_tournament.items():
                    expected[pair] = expected_version
                    writes.append(write)
            try:
                if self.metrics.transact(expected, writes, install, report["client"], now):
                    return 201, {"accepted": True, "confirmed": canonical is not None}
            except ValueError as error:
                if str(error) == "item_too_large":
                    raise ServiceError(413, "record_too_large") from None
                raise
        raise ServiceError(503, "storage_busy")

    @staticmethod
    def _same_report(left: dict[str, Any], right: dict[str, Any]) -> bool:
        return left.get("game") == right.get("game") and left.get("me") == right.get("me")

    @staticmethod
    def _has_counter(game_pk: str, game_doc: dict[str, Any], canonical: dict[str, Any]) -> bool:
        return bool(game_doc.get("counters_written"))

    def personal_stats(self, install_value: str, now: int, cursor: str | None = None) -> dict[str, Any]:
        install = normalize_install(install_value)
        cursor = page_cursor(cursor)
        rows, next_cursor = self.store.query_page(f"PERSONAL#{install}", limit=20, cursor=cursor, reverse=True)
        reports = [
            {**row["payload"]["report"], "received_at": row["payload"]["received_at"]}
            for row in rows if int(row.get("expires_at", 0)) > now
        ]
        summary = self.store.get(personal.partition(self.server_key, install), "SUMMARY")
        if rows and summary is None:
            raise ServiceError(503, "personal_stats_unavailable")
        history = []
        for report in reports:
            history.extend(stats.personal_view([report], now)["last_games"])
        return {**personal.view(summary["payload"] if summary else None),
                "last_games": history, "next_cursor": next_cursor}

    def global_stats(self, mode: str, period: str, now: int) -> dict[str, Any]:
        if mode not in ("", "faithful", "uber"):
            raise ServiceError(400, "invalid_mode", "mode")
        if period not in ("7d", "30d", "all"):
            raise ServiceError(400, "invalid_period", "period")
        row = self.store.get("CACHE", f"{mode}#{period}")
        if not row or int(row.get("payload", {}).get("expires_at", 0)) <= now:
            raise ServiceError(503, "stats_cache_unavailable")
        return row["payload"]["body"]

    def _ticket_row(self, ticket: str) -> tuple[dict[str, Any] | None, dict[str, Any]]:
        if not TICKET_PATTERN.fullmatch(ticket):
            raise ServiceError(404, "ticket_not_found")
        return self._get(f"TICKET#{ticket}", "STATE")

    def create_ticket(self, body: dict[str, Any], now: int) -> dict[str, Any]:
        install, nick = validate_install_body(body)
        client = client_metadata(required(body, "client"))
        mode = required(body, "mode")
        if mode not in ("faithful", "uber"):
            raise RequestError(400, "invalid_field", "mode")
        size = required(body, "size")
        if size not in ("1v1", "2v2"):
            raise RequestError(400, "invalid_field", "size")
        factions = required(body, "factions")
        if not isinstance(factions, list) or not 1 <= len(factions) <= 8:
            raise RequestError(400, "invalid_field", "factions")
        factions = [text(value, f"factions.{i}") for i, value in enumerate(factions)]
        if "random" in factions and factions != ["random"]:
            raise RequestError(400, "invalid_field", "factions")
        region = body.get("region_hint")
        if region is not None:
            region = text(region, "region_hint")
        preferences = {"install": install, "nick": nick, "mode": mode, "size": size,
                       "factions": factions, "region_hint": region, "client": client}
        pool_pk = f"POOL#{mode}#{size}"
        active_pk = f"ACTIVE#{install}"
        for _ in range(MAX_RETRIES):
            active_row, active = self._get(active_pk, "TICKET")
            if active.get("ticket"):
                existing_row, existing = self._ticket_row(active["ticket"])
                if (
                    existing_row
                    and existing.get("state") == "searching"
                    and int(existing.get("logical_expires_at", 0)) > now
                ):
                    old_preferences = {name: existing.get(name) for name in preferences}
                    if old_preferences != preferences:
                        raise ServiceError(409, "active_ticket_conflict")
                    return {"ticket": active["ticket"], "poll_after_s": 5}
            pool_row, pool = self._get(pool_pk, "ACTIVE")
            entries = list(pool.get("entries", []))
            live = []
            candidates = []
            for entry in entries:
                row, data = self._ticket_row(entry["ticket"])
                if row and data.get("state") == "searching" and int(data.get("logical_expires_at", 0)) > now:
                    live.append(entry)
                    candidates.append((entry, row, data))
            sequence = int(pool.get("sequence", 0)) + 1
            ticket = secrets.token_hex(16)
            ticket_data = {
                **preferences,
                "state": "searching",
                "created_at": now,
                "sequence": sequence,
                "logical_expires_at": now + 300,
            }
            new_entry = {"ticket": ticket, "created_at": now, "sequence": sequence}
            all_candidates = candidates + [(new_entry, None, ticket_data)]
            needed = 2 if size == "1v1" else 4
            matched = all_candidates[:needed] if len(all_candidates) >= needed else []
            if not matched and len(live) >= 128:
                raise ServiceError(429, "queue_full")
            updated_entries = live + ([] if matched else [new_entry])
            ticket_rows = []
            expected: dict[tuple[str, str], int | None] = {
                key(pool_pk, "ACTIVE"): pool_row["version"] if pool_row else None,
                key(active_pk, "TICKET"): active_row["version"] if active_row else None,
            }
            if active_row and active.get("ticket"):
                old_ticket_row, old_ticket = self._ticket_row(active["ticket"])
                if old_ticket_row:
                    expected[key(f"TICKET#{active['ticket']}", "STATE")] = old_ticket_row["version"]
            match = None
            if matched:
                team = [0, 1] if needed == 2 else [0, 1, 1, 0]
                match = {
                    "id": secrets.token_hex(16),
                    "room_code": secrets.token_hex(8).upper(),
                    "host_install": matched[0][2]["install"],
                    "players": [{"nick": item[2]["nick"], "team": team[i]} for i, item in enumerate(matched)],
                    "mode": mode,
                }
            matched_tickets = {entry["ticket"] for entry, _row, _data in matched}
            if matched:
                updated_entries = [entry for entry in live if entry["ticket"] not in matched_tickets]
            for index, (entry, row, data) in enumerate(matched):
                data = {**data, "state": "matched", "match": match, "terminal_at": now}
                expected[key(f"TICKET#{entry['ticket']}", "STATE")] = row["version"] if row else None
                ticket_rows.append({"pk": f"TICKET#{entry['ticket']}", "sk": "STATE", "payload": data,
                                    "expires_at": now + DAY})
                active_key = key(f"ACTIVE#{data['install']}", "TICKET")
                active_row_for_ticket = self.store.get(*active_key)
                expected[active_key] = active_row_for_ticket["version"] if active_row_for_ticket else None
                ticket_rows.append({"pk": active_key[0], "sk": active_key[1],
                                    "payload": {"ticket": entry["ticket"], "state": "matched"},
                                    "expires_at": now + DAY})
                if index == 0:
                    match_write = {"pk": f"QM_MATCH#{match['id']}", "sk": "DOC",
                                   "payload": {"id": match["id"], "match": match}, "expires_at": now + DAY}
                    match_row = self.store.get(match_write["pk"], "DOC")
                    expected[key(match_write["pk"], "DOC")] = match_row["version"] if match_row else None
                    ticket_rows.append(match_write)
            if not matched:
                expected[key(f"TICKET#{ticket}", "STATE")] = None
                ticket_rows.append({"pk": f"TICKET#{ticket}", "sk": "STATE", "payload": ticket_data,
                                    "expires_at": now + 300})
                expected[key(active_pk, "TICKET")] = active_row["version"] if active_row else None
                ticket_rows.append(
                    {"pk": active_pk, "sk": "TICKET", "payload": {"ticket": ticket, "state": "searching"},
                     "expires_at": now + 300}
                )
            pool_payload = {"entries": updated_entries, "sequence": sequence}
            pool_write = {"pk": pool_pk, "sk": "ACTIVE", "payload": pool_payload}
            try:
                if self.metrics.transact(expected, [pool_write, *ticket_rows], install, client, now):
                    return {"ticket": ticket, "poll_after_s": 5}
            except ValueError:
                raise ServiceError(503, "storage_busy") from None
        raise ServiceError(503, "storage_busy")

    def poll_ticket(self, ticket: str, now: int) -> dict[str, Any]:
        for _ in range(MAX_RETRIES):
            row, data = self._ticket_row(ticket)
            if not row:
                raise ServiceError(404, "ticket_not_found")
            state = data.get("state")
            if state != "searching" and int(row.get("expires_at", 0)) <= now:
                raise ServiceError(404, "ticket_not_found")
            if state == "searching" and int(data.get("logical_expires_at", 0)) <= now:
                data.update(state="expired", terminal_at=now)
                try:
                    if self.store.transact(
                        {key(row["pk"], row["sk"]): row["version"]},
                        [{"pk": row["pk"], "sk": row["sk"], "payload": data, "expires_at": now + DAY}],
                    ):
                        return {"state": "expired"}
                except ValueError:
                    pass
                continue
            if state == "searching":
                data["logical_expires_at"] = now + 300
                if self.store.transact(
                    {key(row["pk"], row["sk"]): row["version"]},
                    [{"pk": row["pk"], "sk": row["sk"], "payload": data, "expires_at": now + 300}],
                ):
                    return {"state": "searching"}
                continue
            if state == "matched":
                return {"state": "matched", "match": data["match"]}
            return {"state": state}
        raise ServiceError(503, "storage_busy")

    def cancel_ticket(self, ticket: str, now: int) -> dict[str, Any]:
        for _ in range(MAX_RETRIES):
            row, data = self._ticket_row(ticket)
            if not row:
                return {"state": "cancelled"}
            if data.get("state") != "searching" and int(row.get("expires_at", 0)) <= now:
                raise ServiceError(404, "ticket_not_found")
            if data.get("state") != "searching":
                return {"state": data.get("state")}
            data.update(state="cancelled", terminal_at=now)
            pool_pk = f"POOL#{data['mode']}#{data['size']}"
            pool_row, pool = self._get(pool_pk, "ACTIVE")
            entries = [entry for entry in pool.get("entries", []) if entry["ticket"] != ticket]
            expected = {
                key(row["pk"], row["sk"]): row["version"],
                key(pool_pk, "ACTIVE"): pool_row["version"] if pool_row else None,
            }
            writes = [
                {"pk": row["pk"], "sk": row["sk"], "payload": data, "expires_at": now + DAY},
                {"pk": pool_pk, "sk": "ACTIVE", "payload": {"entries": entries, "sequence": pool.get("sequence", 0)}},
            ]
            active_pk = f"ACTIVE#{data['install']}"
            active_row, active = self._get(active_pk, "TICKET")
            if active.get("ticket") == ticket and active_row:
                expected[key(active_pk, "TICKET")] = active_row["version"]
                writes.append({"pk": active_pk, "sk": "TICKET", "payload": {"ticket": ticket, "state": "cancelled"},
                               "expires_at": now + DAY})
            if self.store.transact(expected, writes):
                return {"state": "cancelled"}
        raise ServiceError(503, "storage_busy")

    def _tournament_report_updates(self, report: dict[str, Any], reports: dict[str, dict[str, Any]], now: int):
        from platform_service.tournaments import prepare_result_updates

        return prepare_result_updates(self.store, report, reports, now)

    def _authorize_admin(self, authorization: str) -> None:
        if not authorization.startswith("Bearer "):
            raise ServiceError(401, "unauthorized")
        supplied = authorization[len("Bearer "):]
        if not supplied or not supplied.isascii():
            raise ServiceError(401, "unauthorized")
        expected = self.admin_token_provider() if self.admin_token_provider else self.admin_token
        if not expected or not hmac.compare_digest(supplied, expected):
            raise ServiceError(401, "unauthorized")

    def dispatch(self, method: str, path: str, query: dict[str, Any], body: dict[str, Any] | None,
                 *, now: int, source_ip: str = "127.0.0.1", authorization: str = "",
                 admitted: bool = False) -> tuple[int, dict[str, Any]]:
        method = method.upper()
        body = body or {}
        try:
            parts = canonical_path(path)
        except RequestError as error:
            raise ServiceError(error.status, error.code, error.field) from None
        poll_or_cancel = len(parts) >= 4 and parts[:3] == ("v1", "qm", "tickets") and method in ("GET", "DELETE")
        mutating = method in ("POST", "PUT", "PATCH", "DELETE") or poll_or_cancel
        is_admin = len(parts) >= 2 and parts[:2] == ("v1", "admin")
        if is_admin:
            try:
                self._authorize_admin(authorization)
            except ServiceError:
                if not self.admission.source_and_global(source_ip, now):
                    raise ServiceError(429, "rate_limited") from None
                raise
            if mutating and not admitted and not self.admission.source_and_global(source_ip, now, admin=True):
                raise ServiceError(429, "rate_limited")
            if mutating and not self.admission.install("admin", now, admin=True):
                raise ServiceError(429, "rate_limited")
        elif mutating and not poll_or_cancel:
            if not admitted and not self.admission.source_and_global(source_ip, now):
                raise ServiceError(429, "rate_limited")
            install = normalize_install(required(body, "install"))
            if not self.admission.install(install, now):
                raise ServiceError(429, "rate_limited")
        elif mutating and not admitted and not self.admission.source_and_global(source_ip, now):
            raise ServiceError(429, "rate_limited")
        if method == "GET" and path == "/v1/health":
            return 200, {"status": "ok"}
        if method == "GET" and path == "/v1/admin/metrics":
            days = query.get("days", "30")
            if not isinstance(days, str) or not re.fullmatch(r"[1-9][0-9]?", days) or int(days) > 30:
                raise ServiceError(400, "invalid_field", "days")
            return 200, self.metrics.view(self.store, now, int(days))
        if method == "POST" and path == "/v1/results":
            status, payload = self.submit_result(body, now)
            return status, payload
        if method == "GET" and path == "/v1/stats/global":
            mode = query.get("mode", "")
            period = query.get("period", "all")
            if not isinstance(mode, str) or not isinstance(period, str):
                raise ServiceError(400, "invalid_query")
            return 200, self.global_stats(mode, period, now)
        if method == "GET" and path == "/v1/stats/me":
            install = query.get("install")
            if install is None:
                raise ServiceError(400, "missing_field", "install")
            return 200, self.personal_stats(install, now, query.get("cursor"))
        if method == "POST" and path == "/v1/qm/tickets":
            return 201, self.create_ticket(body, now)
        if len(parts) >= 4 and parts[:3] == ("v1", "qm", "tickets"):
            ticket = path.removeprefix("/v1/qm/tickets/")
            ticket_row, ticket_data = self._ticket_row(ticket)
            if not ticket_row:
                if method == "DELETE":
                    return 200, {"state": "cancelled"}
                raise ServiceError(404, "ticket_not_found")
            if poll_or_cancel and not self.admission.install(ticket_data["install"], now):
                raise ServiceError(429, "rate_limited")
            if method == "GET":
                return 200, self.poll_ticket(ticket, now)
            if method == "DELETE":
                return 200, self.cancel_ticket(ticket, now)
        if method == "POST" and len(parts) == 6 and parts[:2] == ("v1", "tournaments") and parts[-1] == "result":
            pieces = parts
            if len(pieces) != 6 or pieces[3] != "matches":
                raise ServiceError(404, "not_found")
            report = validate_result(body, now)
            if report["game"]["id"] != pieces[4]:
                raise ServiceError(400, "invalid_field", "game.id")
            if not report["game"]["id"].startswith(f"{pieces[2]}."):
                raise ServiceError(400, "invalid_field", "game.id")
            status, payload = self.submit_result(body, now)
            return status, payload
        from platform_service.tournaments import dispatch_tournament

        return dispatch_tournament(
            self, method, path, query, body, now=now, authorization=authorization,
            is_admin=is_admin,
        )


def request_source_ip(event: dict[str, Any]) -> str:
    context = event.get("requestContext")
    http = context.get("http") if isinstance(context, dict) else None
    source = http.get("sourceIp") if isinstance(http, dict) else None
    return source if isinstance(source, str) else "unknown"


def api_request(event: dict[str, Any]) -> tuple[str, str, dict[str, Any], dict[str, Any]]:
    context = event.get("requestContext")
    http = context.get("http") if isinstance(context, dict) else None
    if not isinstance(http, dict):
        raise RequestError(400, "invalid_request")
    method, path = http.get("method"), http.get("path")
    if not isinstance(method, str) or not isinstance(path, str):
        raise RequestError(400, "invalid_request")
    query = event.get("queryStringParameters") or {}
    headers = event.get("headers") or {}
    if not isinstance(query, dict) or not isinstance(headers, dict):
        raise RequestError(400, "invalid_request")
    normalized_headers = {}
    for header, value in headers.items():
        if not isinstance(header, str) or not isinstance(value, str) or header.lower() in normalized_headers:
            raise RequestError(400, "invalid_headers")
        normalized_headers[header.lower()] = value
    return method, path, query, normalized_headers


def safe_dispatch(service: PlatformService, event: dict[str, Any], *, now: int | None = None) -> dict[str, Any]:
    from platform_service.protocol import canonical_path, error_response, parse_event_body, response

    request_time = int(time.time()) if now is None else int(now)
    try:
        try:
            method, path, query, headers = api_request(event)
            parts = canonical_path(path)
            authorization = headers.get("authorization", "")
            is_admin = len(parts) >= 2 and parts[:2] == ("v1", "admin")
            if is_admin:
                service._authorize_admin(authorization)
        except (RequestError, ServiceError):
            if not service.admission.source_and_global(request_source_ip(event), request_time):
                raise ServiceError(429, "rate_limited") from None
            raise
        mutating = method in ("POST", "PUT", "PATCH", "DELETE") or (
            method == "GET" and len(parts) >= 4 and parts[:3] == ("v1", "qm", "tickets")
        )
        if mutating and not service.admission.source_and_global(
            request_source_ip(event), request_time, admin=is_admin,
        ):
            raise ServiceError(429, "rate_limited")
        body_needed = method in ("POST", "PUT", "PATCH") or (
            method == "DELETE" and not path.startswith("/v1/qm/tickets/")
        )
        body = parse_event_body(event) if body_needed else {}
        status, payload = service.dispatch(
            method, path, query, body, now=request_time,
            source_ip=request_source_ip(event), authorization=authorization,
            admitted=mutating,
        )
        return response(status, payload)
    except RequestError as error:
        return error_response(error)
    except ServiceError as error:
        return error_response(RequestError(error.status, error.code, error.field))
    except Exception:
        return error_response(RequestError(503, "unavailable"))
