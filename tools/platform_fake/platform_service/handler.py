from __future__ import annotations

import os
import threading
import time
from copy import deepcopy
from functools import lru_cache

from platform_service.service import PlatformService, safe_dispatch
from platform_service.storage import DynamoStore

REGION = "us-east-2"
ADMIN_TOKEN_TTL_SECONDS = 60


def _monotonic() -> float:
    return time.monotonic()


def _secret(arn: str) -> str:
    if not arn:
        raise RuntimeError("missing_secret_arn")
    import boto3

    value = boto3.client("secretsmanager", region_name=REGION).get_secret_value(SecretId=arn).get("SecretString")
    if not isinstance(value, str) or len(value) != 64 or not value.isascii():
        raise RuntimeError("invalid_secret")
    return value


class _AdminTokenCache:
    def __init__(self):
        self._lock = threading.Lock()
        self._token: str | None = None
        self._refresh_after = 0.0

    def get(self, arn: str) -> str:
        with self._lock:
            if self._token is not None and _monotonic() < self._refresh_after:
                return self._token
            self._token = None
            self._refresh_after = 0.0
            token = _secret(arn)
            self._token = token
            self._refresh_after = _monotonic() + ADMIN_TOKEN_TTL_SECONDS
            return token

    def clear(self) -> None:
        with self._lock:
            self._token = None
            self._refresh_after = 0.0


_admin_token_cache = _AdminTokenCache()


@lru_cache(maxsize=1)
def _real_service() -> PlatformService:
    table_name = os.environ.get("PLATFORM_TABLE", "")
    limiter_name = os.environ.get("LIMITER_TABLE", "")
    if not table_name or not limiter_name:
        raise RuntimeError("missing_resource_configuration")
    return PlatformService(
        DynamoStore(table_name, region=REGION),
        DynamoStore(limiter_name, region=REGION),
        server_key=_secret(os.environ.get("SERVER_HASH_SECRET_ARN", "")).encode("ascii"),
        admin_token_provider=lambda: _admin_token_cache.get(os.environ.get("ADMIN_TOKEN_SECRET_ARN", "")),
    )


class _ScheduledService:
    def __init__(self, store):
        self.store = store

    def _get(self, pk: str, sk: str):
        row = self.store.get(pk, sk)
        return row, deepcopy(row["payload"]) if row else {}


@lru_cache(maxsize=1)
def _scheduled_service() -> _ScheduledService:
    table_name = os.environ.get("PLATFORM_TABLE", "")
    if not table_name:
        raise RuntimeError("missing_resource_configuration")
    return _ScheduledService(DynamoStore(table_name, region=REGION))


def lambda_handler(event, context, *, now: int | None = None, service: PlatformService | None = None):
    try:
        return safe_dispatch(service or _real_service(), event, now=int(time.time()) if now is None else int(now))
    except Exception:
        from platform_service.protocol import RequestError, error_response

        return error_response(RequestError(503, "unavailable"))


def scheduled_handler(event, context, *, now: int | None = None, service: PlatformService | None = None):
    from platform_service.aggregation import aggregate
    from platform_service.protocol import RequestError
    from platform_service.tournaments import _advance_due

    try:
        action = event.get("action") if isinstance(event, dict) else None
        if action not in ("aggregate", "tournament_sweep"):
            raise RequestError(400, "invalid_schedule_action")
        current = int(time.time()) if now is None else int(now)
        active = service or _scheduled_service()
        if action == "aggregate":
            aggregate(active.store, current)
            return {"action": action}
        return {"action": action, "changed": _advance_due(active, current)}
    except Exception:
        raise RuntimeError("platform_schedule_failed") from None
