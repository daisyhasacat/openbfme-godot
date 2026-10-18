"""Run two real openbfme_peer processes through the LANAPI lobby."""

from __future__ import annotations

import argparse
import ipaddress
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path


MAP = "maps/map mp evendim/map mp evendim.map"


class CapturedProcess:
    def __init__(self, command: list[str], label: str, cwd: Path):
        self.label = label
        self.command = command
        self.process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.lines: list[str] = []
        self.stdout_lines: list[str] = []
        self.stderr_lines: list[str] = []
        self._threads = []
        for stream, destination in ((self.process.stdout, self.stdout_lines), (self.process.stderr, self.stderr_lines)):
            thread = threading.Thread(target=self._read, args=(stream, destination), daemon=True)
            thread.start()
            self._threads.append(thread)

    def _read(self, stream, destination: list[str]) -> None:
        assert stream is not None
        for line in stream:
            self.lines.append(line)
            destination.append(line)

    def wait(self, deadline: float) -> int:
        remaining = max(0, deadline - time.monotonic())
        try:
            return self.process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                return self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                return self.process.wait(timeout=3)

    def finish_readers(self) -> None:
        for thread in self._threads:
            thread.join()

    def text(self) -> str:
        return "".join(self.lines)


def write_process_log(path: Path, process: CapturedProcess, exit_code: int) -> None:
    path.write_text(
        "command: " + subprocess.list2cmdline(process.command) + "\n"
        + f"exit: {exit_code}\n\n"
        + process.text(),
        encoding="utf-8",
    )
    path.with_name(path.stem + ".stdout.log").write_text("".join(process.stdout_lines), encoding="utf-8")
    path.with_name(path.stem + ".stderr.log").write_text("".join(process.stderr_lines), encoding="utf-8")


def make_command(
    peer: Path,
    rotwk: Path,
    bfme2: Path,
    output: Path,
    role: str,
    scenario: str,
    address: str,
    port_base: int,
) -> list[str]:
    command = [
        str(peer),
        "--rotwk",
        str(rotwk),
        "--bfme2",
        str(bfme2),
        "--map",
        MAP,
        "--seed",
        "12345",
        "--frames",
        "25",
        "--crc-interval",
        "5",
        "--fixed-run-ahead",
        "--lan-pump-ms",
        "1000",
        "--lan-port-base",
        str(port_base),
        "--lobby-timeout",
        "60",
        "--hashes",
        str(output / f"{role}.hashes"),
        "--report",
        str(output / f"{role}.report"),
    ]
    if role == "host":
        command += ["--lan-host", "--name", "LANHost", "--slot", "human,FactionMen,0,0", "--slot", "human,FactionMordor,1,1"]
    else:
        command += ["--lan-join", "--name", "LANGuest", "--lan-faction", "FactionMordor"]
    if scenario == "explicit":
        command += ["--lan-targets", f"{address},127.0.0.1"]
    elif scenario == "loopback":
        command += ["--lan-targets", "127.0.0.1"]
    elif scenario != "broadcast":
        raise ValueError(f"unknown scenario {scenario}")
    return command


