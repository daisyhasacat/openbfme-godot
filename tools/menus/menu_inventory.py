#!/usr/bin/env python3
"""Lane MENUWORK-1: the menu inventory table (every clickable control of every menu screen: what retail does, what OpenBFME does, the verdict).

The evidence comes from the game itself: `godot --path godot -- --menu-inventory=DIR` (godot/scripts/menu_inventory.gd) presses every control of every
screen the main menu leads to (the online pages past the login too, on a local lobby), and the quit menu and score screen of a skirmish, with real mouse
events, and writes DIR/inventory.json: per press the shell's stack before / after, the buttons that came and went, a message box's text, the menu's
requests, the commands the movie ran (an "unhandled:" one reached no engine handler), the pressed gadget's own state before / after, the gadgets whose
value changed and the top screen's engine state before / after. This tool turns that into the table:

    python3 tools/menus/menu_inventory.py run --out DIR [--windowed] [--res 2560x1440]     a local lobby, the game's walk, then DIR/menu_inventory.md
    python3 tools/menus/menu_inventory.py table DIR/inventory.json [--out FILE.md]         the table from an earlier run

Retail's behaviour per control is tools/menus/retail_controls.json: rows matched by a regular expression on the control's path, each with RotWK's text,
its source (an address or the decompile file) and the control's intended effect as checks the evidence must satisfy (alternatives joined by " | "):

    screen:X        the stack's top is X after the press (and the stack changed)     leave          the stack got shorter (the screen closed)
    page[:A,B]      buttons came (one whose path holds A or B)                       hover          the pointer alone opened buttons (a nav)
    changed         buttons came or went (a page that only hides its controls)
    box[:text]      a message box came (its text holds `text`)                       unavailable    a "not available" box, or a greyed-out entry that did nothing
    request:A       the menu asked the host for A                                    command:C      the movie ran C and an engine handler took it
    value           the pressed gadget's value changed (text, check, slider, choice) open           the pressed combo's drop-down list opened
    select          the pressed list's selected row changed (an empty list: nothing to select)
    gadget:N        the gadget N's state changed (another control's list order, values)
    probe:K         the engine state K changed (AptMenuPlayer.screen_state, the lobby, the online client, Create-a-Hero, the quit menu, the options)
    nothing         retail does nothing either: no screen, box, request, handled command, value or engine state changed

A picture change alone proves nothing. The table fails (exit 1) when the walk did not finish, reported an error, misses a required place
(retail_controls.json "required_places"), or any control has no retail row, no intended effect, or evidence that does not satisfy it.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROJECT = REPO / "godot"
RETAIL = Path(__file__).resolve().parent / "retail_controls.json"
LOBBY_SERVER = REPO / "services" / "online" / "lobby" / "src" / "server.mjs"

VERDICTS = ("works", "unavailable", "broken")


def load_retail_doc(path: Path = RETAIL) -> dict:
    doc = json.loads(path.read_text(encoding="utf-8"))
    for r in doc["controls"]:
        r["_re"] = re.compile(r["match"])
        r["_screen"] = re.compile(r["screen"]) if r.get("screen") else None
    return doc


def load_retail(path: Path = RETAIL) -> list[dict]:
    return load_retail_doc(path)["controls"]


def retail_row(rec: dict, retail: list[dict]) -> dict | None:
    for r in retail:
        if r["_re"].search(rec["control"]) and (r["_screen"] is None or r["_screen"].search(rec.get("screen", ""))):
            return r
    return None


def unavailable_text(text: str) -> bool:
    t = text.lower()
    return "not available" in t or "isn't there yet" in t or "not there yet" in t


def _handled(rec: dict) -> list[str]:
    return [c for c in rec.get("commands", []) if not c.startswith("unhandled:") and not c.startswith("PlaySound")]


def _own_changed(rec: dict, fields: tuple[str, ...]) -> bool:
    a, b = rec.get("own_before") or {}, rec.get("own_after") or {}
    return any(a.get(f) != b.get(f) for f in fields)


def check(rec: dict, spec: str) -> bool:
    """Whether the press's evidence satisfies one check (see the module's doc)."""
    kind, _, arg = spec.strip().partition(":")
    before, after = rec.get("stack_before") or [], rec.get("stack_after") or []
    new = rec.get("new_buttons", [])
    if kind == "screen":
        return bool(after) and after != before and arg in after[-1]
    if kind == "leave":
        return len(after) < len(before)
    if kind == "page":
        return bool(new) and (not arg or any(any(w in p for w in arg.split(",")) for p in new))
    if kind == "changed":
        return bool(new) or bool(rec.get("removed_buttons"))
    if kind == "hover":
        return rec.get("hover", "").startswith("opens") and not rec.get("hover", "").startswith("opens 0")
    if kind == "box":
        return bool(rec.get("box")) and arg.lower() in rec.get("box", "").lower()
    if kind == "unavailable":
        if rec.get("box") and unavailable_text(rec["box"]):
            return True
        return rec["control"].endswith("Disabled") and check(rec, "nothing")
    if kind == "request":
        return arg in rec.get("requests", [])
    if kind == "command":
        return any(c.split(" (")[0] == arg for c in _handled(rec))
    if kind == "value":
        return _own_changed(rec, ("text", "checked", "value", "selected"))
    if kind == "open":
        return (rec.get("own_after") or {}).get("open") is True and (rec.get("own_before") or {}).get("open") is not True
    if kind == "select":
        if (rec.get("own_before") or {}).get("entries") == 0:
            return True  # an empty list: no row to select
        return _own_changed(rec, ("selected",))
    if kind == "gadget":
        return any(g.split(":")[0].startswith(arg) for g in rec.get("gadgets", []))
    if kind == "probe":
        return any(p.split(":")[0] == arg for p in rec.get("probe_changes", []))
    if kind == "nothing":
        return (after == before and not rec.get("box") and not rec.get("requests") and not rec.get("gadgets") and not rec.get("probe_changes")
                and not [c for c in _handled(rec) if c.split(" (")[0] not in ("EnableComponents", "DisableComponents")])
    raise ValueError("unknown check '%s'" % spec)


def verdict(rec: dict, row: dict | None) -> tuple[str, str]:
    """(verdict, why) of one press record against its retail row."""
    effect = rec.get("effect", "")
    if row is None:
        return "broken", "no retail description (tools/menus/retail_controls.json)"
    spec = row.get("effect", "")
    if not spec:
        return "broken", "the retail row names no intended effect"
    if effect == "not-pressed":
        return "broken", "not pressed (and no other press of it): " + rec.get("detail", "")
    if effect == "gone":
        return "broken", "the walk could not reach the control again"
    unhandled = [c[len("unhandled:"):] for c in rec.get("commands", []) if c.startswith("unhandled:")]
    allowed = set(row.get("retail_unhandled", [])) | set(row.get("ignore_unhandled", []))
    missing = [c for c in unhandled if c.split(" (")[0] not in allowed]
    if missing:
        return "broken", "the movie's command reached no handler: " + ", ".join(missing)
    for alt in spec.split("|"):
        if check(rec, alt):
            kind = alt.strip().split(":")[0]
            if kind == "unavailable":
                return "unavailable", rec.get("box") or "greyed out as retail"
            return "works", alt.strip()
    return "broken", "expected %s; saw: %s" % (spec, describe(rec))


def describe(rec: dict) -> str:
    effect = rec.get("effect", "")
    detail = re.sub(r"\s*\[note [^\]]*\]", "", rec.get("detail", "")).strip()
    parts = []
    if effect == "screen":
        parts.append("screen: " + detail)
    elif effect == "box":
        parts.append("message box: " + (rec.get("box") or detail))
    elif effect == "request":
        parts.append("asks the host for: " + detail)
    elif effect == "page":
        added = [p.split(".")[-2] if p.endswith(".bttn") else p.split(".")[-1] for p in rec.get("new_buttons", [])]
        parts.append("the screen changes (%s)%s" % (detail, (": " + ", ".join(added[:6])) if added else ""))
    elif effect == "state":
        parts.append("changes: " + detail)
    elif effect == "nothing":
        parts.append("nothing happens")
    else:
        parts.append(detail or effect)
    own_a, own_b = rec.get("own_before") or {}, rec.get("own_after") or {}
    own = ["%s %s -> %s" % (k, own_a.get(k), own_b.get(k)) for k in ("text", "checked", "value", "selected", "open") if own_a.get(k) != own_b.get(k)]
    if own:
        parts.append("own: " + ", ".join(own))
    if rec.get("probe_changes"):
        parts.append("engine: " + "; ".join(rec["probe_changes"][:2]))
    return "; ".join(parts)


def short_control(path: str) -> str:
    p = re.sub(r"^_level\d+\.", "", path)
    p = re.sub(r"\.bttn$", "", p)
    return p


def build_rows(inv: dict, retail: list[dict]) -> list[dict]:
    """One row per (screen file, control): the first press of a control on a screen speaks for it (the walk reaches some controls from several
    places, e.g. every nav's entries from each other nav's pointer page). A control left unpressed by the front-end walk (a game start, Quit) is
    judged by its press elsewhere (the in-game half, the end of the walk)."""
    pressed: dict[str, dict] = {}
    for rec in inv["records"]:
        if rec.get("effect") not in ("not-pressed", "gone"):
            pressed.setdefault(rec["control"], rec)
    rows: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for rec in inv["records"]:
        if rec.get("effect") == "not-pressed" and rec["control"] in pressed:
            rec = pressed[rec["control"]]
        key = (rec.get("stack_top", rec["screen"].split(" > ")[0]), rec["control"])
        if key in seen:
            continue
        seen.add(key)
        row = retail_row(rec, retail)
        v, why = verdict(rec, row)
        rows.append({
            "screen": rec["screen"],
            "control": short_control(rec["control"]),
            "retail": (row or {}).get("retail", "(not described)"),
            "source": (row or {}).get("source", ""),
            "intended": (row or {}).get("effect", ""),
            "ours": describe(rec),
            "commands": ", ".join(c for c in rec.get("commands", []) if not c.startswith("PlaySound")),
            "verdict": v,
            "why": why,
            "owner": (row or {}).get("owner", ""),
            "note": (row or {}).get("note", ""),
        })
    return rows


def missing_places(inv: dict, required: list[dict]) -> list[str]:
    """The required places (retail_controls.json) the walk never reached: by the stack's top and, for the online screens, the page."""
    reached = [(p.get("stack", [""])[-1] if p.get("stack") else "", p.get("online_page", ""), p.get("online_state", -1)) for p in inv.get("places", [])]
    for rec in inv.get("records", []):
        reached.append((rec.get("stack_top", ""), rec.get("online_page", ""), rec.get("online_state", -1)))
        if rec["screen"].startswith(("Quit menu", "Score screen")):
            reached.append((rec["screen"].split(" > ")[0], "", -1))
    out = []
    for r in required:
        if not any(top == r["top"] and (not r.get("online_page") or page == r["online_page"])
                   and (not r.get("online_states") or state in r["online_states"]) for top, page, state in reached):
            out.append(r["name"])
    return out


