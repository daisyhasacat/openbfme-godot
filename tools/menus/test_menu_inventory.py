"""Lane MENUWORK-1: the menu inventory's verdicts, and that a broken button is caught.

The unit tests feed tools/menus/menu_inventory.py press records as godot/scripts/menu_inventory.gd writes them. The game test presses the Multiplayer
nav's entries twice, headless: as shipped Replays opens its screen (works); with the test hook --menu-walk-drop=LoadReplay the host ignores the
request and the inventory must call the button broken ("nothing happens"). It needs Godot 4.7 (`godot` on the PATH or GODOT), the built extension
(godot/bin) and the retail installs (ROTWK_INSTALL / BFME2_INSTALL); without them it is SKIPPED with the reason. About two minutes per run.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import menu_inventory as mi  # noqa: E402

REPO = HERE.parents[1]
PROJECT = REPO / "godot"


def rec(control: str, effect: str, **kw) -> dict:
    r = {"screen": "S", "control": control, "kind": "button", "effect": effect, "detail": "", "commands": [], "requests": [], "box": "",
         "new_buttons": [], "gadgets": [], "stack_before": ["MainMenu.apt"], "stack_after": ["MainMenu.apt"], "probe_changes": []}
    r.update(kw)
    return r


def test_the_retail_table_names_an_effect_and_a_source_for_every_row():
    doc = mi.load_retail_doc()
    assert doc["controls"] and doc["required_places"]
    for r in doc["controls"]:
        assert r["retail"].strip() and r["source"].strip(), r["match"]
        assert r.get("effect"), r["match"]
        for alt in r["effect"].split("|"):
            mi.check(rec("x", "nothing"), alt)  # every check is a known kind (an unknown one raises)


def test_checks():
    assert mi.check(rec("b", "screen", stack_after=["MainMenu.apt", "Options.apt"]), "screen:Options.apt")
    assert not mi.check(rec("b", "screen", stack_after=["MainMenu.apt", "Options.apt"]), "screen:SaveLoad.apt")
    assert mi.check(rec("b", "screen", stack_before=["A", "B"], stack_after=["A"]), "leave")
    assert mi.check(rec("b", "page", new_buttons=["_level2.Main.Easy.bttn"]), "page:Easy")
    assert not mi.check(rec("b", "page", new_buttons=["_level2.Main.Other.bttn"]), "page:Easy")
    assert mi.check(rec("b", "state", hover="opens 4 buttons under the pointer"), "hover")
    assert mi.check(rec("b", "box", box="OpenBFME: War of the Ring is not available yet: x."), "unavailable")
    assert mi.check(rec("b", "request", requests=["Credits"]), "request:Credits")
    assert mi.check(rec("b", "state", commands=["AptOptions::Reset"]), "command:AptOptions::Reset")
    assert not mi.check(rec("b", "state", commands=["unhandled:AptOptions::Reset"]), "command:AptOptions::Reset")
    assert mi.check(rec("g", "state", own_before={"value": 50}, own_after={"value": 12}), "value")
    assert mi.check(rec("g", "state", own_before={"open": False}, own_after={"open": True}), "open")
    assert mi.check(rec("g", "state", own_before={"selected": 0, "entries": 8}, own_after={"selected": 1, "entries": 8}), "select")
    assert mi.check(rec("g", "nothing", own_before={"selected": -1, "entries": 0}, own_after={"selected": -1, "entries": 0}), "select")
    assert mi.check(rec("b", "state", gadgets=["MapList#list: a -> b"]), "gadget:MapList")
    assert mi.check(rec("b", "state", probe_changes=["sort_primary: 4 -> 1"]), "probe:sort_primary")
    assert mi.check(rec("b", "state", detail="the picture changed"), "nothing")
    assert not mi.check(rec("b", "request", requests=["Credits"]), "nothing")


def test_verdicts():
    row = {"effect": "value", "retail": "r", "source": "s"}
    # a picture change proves nothing
    v, why = mi.verdict(rec("g", "state", detail="the picture changed"), row)
    assert v == "broken" and why.startswith("expected value")
    assert mi.verdict(rec("g", "state", own_before={"value": 1}, own_after={"value": 2}), row)[0] == "works"
    assert mi.verdict(rec("g", "state"), None) == ("broken", "no retail description (tools/menus/retail_controls.json)")
    assert mi.verdict(rec("g", "state"), {"retail": "r", "source": "s"})[1] == "the retail row names no intended effect"
    # a command the engine has no handler for is a defect unless retail has none either
    nav = rec("_level2.SoloPlayNav.instance108", "state", hover="opens 6 buttons", commands=["unhandled:AptMainMenu::OnSoloPlayNav"])
    assert mi.verdict(nav, {"effect": "hover"})[0] == "broken"
    assert mi.verdict(nav, {"effect": "hover", "retail_unhandled": ["AptMainMenu::OnSoloPlayNav"]})[0] == "works"
    assert mi.verdict(rec("_level2.SoloPlayNav.BonusCampaign.bttnDisabled", "state", detail="the picture changed"), {"effect": "unavailable"})[0] == "unavailable"
    assert mi.verdict(rec("b", "gone"), row)[0] == "broken"
    assert mi.verdict(rec("b", "not-pressed"), row)[0] == "broken"


def test_a_control_left_unpressed_is_judged_by_its_press_elsewhere():
    inv = {"records": [rec("_level3.lobby.StartGame.bttn", "not-pressed", stack_top="Skirmish.apt"),
                       rec("_level3.lobby.StartGame.bttn", "screen", screen="Skirmish lobby", stack_top="Skirmish.apt",
                           stack_before=["Skirmish.apt"], stack_after=["LoadScreen.apt"])], "errors": [], "finished": True}
    rows = mi.build_rows(inv, mi.load_retail())
    assert [r["verdict"] for r in rows] == ["works"]


def test_the_table_fails_closed(tmp_path):
    # Sol r1's reproduction: an empty inventory with a walk error passed
    p = tmp_path / "inventory.json"
    p.write_text(json.dumps({"records": [], "errors": ["the route could not be replayed"]}), encoding="utf-8")
    assert mi.table(p, tmp_path / "t.md") == 1
    text = (tmp_path / "t.md").read_text(encoding="utf-8")
    assert "the walk did not finish" in text and "the walk pressed nothing" in text and "walk error" in text
    assert "required place not reached: the main menu" in text
    # a finished walk with every place but one controls that pass still fails on the missing place
    doc = mi.load_retail_doc()
    places = [{"stack": [r["top"]], "online_page": r.get("online_page", ""), "online_state": (r.get("online_states") or [-1])[0]}
              for r in doc["required_places"] if r["name"] != "Options"]
    good = rec("_level2.MultiPlayNav.Replay.bttn", "screen", stack_after=["MainMenu.apt", "SaveLoad.apt"], stack_top="MainMenu.apt")
    inv = {"records": [good, rec("x", "page", screen="Quit menu (in game)"), rec("y", "page", screen="Score screen")], "errors": [],
           "finished": True, "places": places}
    fails = mi.failures(inv, mi.build_rows({"records": [good]}, doc["controls"]), doc["required_places"])
    assert fails == ["required place not reached: Options"]
    # a broken control fails the table even when its retail row names another lane as the owner (Sol r2): the owner is only named in the failure
    rows = [{"screen": "S", "control": "C", "verdict": "broken", "why": "w", "owner": "ONLINE-5"}]
    owned = mi.failures({"records": [good], "finished": True, "places": places + [{"stack": ["Options.apt"]}]}, rows, doc["required_places"])
    assert owned == ["S / C: w (owner: ONLINE-5)"]


def _godot() -> str | None:
    return os.environ.get("GODOT") or shutil.which("godot")


def _skip_reason() -> str | None:
    if not _godot():
        return "no godot on the PATH (set GODOT)"
    if not any((PROJECT / "bin").glob("openbfme.*")):
        return "the openbfme extension is not built (godot/bin)"
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        return "ROTWK_INSTALL / BFME2_INSTALL unset"
    return None


def _replay_row(*extra: str) -> dict:
    with tempfile.TemporaryDirectory(prefix="openbfme-menu-inventory-") as out:
        env = dict(os.environ, MENU_INVENTORY_ONLY="MainMenu > MultiPlayNav.instance80 (pointer)", MENU_INVENTORY_NO_GAME="1",
                   HOME=out, XDG_DATA_HOME=str(Path(out) / "data"))
        args = [_godot(), "--headless", "--path", str(PROJECT), "--", "--res=1280x720", f"--menu-inventory={out}", *extra]
        p = subprocess.run(args, capture_output=True, text=True, errors="replace", timeout=900, env=env)
        assert p.returncode == 0, (p.stdout + p.stderr)[-3000:]
        inv = json.loads((Path(out) / "inventory.json").read_text(encoding="utf-8"))
    rows = mi.build_rows(inv, mi.load_retail())
    found = [r for r in rows if r["control"] == "MultiPlayNav.Replay"]
    assert len(found) == 1, [r["control"] for r in rows]
    return found[0]


@pytest.mark.skipif(_skip_reason() is not None, reason=str(_skip_reason()))
def test_the_inventory_passes_replays_and_catches_it_dropped():
    row = _replay_row()
    assert row["verdict"] == "works", row
    assert row["why"] == "screen:SaveLoad.apt"
    row = _replay_row("--menu-walk-drop=LoadReplay")
    assert row["verdict"] == "broken", (row["verdict"], row["ours"], row["why"], row["commands"])
