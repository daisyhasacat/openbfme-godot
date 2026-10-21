#!/usr/bin/env python3
"""Control a development OpenBFME build through its local sandbox files."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
import uuid


class Transport:
    def __init__(self, directory=None, device=None, bundle="org.openbfme.game"):
        self.directory = Path(directory).expanduser() if directory else None
        self.device = device
        self.bundle = bundle

    def transfer(self, direction, source, destination):
        subprocess.run(["xcrun", "devicectl", "device", "copy", direction,
                        "--device", self.device, "--domain-type", "appDataContainer",
                        "--domain-identifier", self.bundle, "--source", str(source),
                        "--destination", str(destination), "--timeout", "15"],
                       check=True, stdout=subprocess.DEVNULL)

    def write(self, request):
        data = json.dumps(request)
        if self.device:
            with tempfile.TemporaryDirectory(prefix="openbfme-control-") as tmp:
                path = Path(tmp) / "request.json"
                path.write_text(data)
                self.transfer("to", path, "Documents/local-control/request.json")
        else:
            # Atomic replacement prevents the app from reading partial JSON.
            temp = self.directory / "incoming.json"
            temp.write_text(data)
            temp.replace(self.directory / "request.json")

    def read(self, name):
        try:
            if self.device:
                with tempfile.TemporaryDirectory(prefix="openbfme-control-") as tmp:
                    path = Path(tmp) / name
                    self.transfer("from", "Documents/local-control/" + name, path)
                    return json.loads(path.read_text())
            return json.loads((self.directory / name).read_text())
        except (FileNotFoundError, json.JSONDecodeError, subprocess.CalledProcessError):
            return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--device", help="Connected iPhone/iPad UDID or name")
    target.add_argument("--directory", help="Desktop app's local-control directory")
    parser.add_argument("--bundle", default="org.openbfme.game")
    parser.add_argument("--timeout", type=float, default=30)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    bench = sub.add_parser("benchmark_start")
    bench.add_argument("--seconds", type=float, default=10)
    bench.add_argument("--shadow-profile", choices=["auto", "balanced", "high"])
    bench.add_argument("--render-scale", type=float)
    bench.add_argument("--capped", action="store_true")
    bench.add_argument("--wait", action="store_true", help="Wait for completion; allow up to --timeout seconds")
    sub.add_parser("benchmark_cancel")
    settings = sub.add_parser("settings")
    settings.add_argument("--tab", default="Graphics")
    sub.add_parser("screenshot")
    menu = sub.add_parser("menu_input")
    menu.add_argument("--action", required=True, choices=["up", "down", "left", "right", "accept", "back"])
    camera = sub.add_parser("camera")
    for field in ("x", "y", "height"):
        camera.add_argument("--" + field, type=float, required=True)
    args = parser.parse_args()
    transport = Transport(args.directory, args.device, args.bundle)
    request = {"version": 1, "id": uuid.uuid4().hex, "command": args.command}
    for field in ("seconds", "shadow_profile", "render_scale", "tab", "action", "x", "y", "height"):
        value = getattr(args, field, None)
        if value is not None:
            request[field] = value
    if args.command == "benchmark_start":
        request["uncapped"] = not args.capped
    transport.write(request)
    deadline = time.monotonic() + args.timeout
    response = None
    while time.monotonic() < deadline:
        response = transport.read("response.json")
        if response and response.get("id") == request["id"]:
            break
        time.sleep(0.5)
    else:
        raise SystemExit("No acknowledgement; app must be awake, in foreground, and a development build.")
    if response.get("status") in ("error", "duplicate"):
        print(json.dumps(response, indent=2))
        raise SystemExit(1)
    if args.command == "benchmark_start" and args.wait:
        while time.monotonic() < deadline:
            job = transport.read("job.json")
            if job and job.get("id") == request["id"] and job.get("status") != "running":
                print(json.dumps(job, indent=2))
                raise SystemExit(0 if job.get("status") == "complete" else 1)
            time.sleep(2)
        raise SystemExit("Benchmark still running; use status or benchmark_cancel.")
    print(json.dumps(response, indent=2))


if __name__ == "__main__":
    main()