def counts(rows: list[dict]) -> dict[str, int]:
    out = {v: 0 for v in VERDICTS}
    for r in rows:
        out[r["verdict"]] += 1
    return out


def failures(inv: dict, rows: list[dict], required: list[dict]) -> list[str]:
    """Why the inventory does not prove the menus (empty: it does)."""
    out = []
    if not inv.get("finished"):
        out.append("the walk did not finish (no 'finished' mark)")
    if not inv.get("records"):
        out.append("the walk pressed nothing")
    out += ["walk error: " + e for e in inv.get("errors", [])]
    out += ["required place not reached: " + p for p in missing_places(inv, required)]
    # a broken control fails the table whoever owns it: another lane's ownership says who fixes it, not that the menu works
    out += ["%s / %s: %s%s" % (r["screen"], r["control"], r["why"], (" (owner: %s)" % r["owner"]) if r["owner"] else "")
            for r in rows if r["verdict"] == "broken"]
    return out


def md_cell(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def render(rows: list[dict], inv: dict, fails: list[str]) -> str:
    c = counts(rows)
    lines = [
        "# Menu inventory (lane MENUWORK-1)",
        "",
        "Generated by `python3 tools/menus/menu_inventory.py` from a run of `godot/scripts/menu_inventory.gd` (every control pressed with real mouse "
        "events; each row's verdict checks the control's intended effect, from tools/menus/retail_controls.json, against the press's evidence in the "
        "game: the screen stack, a message box, a request, a handled command, the gadget's own value, the engine's screen state).",
        "",
        "Counts: " + ", ".join("%s %d" % (k, c[k]) for k in VERDICTS) + " (%d controls)" % len(rows),
        "",
        "Result: " + ("PASS" if not fails else "FAIL (%d)" % len(fails)),
        "",
    ]
    if fails:
        lines += ["Failures:", ""] + ["- " + md_cell(f) for f in fails] + [""]
    lines += ["| Screen | Control | Retail behaviour | Intended effect | Ours (evidence) | Verdict |", "|---|---|---|---|---|---|"]
    for r in rows:
        retail = r["retail"] + ((" (" + r["source"] + ")") if r["source"] else "")
        ours = r["ours"] + ((" [" + r["commands"][:300] + "]") if r["commands"] else "") + ((" Note: " + r["note"]) if r["note"] else "")
        verdict_cell = r["verdict"] + ((": " + r["why"]) if r["why"] else "")
        lines.append("| %s | %s | %s | %s | %s | %s |" % tuple(md_cell(x) for x in (r["screen"], r["control"], retail, r["intended"], ours, verdict_cell)))
    return "\n".join(lines) + "\n"


def table(inventory: Path, out: Path | None) -> int:
    if not inventory.exists():
        print("MENU INVENTORY FAIL: no inventory %s" % inventory)
        return 1
    inv = json.loads(inventory.read_text(encoding="utf-8"))
    doc = load_retail_doc()
    rows = build_rows(inv, doc["controls"])
    fails = failures(inv, rows, doc.get("required_places", []))
    text = render(rows, inv, fails)
    out = out or inventory.with_name("menu_inventory.md")
    out.write_text(text, encoding="utf-8")
    (out.with_suffix(".json")).write_text(json.dumps({"counts": counts(rows), "failures": fails, "rows": rows}, indent=1), encoding="utf-8")
    c = counts(rows)
    print("MENU INVENTORY " + ", ".join("%s %d" % (k, c[k]) for k in VERDICTS) + " -> " + str(out))
    for f in fails:
        print("FAIL " + f)
    print("MENU INVENTORY " + ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 0 if not fails else 1


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_lobby(workdir: Path) -> tuple[subprocess.Popen, str]:
    """A local lobby (services/online/lobby, as test_online4_screens.cpp starts it): (process, url). Raises when it cannot start."""
    node = shutil.which("node")
    if not node or not LOBBY_SERVER.exists():
        raise RuntimeError("the online pages need Node.js and %s" % LOBBY_SERVER)
    port = _free_port()
    (workdir / "lobby-data").mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LOBBY_BIND="127.0.0.1:%d" % port, LOBBY_DATA=str(workdir / "lobby-data"),
               TICKET_KEYS="1:000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n",
               SESSION_KEY="a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", RELAYS="", MOTD="The menu inventory's local lobby.")
    log = (workdir / "lobby.log").open("w", encoding="utf-8")
    proc = subprocess.Popen([node, str(LOBBY_SERVER)], env=env, stdout=log, stderr=subprocess.STDOUT)
    url = "http://127.0.0.1:%d" % port
    for _ in range(100):
        try:
            with urllib.request.urlopen(url + "/v1/health", timeout=1) as r:
                if r.status == 200:
                    return proc, url
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError("the local lobby did not answer on %s (see %s)" % (url, workdir / "lobby.log"))


def run(out: Path, windowed: bool, res: str, extra: list[str], timeout: int) -> int:
    godot = os.environ.get("GODOT") or shutil.which("godot")
    if not godot:
        print("menu_inventory: no godot on the PATH (set GODOT)", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    try:
        lobby, url = start_lobby(out)
    except RuntimeError as e:
        print("menu_inventory: %s" % e, file=sys.stderr)
        return 2
    try:
        env = dict(os.environ, OPENBFME_LOBBY=url)
        args = [godot] + ([] if windowed else ["--headless"]) + ["--path", str(PROJECT), "--", "--res=" + res, "--menu-inventory=" + str(out)] + extra
        log = out / "run.log"
        with log.open("w", encoding="utf-8") as f:
            p = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, env=env)
    finally:
        lobby.kill()
        lobby.wait()
    inventory = out / "inventory.json"
    if p.returncode != 0 or not inventory.exists():
        print("menu_inventory: the run did not finish (exit %d), see %s" % (p.returncode, log), file=sys.stderr)
        return 2
    return table(inventory, out / "menu_inventory.md")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--out", type=Path, required=True)
    r.add_argument("--windowed", action="store_true")
    r.add_argument("--res", default="1280x720")
    r.add_argument("--timeout", type=int, default=9000)
    r.add_argument("extra", nargs="*", help="more game.gd arguments (after --)")
    t = sub.add_parser("table")
    t.add_argument("inventory", type=Path)
    t.add_argument("--out", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "run":
        return run(a.out, a.windowed, a.res, a.extra, a.timeout)
    return table(a.inventory, a.out)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
