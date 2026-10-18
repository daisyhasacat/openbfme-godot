"""Lane RENDERWAIT-1: no per-frame render path may wait for the logic worker.

The render (main) thread reads the live logic only while it is held (LiveGame's client window, or an idle worker); a bound GameWorld / InGameHudNode
method that reaches the live logic waits (LiveGame::holdLogic -> waitIdle) when called outside it. UBER-1's 10k battle spent 110 ms of every 164 ms frame
in such a wait: the spell ring's per-frame get_spellbook_state (InGameHud::enterContext). This test reads the real code:
  1. which bound methods wait: their C++ body reaches the live logic (the waiting accessors, enterContext, waitLogic, waitSpellIdle, createObject);
  2. which scripts run every render frame of a game: game.gd's per-frame functions and the _process of every script game.gd loads;
  3. a per-frame function may call a waiting method only where its control flow has established `is_logic_held()`: after an
     `if not <held>: return / continue` of an enclosing block, inside an `if <held> [and ...]:` body, behind `<held> and` in a condition, or in the
     true arm of `x if <held> else y`. A guard in another branch, or after the call, does not count (review r1 #3). Helpers are followed.
The scripts: game.gd's PLAYING branch and per-frame functions, campaign_flow.gd's tick, and the _process of every script reachable from game.gd by
its "res://scripts/*.gd" names (campaign_cine.gd through campaign_flow.gd).
It fails on the code before RENDERWAIT-1 (spell_target_ring.gd's _process, game_options.gd's clock).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "godot" / "scripts"
DEVICE = ROOT / "engine" / "src" / "GodotDevice"

# game.gd's functions run every rendered frame while a game is played (_process and what it calls each frame)
GAME_PER_FRAME = ["_process", "_perf_frame", "_perf_log_frame", "_update_listener", "_end_game_tick"]
# (script, function, the call in game.gd's PLAYING branch) run every render frame of a game from game.gd
EXTERNAL_PER_FRAME = [("campaign_flow.gd", "tick", "_campaign_flow.tick()")]
# bound methods that end or replace the game: reached from the per-frame functions once (the game's end, a mission's teardown and the next one's load),
# never every frame; they wait for the worker by design (the game goes away under it)
ONE_OFF = {"clear_game_data", "start_campaign_mission"}
# what reaches the live logic in a bound method's body (each waits for the worker outside the client window)
WAITING = re.compile(
    r"(->|\.)(logic|players|ai|shroud|commands|dispatch|aiCommands|buildCommands|spellCommands|launchBones|createObject|waitIdle|holdLogic)\(|"
    r"\benterContext\(|\bwaitLogic\(|\bwaitSpellIdle\(")


def cpp_body(source: str, start: int) -> str:
    body = source.index("{", start)
    depth = 0
    for i in range(body, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[body:i + 1]
    raise AssertionError("unterminated body")


GAMECLIENT = ROOT / "engine" / "src" / "GameClient"
DEF = r"^[\w:<>,\s\*&]*\b(\w+)::(\w+)\([^;{{]*\)\s*(?:const\s*)?\{{"


def cpp_definitions(paths, classes) -> dict:
    """{(class, method): body} of the member functions of `classes` defined in `paths`"""
    out = {}
    for path in paths:
        text = path.read_text(encoding="utf-8")
        for m in re.finditer(DEF.format(), text, re.M):
            if m.group(1) in classes:
                out[(m.group(1), m.group(2))] = cpp_body(text, m.start())
    return out


def _balanced(text: str, open_at: int) -> int:
    """the index just past the bracket that closes the one at `open_at`"""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] in "([{":
            depth += 1
        elif text[i] in ")]}":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(text)


def _top_level_split(text: str, sep: str) -> list:
    """`text` cut at `sep` where no bracket is open"""
    parts, depth, last, i = [], 0, 0, 0
    while i < len(text):
        if text[i] in "([{":
            depth += 1
        elif text[i] in ")]}":
            depth -= 1
        elif depth == 0 and text.startswith(sep, i):
            parts.append(text[last:i])
            last = i + len(sep)
            i += len(sep) - 1
        i += 1
    parts.append(text[last:])
    return parts


HELD_C = r"(?:\w+(?:->|\.))*logicHeld\(\)"


def establishes_held(cond: str) -> bool:
    """the condition is true only where the logic is held: no top-level `||`, and one `&&` conjunct is `logicHeld()` itself (review r2, DEVPORT-5: `logicHeld() ||
    x` is true while a frame runs)"""
    if len(_top_level_split(cond, "||")) > 1:
        return False
    return any(re.fullmatch(r"\s*\(?\s*" + HELD_C + r"\s*\)?\s*", c) for c in _top_level_split(cond, "&&"))


def leaves_when_not_held(cond: str) -> bool:
    """the condition is true whenever the logic is NOT held (a top-level `||` disjunct is `!logicHeld()`): an `if` on it that returns leaves only held code behind"""
    return any(re.fullmatch(r"\s*!\s*" + HELD_C + r"\s*", c) for c in _top_level_split(cond, "||"))


def _if_condition(header: str):
    found = list(re.finditer(r"\bif\s*\(", header))
    if not found:
        return None
    start = found[-1].end() - 1
    return header[start + 1:_balanced(header, start) - 1]


def guarded_spans(body: str) -> list:
    """[(start, end)] of the body where the logic is known to be held (reviews r1 / r2, DEVPORT-5), each respecting the block it is in:
      - the block of an `if (cond)` whose cond establishes_held (not `held || x`, not `else`);
      - what follows an `if (cond)` block, up to the end of the ENCLOSING block, when cond leaves_when_not_held and the block ends in a return;
      - an expression's `logicHeld() &&` tail / `logicHeld() ?` true arm, up to the end of its bracket group (or statement), unless that group has a top-level `||`.
    A body is never exempted wholesale for containing logicHeld() somewhere."""
    spans = []
    stack = []   # frames: {"start", "header", "after": [offsets where an early-return guard starts in this block]}
    last = 0     # the start of the current statement / header
    for i, c in enumerate(body):
        if c == "{":
            stack.append({"start": i, "header": body[last:i], "after": []})
            last = i + 1
        elif c == "}":
            if stack:
                frame = stack.pop()
                cond = _if_condition(frame["header"])
                inner = body[frame["start"] + 1:i]
                if cond is not None and establishes_held(cond):
                    spans.append((frame["start"], i))
                if cond is not None and leaves_when_not_held(cond) and re.search(r"\breturn\b[^;{}]*;\s*$", inner) and stack:
                    stack[-1]["after"].append(i + 1)
                for offset in frame["after"]:
                    spans.append((offset, i))  # to the end of the block that held the early return
            last = i + 1
        elif c == ";":
            last = i + 1
    for m in re.finditer(r"(" + HELD_C + r")\s*(&&|\?)", body):
        if body[:m.start()].rstrip().endswith("!"):
            continue
        # the bracket group (or statement) the expression sits in
        depth, gs = 0, 0
        for j in range(m.start() - 1, -1, -1):
            if body[j] in ")]}":
                depth += 1
            elif body[j] in "([{":
                if depth == 0:
                    gs = j + 1
                    break
                depth -= 1
            elif body[j] == ";" and depth == 0:
                gs = j + 1
                break
        ge = len(body)
        depth = 0
        for j in range(m.end(), len(body)):
            if body[j] in "([{":
                depth += 1
            elif body[j] in ")]}":
                if depth == 0:
                    ge = j
                    break
                depth -= 1
            elif body[j] == ";" and depth == 0:
                ge = j
                break
        if len(_top_level_split(body[gs:ge], "||")) > 1:
            continue
        if m.group(2) == "?":
            colon = _top_level_split(body[m.end():ge], ":")
            ge = m.end() + len(colon[0])  # the true arm only
        spans.append((m.end(), ge))
    return spans


def unguarded_reads(body: str, callees) -> list:
    """what in a C++ body reaches the live logic where it is not known to be held: direct reads (WAITING) and calls of `callees` (names of waiting functions)"""
    body = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), body)  # a comment that names a call is not one
    spans = guarded_spans(body)
    pattern = WAITING.pattern
    if callees:
        pattern += r"|(?<![\w.>:])(" + "|".join(sorted(re.escape(c) for c in callees)) + r")\("  # a method of the same class called as itself (a name on another object is not it)
    out = []
    for m in re.finditer(pattern, body):
        if not any(a <= m.start() < b for a, b in spans):
            out.append(m.group(0))
    return out


def full_waits() -> set:
    """names of LiveGame functions that wait for EVERY requested frame (waitIdle / drainFrames, transitively): a held client window does not make these safe, they
    lift it and run the queued frames (LiveGame::waitIdle). holdLogic() does not."""
    defs = cpp_definitions(list(GAMECLIENT.glob("LiveGame*.cpp")), {"LiveGame"})
    names = {m for (c, m), b in defs.items() if re.search(r"\b(waitIdle|drainFrames)\(", b) and m not in ("waitIdle", "holdLogic")}
    names |= {"waitIdle", "drainFrames"}
    changed = True
    while changed:
        changed = False
        for (c, m), b in defs.items():
            if m not in names and m != "holdLogic" and re.search(r"(?<![\w])(" + "|".join(map(re.escape, names)) + r")\(", b):
                names.add(m)
                changed = True
    return names


def device_functions() -> dict:
    return cpp_definitions(list(DEVICE.glob("*.cpp")), {"GameWorld", "InGameHudNode"})


def waiting_methods(with_full: bool = False, defs: dict = None, full: set = None) -> dict:
    """{bound name: C++ method} of GameWorld / InGameHudNode that reach the live logic where it is not known to be held, directly or through another of their
    methods (a nested call: GameWorld::net_status -> net_observed). with_full=True: instead the bound methods that reach a full wait (see full_waits), through
    a LiveGame call on any member (m_game->report()) or through another method of their own class called as itself (net_observed()), guarded or not: a held
    window does not make a full wait safe. `defs` / `full` let a test run the analysis on mutated bodies."""
    defs = device_functions() if defs is None else defs
    full = full_waits() if full is None else full
    waiting = set()
    changed = True
    while changed:
        changed = False
        for (cls, method), body in defs.items():
            if method in waiting:
                continue
            code = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), body)
            if with_full:
                hit = re.search(r"(?:->|\.)(" + "|".join(map(re.escape, sorted(full))) + r")\(", code)
                if not hit and waiting:
                    hit = re.search(r"(?:(?<![\w.>:])|\bthis->)(" + "|".join(map(re.escape, sorted(waiting))) + r")\(", code)
            else:
                hit = unguarded_reads(body, waiting)
            if hit:
                waiting.add(method)
                changed = True
    binds = []
    for path in DEVICE.glob("*.cpp"):
        binds += re.findall(r'bind_method\(D_METHOD\("(\w+)"[^)]*\)\s*,\s*&(GameWorld|InGameHudNode)::(\w+)', path.read_text(encoding="utf-8"))
    return {name: f"{cls}::{method}" for name, cls, method in binds if method in waiting}


def gd_function(source: str, name: str) -> str:
    m = re.search(r"^func " + re.escape(name) + r"\(.*?$(.*?)(?=^func |\Z)", source, re.M | re.S)
    return m.group(1) if m else ""


HELD_CALL = r"(?:[\w.]+\.)?is_logic_held\(\)"


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip("\t "))


def unguarded_waiting_calls(source: str, body: str, waiting: dict, entry_guarded: bool = False, _depth: int = 0, ignore_guards: bool = False) -> list:
    """the waiting calls of `body` (a function's lines; its helpers in `source` are followed) made where the control flow has not established that the
    logic is held"""
    funcs = set(re.findall(r"^func (\w+)\(", source, re.M))
    lines = [l for l in body.split("\n") if l.strip() and not l.strip().startswith("#")]
    held_names = set(re.findall(r"^\s*var (\w+)(?:: bool)? :?= (?!not )[^#\n]*?" + HELD_CALL, body, re.M))
    held = "(?:" + "|".join([HELD_CALL] + [re.escape(n) + r"\b" for n in sorted(held_names)]) + ")"
    out = []
    after_guard = []   # indents d: an `if not held: return / continue` was passed in the block at indent d
    inside_held = []   # indents d: inside the body of an `if held [and ...]:` at indent d
    for k, line in enumerate(lines):
        d = _indent(line)
        after_guard = [g for g in after_guard if g <= d]
        inside_held = [g for g in inside_held if g < d]
        text = line.strip()
        base = entry_guarded or bool(after_guard) or bool(inside_held)
        # where in this line the held state is established: behind `held and` of an and-only condition, before ` if held else`
        from_col, until_col = None, None
        cond = re.match(r"(?:if|elif|while) (.*):\s*(?:#.*)?$", text)
        positive = False
        if cond and " or " not in cond.group(1):
            parts = cond.group(1).split(" and ")
            col = len(text) - len(text.lstrip()) + text.index(cond.group(1))
            for part in parts:
                if re.fullmatch(held, part.strip()):
                    positive = True
                    from_col = col + len(part)
                    break
                col += len(part) + len(" and ")
        tern = re.search(r" if " + held + r" else ", text)
        if tern:
            until_col = tern.start()
        for m in list(re.finditer(r"\b[\w]+\.(\w+)\(", text)) + list(re.finditer(r"(?<![\w.])(\w+)\(", text)):
            name = m.group(1)
            guarded = base or (from_col is not None and m.start() >= from_col) or (until_col is not None and m.start() < until_col)
            guarded = guarded and not ignore_guards  # a full wait lifts the held window: the guard does not make it safe
            if "." in m.group(0) and name in waiting:
                if not guarded:
                    out.append(name)
            elif "." not in m.group(0) and name in funcs and not text.startswith("func ") and _depth < 4:
                out += unguarded_waiting_calls(source, gd_function(source, name), waiting, guarded, _depth + 1, ignore_guards)
        if positive and text.startswith("if "):
            inside_held.append(d)
        if re.fullmatch(r"if not " + held + r":(?:\s*#.*)?", text) and k + 1 < len(lines) and _indent(lines[k + 1]) == d + 1 \
                and lines[k + 1].strip().split("#")[0].strip() in ("return", "continue"):
            after_guard.append(d)
    return out


def reachable_scripts(start: str = "game.gd") -> list:
    seen, todo = [], [start]
    while todo:
        name = todo.pop()
        path = SCRIPTS / name
        if name in seen or not path.exists():
            continue
        seen.append(name)
        todo += re.findall(r'"res://scripts/(\w+\.gd)"', path.read_text(encoding="utf-8"))
    return sorted(seen)


def per_frame_functions() -> list:
    """[(file, function, source, body)] run every render frame of a game"""
    game = (SCRIPTS / "game.gd").read_text(encoding="utf-8")
    # game.gd's _process while a game is played: its PLAYING branch (the menu's and the score screen's run without a game on the worker)
    process = gd_function(game, "_process")
    playing = process[process.index("elif _state == State.PLAYING:"):process.index("elif _state == State.SCORE:")]
    out = [("game.gd", "_process (PLAYING)", game, playing)]
    out += [("game.gd", f, game, gd_function(game, f)) for f in GAME_PER_FRAME if f != "_process"]
    for script, func, call in EXTERNAL_PER_FRAME:
        assert call in playing, f"game.gd's PLAYING branch no longer calls {call}: update EXTERNAL_PER_FRAME"
        text = (SCRIPTS / script).read_text(encoding="utf-8")
        out.append((script, func, text, gd_function(text, func)))
    for script in reachable_scripts():
        if script == "game.gd":
            continue
        text = (SCRIPTS / script).read_text(encoding="utf-8")
        body = gd_function(text, "_process")
        if body:
            out.append((script, "_process", text, body))
    return out


def test_the_waiting_set_is_found():
    waiting = waiting_methods()
    # known members of both classes (their bodies wait for the worker outside the window)
    for name in ("get_frame", "get_object", "get_spellbook_state", "pixel_to_world", "get_ground_height", "create_object", "debug_script_unit"):
        assert name in waiting, f"{name} is not recognised as a waiting bound method: the parser of this test is broken"
    for name in ("get_presented_frame", "get_frame_timings", "get_camera", "get_move_hints", "is_logic_held", "get_logic_wait_sites",
                 "update_script_view", "take_script_requests"):
        assert name not in waiting, f"{name} reaches the live logic ({waiting.get(name)}): it must not wait, scripts call it every frame"


def per_frame_violations(waiting: dict, full: dict) -> list:
    """what the per-frame scripts call that waits: a waiting method where the logic is not known to be held, or a full-wait method even guarded"""
    bad = []
    for script, func, source, body in per_frame_functions():
        for name in unguarded_waiting_calls(source, body, waiting):
            if name not in ONE_OFF and name not in NOT_THE_WORLD:
                bad.append(f"{script} {func} calls {name} ({waiting[name]}) where the logic is not known to be held")
        for name in unguarded_waiting_calls(source, body, full, ignore_guards=True):
            if name not in ONE_OFF and name not in NOT_THE_WORLD:
                bad.append(f"{script} {func} calls {name} ({full[name]}), which waits for every queued frame even in a held window")
    return bad


def test_per_frame_scripts_never_call_a_waiting_method_unguarded():
    waiting = waiting_methods()
    found = per_frame_functions()
    # lane SPELL-4: spell_target_ring.gd is gone (the HUD draws the radius cursor in InGameHud::update, which runs only while the logic is held)
    for script in ("move_hints.gd", "game_options.gd", "qa_player.gd", "campaign_cine.gd"):
        assert any(f == script for f, _, _, _ in found), f"{script}'s _process is not among the per-frame functions"
    bad = []
    for script, func, source, body in found:
        for name in unguarded_waiting_calls(source, body, waiting):
            if name in ONE_OFF or name in NOT_THE_WORLD:
                continue
            bad.append(f"{script} {func} calls {name} ({waiting[name]}) where the logic is not known to be held")
    assert not bad, "per-frame render paths that wait for the logic worker:\n" + "\n".join(bad)


# a script object's own method of the same name as a bound method that is not called on the world (campaign_flow.gd's tick calls _cine.setup)
NOT_THE_WORLD = {"setup"}


def test_per_frame_scripts_never_call_a_full_wait_method_even_guarded():
    """review r1 (DEVPORT-5): `is_logic_held()` is true inside a client window that still holds queued frames, and LiveGame::waitIdle / drainFrames lift that window
    and run them. A bound method that reaches one of those (the old observeNextPlayer behind the Tab key) is not made safe by the script's guard."""
    full = waiting_methods(with_full=True)
    assert "set_logic_thread" in full and "start_new_game" in full, "the full-wait parser of this test is broken"
    bad = []
    for script, func, source, body in per_frame_functions():
        for name in unguarded_waiting_calls(source, body, full, ignore_guards=True):
            if name not in ONE_OFF and name not in NOT_THE_WORLD:
                bad.append(f"{script} {func} calls {name} ({full[name]}), which waits for every queued frame even in a held window")
    assert not bad, "\n".join(bad)


def test_a_status_the_overlay_asks_every_frame_does_not_wait_through_a_nested_call():
    """review r1 (DEVPORT-5): GameWorld::net_status (game.gd's _net_tick, every frame) kept its hash behind logicHeld() and then called net_observed() outside it;
    the old classification exempted the whole body for containing logicHeld() and did not follow the nested call"""
    waiting = waiting_methods()
    assert "net_observed" in waiting and "net_status" not in waiting, "net_status must reach net_observed only through net_observed_unwaiting"
    assert "net_observed_unwaiting" not in waiting
    assert "caster_status" not in waiting_methods(with_full=True), "caster_status (the caster bar, 5 Hz) must not use LiveGame::report(), a full wait"


def test_the_cpp_guard_analysis():
    guarded = "void f() {\n\tif (g->logicHeld())\n\t{\n\t\tx = g->players();\n\t}\n}"
    assert unguarded_reads(guarded, set()) == []
    # a guard elsewhere in the body does not cover a read outside it (the wholesale exemption this replaces)
    nested = "void f() {\n\tif (g->logicHeld())\n\t{\n\t\th = 1;\n\t}\n\tx = g->players();\n}"
    assert unguarded_reads(nested, set()) == ["->players("]
    assert unguarded_reads("void f() {\n\tj = net_observed();\n}", {"net_observed"}) == ["net_observed("]
    assert unguarded_reads("void f() {\n\tif (g->logicHeld())\n\t{\n\t\tj = net_observed();\n\t}\n}", {"net_observed"}) == []
    # an early return on a negated guard covers the rest of the block; one that does not return does not
    early = "void f() {\n\tif (!g->logicHeld())\n\t{\n\t\treturn;\n\t}\n\tx = g->players();\n}"
    assert unguarded_reads(early, set()) == []
    assert unguarded_reads(early.replace("return;", "y = 1;"), set()) == ["->players("]
    # the positive condition's else arm is not guarded
    other = "void f() {\n\tif (g->logicHeld())\n\t{\n\t\ty = 1;\n\t}\n\telse\n\t{\n\t\tx = g->players();\n\t}\n}"
    assert unguarded_reads(other, set()) == ["->players("]
    assert unguarded_reads("void f() {\n\tx = g->logicHeld() && g->players() != nullptr;\n}", set()) == []
    assert unguarded_reads("void f() {\n\tx = g->players() != nullptr && g->logicHeld();\n}", set()) == ["->players("]


def _mutated(key, old, new):
    defs = dict(device_functions())
    assert old in defs[key], f"{key} no longer contains {old!r}: update the mutation"
    defs[key] = defs[key].replace(old, new, 1)
    return defs


def _violations(defs):
    full_set = full_waits()
    return per_frame_violations(waiting_methods(defs=defs, full=full_set), waiting_methods(with_full=True, defs=defs, full=full_set))


def test_the_real_tree_has_no_per_frame_violation():
    assert per_frame_violations(waiting_methods(), waiting_methods(with_full=True)) == []


def test_mutation_a_full_wait_inside_a_nested_same_class_call_is_followed():
    """review r2 #1: report() added to net_observed() (waits for every frame) reaches net_status() and caster_status() through plain calls of their own class"""
    defs = _mutated(("GameWorld", "net_observed"), "{", "{\n\tm_game->report();", )
    full = waiting_methods(with_full=True, defs=defs)
    for name in ("net_observed", "net_status", "caster_status"):
        assert name in full, f"{name} reaches report() through net_observed() but is not found"
    assert any("net_status" in v for v in _violations(defs))
    # the safe variant: a read behind a proper held block is not a full wait (nor a violation)
    safe = _mutated(("GameWorld", "net_status"), "if (m_game->logicHeld())\n\t{", "if (m_game->logicHeld())\n\t{\n\t\tconst auto *held = &m_game->players();")
    assert _violations(safe) == []


def test_mutation_a_guard_that_is_true_while_a_frame_runs_is_not_a_guard():
    """review r2 #2: `logicHeld() || observed >= 0` lets net_observed() read the live logic during a running frame"""
    defs = _mutated(("GameWorld", "net_observed_unwaiting"), "if (m_game->logicHeld())", "if (m_game->logicHeld() || observed >= 0)")
    waiting = waiting_methods(defs=defs)
    assert "net_status" in waiting  # reached through the helper (net_observed_unwaiting is not bound)
    assert any("net_status" in v for v in _violations(defs))
    # a conjunct keeps it a guard
    ok = _mutated(("GameWorld", "net_observed_unwaiting"), "if (m_game->logicHeld())", "if (observed >= 0 && m_game->logicHeld())")
    assert _violations(ok) == []


def test_the_cpp_guard_analysis_respects_conditions_and_block_boundaries():
    def reads(body, callees=()):
        return unguarded_reads("void f() {\n" + body + "\n}", set(callees))
    read = "x = g->players();"
    assert reads("if (g->logicHeld()) { " + read + " }") == []
    assert reads("if (a && g->logicHeld()) { " + read + " }") == []
    assert reads("if (g->logicHeld() && a) { " + read + " }") == []
    assert reads("if (g->logicHeld() || a) { " + read + " }") == ["->players("]
    assert reads("if (a || g->logicHeld()) { " + read + " }") == ["->players("]
    assert reads("if (!g->logicHeld()) { " + read + " }") == ["->players("]
    assert reads("if (g->logicHeld()) { y = 1; } else { " + read + " }") == ["->players("]
    assert reads("if (g->logicHeld()) { y = 1; } else if (a) { " + read + " }") == ["->players("]
    # an early return covers the rest of ITS block only
    assert reads("if (!g->logicHeld()) { return; }\n" + read) == []
    assert reads("if (!g->logicHeld() || a) { return; }\n" + read) == []
    assert reads("if (a) { if (!g->logicHeld()) { return; } }\n" + read) == ["->players("]
    assert reads("if (a) { if (!g->logicHeld()) { return; } " + read + " }\n" + read) == ["->players("]
    assert reads("if (!g->logicHeld()) { if (b) { return; } }\n" + read) == ["->players("]
    assert reads("if (!g->logicHeld() && a) { return; }\n" + read) == ["->players("]
    # expressions: the tail of a held conjunction, the true arm of a conditional; a disjunction in the group is not a guard
    assert reads("x = g->logicHeld() && g->players() != nullptr;") == []
    assert reads("x = !g->logicHeld() && g->players() != nullptr;") == ["->players("]
    assert reads("x = g->logicHeld() ? g->players() : nullptr;") == []
    assert reads("x = g->logicHeld() ? nullptr : g->players();") == ["->players("]
    assert reads("x = (g->logicHeld() || a) && g->players() != nullptr;") == ["->players("]


def test_the_script_audio_of_a_request_waits_for_a_held_logic():
    """campaign_cine.gd's take_script_requests / update_script_view (every frame): the requests' audio reads the live game (review r1 #2)"""
    text = (DEVICE / "GodotGameWorldScripts.cpp").read_text(encoding="utf-8")
    take = cpp_body(text, text.index("Array GameWorld::take_script_requests()"))
    assert "applyScriptRequest" not in take, "take_script_requests applies the audio itself: it reads the live game outside the client window"
    apply = cpp_body(text, text.index("void GameWorld::applyPendingScriptAudio()"))
    assert apply.index("logicHeld()") < apply.index("applyScriptRequest")
    view = cpp_body(text, text.index("Dictionary GameWorld::update_script_view("))
    assert "presentedObjectPosition" in view and not WAITING.search(view)


def test_the_guard_itself_does_not_wait():
    text = (DEVICE / "GodotGameWorld.cpp").read_text(encoding="utf-8")
    body = cpp_body(text, text.index("bool GameWorld::is_logic_held() const"))
    assert "logicHeld()" in body and not WAITING.search(body)


def test_the_detector_reads_the_control_flow():
    waiting = {"get_x": "GameWorld::get_x"}

    def bad(body, source=""):
        return unguarded_waiting_calls(source or body, body, waiting)
    assert bad("\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tif not world.is_logic_held():\n\t\treturn\n\tvar v = world.get_x()\n") == []
    # a guard after the call, in another branch, or one that does not leave
    assert bad("\tvar v = world.get_x()\n\tif not world.is_logic_held():\n\t\treturn\n") == ["get_x"]
    assert bad("\tif a:\n\t\tif not world.is_logic_held():\n\t\t\treturn\n\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tif not world.is_logic_held():\n\t\tprint(1)\n\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tvar n = world.is_logic_held()\n\tvar v = world.get_x()\n") == ["get_x"]
    # a loop's continue covers the rest of the loop body only
    loop = "\tvar held: bool = world.is_logic_held()\n\tfor h in hs:\n\t\tif not held:\n\t\t\tcontinue\n\t\tworld.get_x()\n"
    assert bad(loop) == []
    assert bad(loop + "\tworld.get_x()\n") == ["get_x"]
    # the positive forms: an if body (not its else), a condition's later conjunct, the true arm of a conditional expression
    assert bad("\tif world.is_logic_held():\n\t\tworld.get_x()\n") == []
    assert bad("\tif world.is_logic_held():\n\t\tpass\n\telse:\n\t\tworld.get_x()\n") == ["get_x"]
    assert bad("\tif a and world.is_logic_held() and world.get_x() > 0:\n\t\tpass\n") == []
    assert bad("\tif world.get_x() > 0 and world.is_logic_held():\n\t\tpass\n") == ["get_x"]
    assert bad("\tif a or world.is_logic_held():\n\t\tworld.get_x()\n") == ["get_x"]
    assert bad("\tvar held = world.is_logic_held()\n\tz = world.get_x() if held else z\n") == []
    assert bad("\tvar held = world.is_logic_held()\n\tz = z if held else world.get_x()\n") == ["get_x"]
    # a helper is read at its call: guarded there or not
    source = "func _process(d):\n\t_h()\n\nfunc _h():\n\treturn world.get_x()\n"
    assert unguarded_waiting_calls(source, gd_function(source, "_process"), waiting) == ["get_x"]
    source = "func _process(d):\n\tif world.is_logic_held():\n\t\t_h()\n\nfunc _h():\n\treturn world.get_x()\n"
    assert unguarded_waiting_calls(source, gd_function(source, "_process"), waiting) == []


def test_every_deferred_list_of_renderwait1_is_cleared_when_the_scene_goes():
    """review r2: what the render side defers for a game must not reach the next one (godot/tests/renderwait1_teardown_test.gd runs the real teardown)"""
    text = (DEVICE / "GodotGameWorld.cpp").read_text(encoding="utf-8")
    body = cpp_body(text, text.index("void GameWorld::clear_scene()"))
    for member in ("m_pendingScriptAudio.clear()", "m_pendingScriptRequests.clear()", "m_fxPendingDt = 0.0", "m_netHash = 0"):
        assert member in body, f"clear_scene() leaves {member.split('.')[0].split(' ')[0]} of the old game for the next one"


def test_space_indented_per_frame_guard_is_recognized():
    source = "func _process(delta):\n var held: bool = hud.is_logic_held()\n if not held:\n  return\n hud.get_state()\n"
    assert unguarded_waiting_calls(source, gd_function(source, "_process"), {"get_state": "HUD"}) == []
    unguarded = source.replace(" if not held:\n  return\n", "")
    assert unguarded_waiting_calls(unguarded, gd_function(unguarded, "_process"), {"get_state": "HUD"}) == ["get_state"]