def run_scenario(args: argparse.Namespace, scenario: str, index: int) -> bool:
    output = args.output / (("before-" if args.before else "after-") + f"{index}-{scenario}")
    output.mkdir(parents=True, exist_ok=False)
    base = args.port_base + index * 8
    host_command = make_command(args.peer, args.rotwk, args.bfme2, output, "host", scenario, args.address, base)
    guest_command = make_command(args.peer, args.rotwk, args.bfme2, output, "guest", scenario, args.address, base)
    workdir = Path(__file__).resolve().parents[2]
    host = CapturedProcess(host_command, "host", workdir)
    ready = False
    ready_deadline = time.monotonic() + args.deadline
    while time.monotonic() < ready_deadline:
        if any("LAN_READY role=host" in line for line in host.lines):
            ready = True
            break
        if host.process.poll() is not None:
            break
        time.sleep(0.02)
    guest = None
    if ready:
        guest = CapturedProcess(guest_command, "guest", workdir)
        end_deadline = time.monotonic() + args.deadline
        guest_exit = guest.wait(end_deadline)
        host_exit = host.wait(end_deadline)
    else:
        guest_exit = 124
        host_exit = host.wait(ready_deadline)
    host.finish_readers()
    if guest is not None:
        guest.finish_readers()
    write_process_log(output / "host.log", host, host_exit)
    if guest is not None:
        write_process_log(output / "guest.log", guest, guest_exit)
    else:
        (output / "guest.log").write_text("guest was not launched: host was not ready\n", encoding="utf-8")

    host_text = host.text()
    guest_text = guest.text() if guest is not None else ""
    failures: list[str] = []
    if host_exit != 0 or guest_exit != 0:
        failures.append(f"peer exits host={host_exit} guest={guest_exit}")
    if "LAN_START role=host" not in host_text or "LAN_START role=guest" not in guest_text:
        failures.append("both peers did not report LAN_START")
    join_results = [int(value) for value in re.findall(r"LAN_GAME_JOIN result=(-?\d+)", host_text + guest_text)]
    if not join_results:
        failures.append("no GameJoin result was recorded")
    if any(value != 0 for value in join_results):
        failures.append(f"LAN GameJoin results were {join_results!r}, expected only 0")
    if re.search(r"no LAN game start|no LAN game start in time", host_text + guest_text, re.I):
        failures.append("LAN join timed out before game start")
    counts = [int(value) for value in re.findall(r"LAN_DISCOVERY count=(\d+)", guest_text)]
    if not counts or any(value != 1 for value in counts):
        failures.append(f"guest logical host counts were {counts!r}, expected exactly one")
    host_hashes = (output / "host.hashes").read_text(encoding="utf-8").splitlines() if (output / "host.hashes").exists() else []
    guest_hashes = (output / "guest.hashes").read_text(encoding="utf-8").splitlines() if (output / "guest.hashes").exists() else []
    hashes_match = bool(host_hashes) and bool(guest_hashes) and host_hashes == guest_hashes
    if not hashes_match:
        failures.append("hash lines differ or are missing")
    host_frame25 = [line for line in host_hashes if line.startswith("25 ")]
    guest_frame25 = [line for line in guest_hashes if line.startswith("25 ")]
    if not host_frame25 or not guest_frame25:
        failures.append("neither peer produced a frame-25 hash")
    summary = {
        "scenario": scenario,
        "before": args.before,
        "host_exit": host_exit,
        "guest_exit": guest_exit,
        "guest_discovery_counts": counts,
        "game_join_events": re.findall(r"LAN_GAME_JOIN[^\r\n]*", host_text + guest_text),
        "player_join_events": re.findall(r"LAN_PLAYER_JOIN[^\r\n]*", host_text + guest_text),
        "host_start_lines": re.findall(r"LAN_START[^\r\n]*", host_text),
        "guest_start_lines": re.findall(r"LAN_START[^\r\n]*", guest_text),
        "host_frame25_hashes": host_frame25,
        "guest_frame25_hashes": guest_frame25,
        "hashes_match": hashes_match,
        "failures": failures,
        "output": str(output),
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    label = "before" if args.before else "after"
    if failures:
        print(f"FAIL {label} {scenario}: " + "; ".join(failures), file=sys.stderr)
        return False
    print(f"PASS {label} {scenario}: host={host_exit} guest={guest_exit} evidence={output}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--peer", type=Path, required=True)
    parser.add_argument("--rotwk", type=Path, required=True)
    parser.add_argument("--bfme2", type=Path, required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", choices=("explicit", "broadcast", "loopback"), action="append")
    parser.add_argument("--port-base", type=int, default=42086)
    parser.add_argument("--deadline", type=float, default=120)
    parser.add_argument("--before", action="store_true", help="label baseline evidence as before")
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.address)
    except ipaddress.AddressValueError:
        parser.error("--address must be an IPv4 address")
    args.peer = args.peer.expanduser().resolve()
    args.rotwk = args.rotwk.expanduser().resolve()
    args.bfme2 = args.bfme2.expanduser().resolve()
    args.output = args.output.expanduser().resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.scenario:
        args.scenario = ["explicit"]
    if not args.peer.is_file():
        parser.error(f"peer executable does not exist: {args.peer}")
    if not args.rotwk.is_dir() or not args.bfme2.is_dir():
        parser.error("both retail install directories must exist")
    success = True
    for index, scenario in enumerate(args.scenario):
        success = run_scenario(args, scenario, index) and success
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
