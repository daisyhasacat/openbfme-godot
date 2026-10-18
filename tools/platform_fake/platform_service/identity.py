"""Shared install identity boundary; no account, fingerprint, or ban semantics."""
from __future__ import annotations

import hashlib
import hmac
import re
import uuid

_INSTALL = re.compile(
    r"^(?:[0-9a-fA-F]{32}|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$"
)
PLATFORMS = ("windows", "linux", "macos")


class IdentityError(ValueError):
    def __init__(self, code: str, field: str):
        super().__init__(code)
        self.code, self.field = code, field


def normalize_install(value: object) -> str:
    if not isinstance(value, str) or not _INSTALL.fullmatch(value):
        raise IdentityError("invalid_install", "install")
    return value.replace("-", "").lower()


def canonical_crash_install(value: object) -> str:
    parsed = uuid.UUID(normalize_install(value))
    if str(parsed) != value or parsed.version != 4:
        raise IdentityError("invalid_install", "install")
    return str(parsed)


def daily_install_hash(server_key: bytes, day: int, install: str) -> str:
    canonical = normalize_install(install)
    return hmac.new(server_key, f"metrics:{day}:{canonical}".encode(), hashlib.sha256).hexdigest()


def aggregate_install_hash(server_key: bytes, install: str) -> str:
    canonical = normalize_install(install)
    return hmac.new(server_key, f"personal:{canonical}".encode(), hashlib.sha256).hexdigest()


def client_metadata(value: object) -> dict:
    if not isinstance(value, dict):
        raise IdentityError("invalid_field", "client")
    for name in ("platform", "version"):
        if name not in value:
            raise IdentityError("missing_field", f"client.{name}")
    if value["platform"] not in PLATFORMS:
        raise IdentityError("invalid_field", "client.platform")
    version = value["version"]
    if not isinstance(version, str) or not 1 <= len(version) <= 64:
        raise IdentityError("invalid_field", "client.version")
    return {"platform": value["platform"], "version": version}
