#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from platform_service.protocol import MAX_BODY, RequestError, error_response  # noqa: E402
from platform_service.service import PlatformService  # noqa: E402
from platform_service.storage import MemoryStore  # noqa: E402


class PlatformHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, handler, *, app):
        super().__init__(address, handler)
        self.app = app


class Handler(BaseHTTPRequestHandler):
    server: PlatformHTTPServer
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        return

    def _reply(self, method: str) -> None:
        parsed = urlsplit(self.path)
        query_values = parse_qs(parsed.query, keep_blank_values=True)
        query = {key: values[-1] for key, values in query_values.items()}
        raw_length = self.headers.get("Content-Length", "0")
        if not raw_length.isascii() or not raw_length.isdecimal() or len(raw_length) > 8:
            result = error_response(RequestError(400, "invalid_content_length"))
        elif int(raw_length) > MAX_BODY:
            result = error_response(RequestError(413, "body_too_large"))
        else:
            raw = self.rfile.read(int(raw_length)) if int(raw_length) else b""
            if len(raw) != int(raw_length):
                result = error_response(RequestError(400, "invalid_content_length"))
            else:
                event = {
                    "requestContext": {
                        "http": {"method": method, "path": parsed.path, "sourceIp": self.client_address[0]},
                    },
                    "queryStringParameters": query,
                    "headers": {key.lower(): value for key, value in self.headers.items()},
                    "body": base64.b64encode(raw).decode("ascii") if raw else "",
                    "isBase64Encoded": bool(raw),
                }
                result = self.server.app(event)
        wire = result["body"].encode("utf-8")
        self.send_response(result["statusCode"])
        for name, value in result["headers"].items():
            self.send_header(name, value)
        self.send_header("content-length", str(len(wire)))
        self.send_header("connection", "close")
        self.end_headers()
        if method != "HEAD":
            self.wfile.write(wire)
        self.close_connection = True

    def do_GET(self):
        self._reply("GET")

    def do_POST(self):
        self._reply("POST")

    def do_DELETE(self):
        self._reply("DELETE")


def run_scheduled_work(app: PlatformService, now: int) -> dict[str, int | bool]:
    """Run both scheduled jobs once with an explicit clock value for tests."""
    from platform_service.aggregation import aggregate
    from platform_service.tournaments import _advance_due

    aggregate(app.store, now)
    return {"aggregated": True, "tournament_changes": _advance_due(app, now)}


def start_background_scheduler(
    app: PlatformService,
    *,
    interval_s: float = 1.0,
    clock=time.time,
) -> tuple[threading.Event, threading.Thread]:
    stop = threading.Event()
    initial_minute = int(clock()) // 60
    initial_hour = int(clock()) // 3600

    def worker() -> None:
        last_minute = initial_minute
        last_hour = initial_hour
        while not stop.wait(interval_s):
            current = int(clock())
            minute = current // 60
            hour = current // 3600
            try:
                if minute != last_minute:
                    from platform_service.tournaments import _advance_due

                    _advance_due(app, current)
                    last_minute = minute
                if hour != last_hour:
                    from platform_service.aggregation import aggregate

                    aggregate(app.store, current)
                    last_hour = hour
            except Exception as error:
                print(
                    f"platform fake scheduler failed: {type(error).__name__}: {error}",
                    file=sys.stderr,
                    flush=True,
                )
                return

    thread = threading.Thread(target=worker, name="platform-fake-scheduler", daemon=True)
    thread.start()
    return stop, thread


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the dependency-free local platform API fake.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--admin-token", default=None, help="explicit local test token only")
    args = parser.parse_args()
    if not 0 <= args.port <= 65_535:
        parser.error("--port must be in 0..65535")
    token = args.admin_token if args.admin_token is not None else os.environ.get("PLATFORM_FAKE_ADMIN_TOKEN", "")
    if not token:
        parser.error("--admin-token or PLATFORM_FAKE_ADMIN_TOKEN is required")
    app = PlatformService(
        MemoryStore(), MemoryStore(),
        server_key=b"local-fake-pseudonymization-key-not-for-deployment",
        admin_token=token,
    )
    run_scheduled_work(app, int(time.time()))
    stop, thread = start_background_scheduler(app)

    def dispatch(event):
        from platform_service.service import safe_dispatch

        return safe_dispatch(app, event)

    server = PlatformHTTPServer((args.host, args.port), Handler, app=dispatch)
    host, port = server.server_address[:2]
    print(json.dumps({"event": "listening", "host": host, "port": port}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        stop.set()
        thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
