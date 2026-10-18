#!/usr/bin/env python3
"""OpenBFME platform API for the game's tests: the backend's OWN service (platform_service/, copied from the private repository, see SOURCE.md) behind
its own HTTP fake (platform_fake.py), plus a test surface under /__test/ that the game never calls:

  GET  /__test/log               every /v1 request received: [{method, path, body, status, content_type}] (path with its query)
  POST /__test/advance {seconds} the service's clock moves on (ticket expiry, tournament starts, the 15 minute check-in window); the scheduled work
                                 (tournament sweep, the statistics cache) runs at once for the new time
  POST /__test/sweep             the scheduled work for the current time
  POST /__test/fail {prefix, status, count, delay_ms, body_contains}   the next `count` /v1 requests whose path starts with `prefix` (and, when given,
                                 whose body contains `body_contains`) answer `status` (or wait first)
  POST /__test/down {down}       every /v1 request is dropped without an answer
  POST /__test/reset             the log and the injected failures are cleared (the service's data stays)

Usage: python3 -S tools/platform_fake/fake_server.py --admin-token T [--port 0]; the first line printed is {"event":"listening","host":..,"port":..}.
The service's clock starts at the real time. Everything is in memory; nothing is exposed beyond --host (default 127.0.0.1).
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

import platform_fake  # noqa: E402
from platform_service.service import PlatformService, safe_dispatch  # noqa: E402
from platform_service.storage import MemoryStore  # noqa: E402


class State:
    def __init__(self, service: PlatformService):
        self.service = service
        self.lock = threading.Lock()
        self.offset = 0
        self.log: list[dict] = []
        self.failures: list[dict] = []
        self.down = False

    def now(self) -> int:
        return int(time.time()) + self.offset


def _json(status: int, body: dict) -> dict:
    payload = json.dumps(body, separators=(",", ":"))
    return {"statusCode": status, "headers": {"content-type": "application/json"}, "body": payload}


def make_app(state: State):
    def app(event: dict) -> dict:
        http = event["requestContext"]["http"]
        path, method = http["path"], http["method"]
        raw = event.get("body", "")
        if path.startswith("/__test/"):
            import base64

            text = base64.b64decode(raw).decode("utf-8") if raw and event.get("isBase64Encoded") else raw
            body = json.loads(text) if text else {}
            with state.lock:
                if method == "GET" and path == "/__test/log":
                    return _json(200, {"log": state.log})
                if method == "POST" and path == "/__test/advance":
                    state.offset += int(body["seconds"])
                    platform_fake.run_scheduled_work(state.service, state.now())
                    return _json(200, {"now": state.now()})
                if method == "POST" and path == "/__test/sweep":
                    return _json(200, platform_fake.run_scheduled_work(state.service, state.now()))
                if method == "POST" and path == "/__test/fail":
                    state.failures.append({"count": 1, "status": 503, "delay_ms": 0, "prefix": "/v1/", "body_contains": "", **body})
                    return _json(200, {})
                if method == "POST" and path == "/__test/down":
                    state.down = bool(body.get("down"))
                    return _json(200, {})
                if method == "POST" and path == "/__test/reset":
                    state.log.clear()
                    state.failures.clear()
                    state.down = False
                    return _json(200, {})
            return _json(404, {"error": {"code": "not_found"}})
        query = event.get("queryStringParameters") or {}
        shown = path + ("?" + "&".join(f"{k}={v}" for k, v in query.items()) if query else "")
        entry = {"method": method, "path": shown, "body": "", "status": 0,
                 "content_type": event["headers"].get("content-type", "")}
        if raw:
            import base64

            entry["body"] = base64.b64decode(raw).decode("utf-8", "replace") if event.get("isBase64Encoded") else raw
        with state.lock:
            state.log.append(entry)
            if state.down:
                entry["status"] = -1
                raise ConnectionAbortedError("the fake is down")
            injected = next((f for f in state.failures if f["count"] > 0 and path.startswith(f["prefix"]) and f["body_contains"] in entry["body"]), None)
            if injected:
                injected["count"] -= 1
        if injected:
            if injected["delay_ms"]:
                time.sleep(injected["delay_ms"] / 1000.0)
            if injected["status"]:
                entry["status"] = injected["status"]
                return _json(injected["status"], {"v": 1, "error": {"code": "injected"}})
        result = safe_dispatch(state.service, event, now=state.now())
        entry["status"] = result["statusCode"]
        return result

    return app


class QuietHandler(platform_fake.Handler):
    def handle(self):
        try:
            super().handle()
        except (ConnectionError, BrokenPipeError):
            pass


class QuietServer(platform_fake.PlatformHTTPServer):
    def handle_error(self, request, client_address):  # a dropped answer (the fake is "down") is not an error to print
        return


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--admin-token", required=True)
    args = parser.parse_args()
    service = PlatformService(MemoryStore(), MemoryStore(), server_key=b"openbfme-test-fake-key-not-for-deployment",
                              admin_token=args.admin_token)
    state = State(service)
    platform_fake.run_scheduled_work(service, state.now())
    stop, thread = platform_fake.start_background_scheduler(service, interval_s=0.5, clock=state.now)
    server = QuietServer((args.host, args.port), QuietHandler, app=make_app(state))
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
