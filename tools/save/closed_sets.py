#!/usr/bin/env python3
"""The closed-set audit of the snapshot's integer transfers (lane SAVE-4e, SAVES.md section 8 "closed sets").

    python3 tools/save/closed_sets.py [--root engine/src] [--overrides tools/save/closed_sets.json] [--list]

Every integer transfer of the snapshot code in a .cpp or .h file under the root is found and put in exactly one class. Review r2 (Sol): the discovery
reads each file's code (comments and strings blanked) as one text, so neither the formatting nor the receiver's name matters: every call of an Xfer
integer primitive (xferInt / xferUnsignedInt / xferByte / xferUnsignedByte / xferShort / xferUnsignedShort / xferEnum / xferInt64 / xferUnsignedInt64 /
xferObjectID / xferUser / xferImplementation, through any object, over any lines) and every call of a value or checked helper of the snapshot
namespaces (HELPERS), its argument read with its parentheses balanced. It fails closed on a way around it: a helper of SnapshotXfer / AIXfer /
SnapshotXferUtil that transfers an integer and is in no kind of HELPERS, an Xfer method in no kind of XFER_METHODS, a `using` of a helper namespace,
a macro that transfers an integer. What it cannot see: a transfer of an integer through a non-integer primitive (a float's bits, a string). The
classes:

  ref     an object id by its transfer (xferObjectID, an id list helper): looked up when used (GameLogic::findObjectByID is bounded); STOPS S-5014;
  closed  its name says it is a mask, a flag set, an enum (type, kind, state, mode, phase, stance, rule ...), an index, a slot, a player, a side, a
          team, a rank or level: a check of the loaded value must follow it in its function (SnapshotXfer::checkRange / enumIn / maskWords /
          maskWithin, AIXfer::inRange / enumIn, or a throw XferException naming the variable) after the transfer (a check before it, even on its line, does not count) in the statements that follow;
  id      an object / team / script id: the same, with the id helpers (findObject, objectId, issued ids, findTeamByID ...) counted as checks;
  open    a frame, a timer, a counter, a quantity, a seed, a hash, a size the container helpers bound: any value the type holds is legal (a name
          of an open and a closed word both, m_maskCount, is listed instead);
  listed  any other transfer: checked like a closed one when a check follows it, else it must have an entry in the overrides file (closed: <the check>, checked by <symbol> in <file>: <the check> for a check
          made elsewhere (the file must hold the symbol), or open: <why>), keyed file::function::name#n.

The overrides file may also move a transfer of any class (an entry names why). It exits 1 on: no transfer found (an empty or wrong root), a closed or
id transfer without its check, a transfer of no class and no entry, an entry that names no transfer (stale) or has no reason. Not simulation code.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

INT_XFER = re.compile(r"\bxfer(?:Int|UnsignedInt|Byte|UnsignedByte|Short|UnsignedShort|Enum|Int64|UnsignedInt64|ObjectID|User|Implementation)\s*\(")
# the Xfer class itself (its primitives are what the audit looks for, not sites); every method it declares is in one kind here, so a new way to move
# bytes fails the audit until it is placed: an integer primitive (its calls are sites), a count checked against the bytes left and the budgets (Xfer::
# xferCount), or no integer (a bool, a float, a string, a version, a block)
XFER_FILES = re.compile(r"^Common/(Xfer\w*\.h|System/Xfer\w*\.cpp)$")
XFER_METHODS = {
    "integer": {"xferByte", "xferUnsignedByte", "xferShort", "xferUnsignedShort", "xferInt", "xferUnsignedInt", "xferInt64", "xferUnsignedInt64",
                "xferEnum", "xferObjectID", "xferUser", "xferImplementation"},
    "counted": {"xferCount", "xferVectorSize", "checkLoadedCount"},
    "none": {"xferBool", "xferReal", "xferAsciiString", "xferUnicodeString", "xferVersion", "xferSnapshot", "getXferMode", "isLoading", "beginBlock",
             "endBlock", "skipBlock", "loadBytesLeftInBlock", "chargeLoad", "loadBytesLeft"},
}
# the transfers whose type says they carry object ids (Xfer::xferObjectID, the id list helpers): an object reference, looked up when it is used
# (GameLogic::findObjectByID is bounded: an id that names no object finds none); not checked against the ids the game has issued (STOPS S-5014)
REF_HELPERS = {"ids", "idSet", "stlObjectIDList", "idContainer"}
VALUE_ARG = {"objectPointers": 2, "containerPointer": 2}  # the helpers whose value is not their second argument (after the Xfer and the logic)
HELPER_CALL = re.compile(r"\b(SnapshotXfer|AIXfer|SnapshotXferUtil)\s*::\s*(\w+)\s*\(")
HELPER_NS = re.compile(r"\bnamespace\s+(SnapshotXfer|AIXfer|SnapshotXferUtil)\s*\{")
# the helpers of the snapshot namespaces that transfer integers, each in one kind (a helper with an integer transfer in none of them is a problem):
#   value:   each call is a transfer of its second argument (audited at the call; the helper's own transfer is not a site)
#   checked: the same, and the helper checks the value itself (refuses an out-of-set one)
#   body:    the helper's own transfers are the sites (audited where the helper is written; its calls are not transfers of their argument)
HELPERS = {
    "value": {"SnapshotXfer": {"u64", "words", "ints", "uints", "ids", "stlObjectIDList", "idContainer"}, "AIXfer": {"uns", "u64", "ints", "ids", "idSet"}},
    "checked": {"SnapshotXfer": {"maskWords", "maskWithin", "enumIn", "objectPointers", "containerPointer"}, "AIXfer": {"enumIn", "objectId"}, "SnapshotXferUtil": {"xferBitFlagNames"}},
    "body": {"SnapshotXfer": {"damageInput", "icoord2D"}, "AIXfer": {"icoord2", "reportString"}},
}
CHECK = re.compile(r"\bcheck[A-Z]\w*\(|\bissued\(|enumIn|inRange|maskWords|maskWithin|throw XferException|refuse\(|fail\(")
ID_CHECK = re.compile(r"findObject|objectId|issued|Issued|getNextObjectID|findTeamByID|findTeam|getNthPlayer|findPlayer|validId|knownId|ObjectID")
OPEN_NAME = re.compile(
    r"(frame|Frame|until|Until|deadline|Deadline|expir|Expir|end$|End$|start$|Start$|when|When|time|Time|timer|Timer|countdown|Countdown|"
    r"count|Count|^num|Num|total|Total|value|Value|amount|Amount|money|Money|cost|Cost|points|Points|shots|Shots|hash|Hash|seed|Seed|serial|Serial|"
    r"crc|Crc|stamp|Stamp|delay|Delay|wait|Wait|budget|Budget|spent|Spent|earned|Earned|lost|Lost|built|Built|destroyed|Destroyed|alive|Alive|"
    r"calls|Calls|requests|Requests|retries|Retries|failures|Failures|scans|Scans|pulses|Pulses|heals|Heals|strikes|Strikes|uses|Uses|"
    r"unported|Unported|refused|Refused|given|Given|granted|Granted|made|Made|queued|Queued|executed|Executed|launched|Launched|fired|Fired|"
    r"length|Length|size|Size|width|Width|height|Height|steps|Steps|step$|Step$|history|History|generation|Generation|dummy|magic|Magic|"
    r"score|Score|experience|Experience|skill|Skill|purchase|Purchase|price|Price|surcharge|Surcharge|color|Color|seconds|Seconds|ticks|Ticks)"
)
ID_NAME = re.compile(r"(ID$|Id$|^id$|^ids?$|objectId|Victim$|victim$|Attacker$|Target$|target$|Leader$|Owner$|owner$|Source$|source$|Benefactor$)")
CLOSED_NAME = re.compile(
    r"(mask|Mask|flags|Flags|bits|Bits|type|Type|kind|Kind|state|State|status|Status|mode|Mode|phase|Phase|cond|Cond|stance|Stance|rank|Rank|"
    r"level|Level|side|Side|slot|Slot|index|Index|player|Player|team|Team|which|Which|choice|Choice|direction|Direction|profile|Profile|layer|Layer|"
    r"category|Category|difficulty|Difficulty|rule|Rule|weather|Weather|options|Options|disabled|Disabled|priority|Priority|class|Class|style|Style|"
    r"set$|Set$|cursor|Cursor|barrel|Barrel|segment|Segment|waypoint|Waypoint|task|Task|relationship|Relationship|emotion|Emotion|nugget|Nugget)"
)


def name_of(expr: str) -> str:
    """the last identifier of a transfer's destination expression (m_a.b[3] -> b; *c -> c; (std::int32_t &)v -> v)"""
    expr = re.sub(r"\.(?:data|begin)\(\)\s*$", "", expr)  # a raw transfer of a container's bytes: the container (r.status.data() -> status)
    expr = re.sub(r"\[[^\]]*\]", "", expr)
    expr = re.sub(r"\([^)]*\)", "", expr)
    ids = re.findall(r"[A-Za-z_][A-Za-z_0-9]*", expr)
    return ids[-1] if ids else expr.strip()


def strip_code(text: str) -> str:
    """the text with its comments and string / character literals blanked (same length, newlines kept): a transfer or a check is found in code only"""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c in "\"'" and not (c == "'" and i > 0 and (text[i - 1].isalnum())):  # 1'000 is a digit separator, not a literal
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            for k in range(i + 1, j - 1):
                if out[k] != "\n":
                    out[k] = " "
            i = j
            continue
        else:
            i += 1
            continue
        for k in range(i, j):
            if out[k] != "\n":
                out[k] = " "
        i = j
    return "".join(out)


def call_args(code: str, open_paren: int) -> tuple[list[str], int]:
    """the top-level arguments of the call whose '(' is at open_paren, and the offset just past its ')' (any formatting, any line breaks)"""
    depth, args, cur = 0, [], open_paren + 1
    for i in range(open_paren, len(code)):
        ch = code[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                args.append(code[cur:i])
                return [a.strip() for a in args], i + 1
        elif ch == "," and depth == 1:
            args.append(code[cur:i])
            cur = i + 1
    return [code[open_paren + 1:].strip()], len(code)


def is_definition(code: str, start: int) -> bool:
    """a declaration or definition of the transfer function, not a call: its name follows `::` (XferLoad::xferInt) or a type (void xferInt)"""
    before = code[:start].rstrip()
    if before.endswith("::"):
        return True
    m = re.search(r"([A-Za-z_]\w*)$", before)
    return bool(m) and m.group(1) not in ("return", "else", "do", "case")


def function_of(lines: list[str], i: int) -> str:
    """the enclosing function: the last line at column 0 before i that opens a definition (Class::name( or name()"""
    for j in range(i, -1, -1):
        line = lines[j]
        if line[:1].isalpha() or line[:1] == "_":
            m = re.search(r"([A-Za-z_][\w:~]*)\s*\(", line)
            if m and not line.startswith(("namespace", "struct", "class", "enum", "using", "typedef", "#")):
                return m.group(1)
    return "?"


def namespace_spans(code: str) -> list[tuple[int, int, str]]:
    """the (begin, end, name) of each helper namespace's body (SnapshotXfer, AIXfer, SnapshotXferUtil)"""
    spans = []
    for m in HELPER_NS.finditer(code):
        depth, i = 0, m.end() - 1
        while i < len(code):
            if code[i] == "{":
                depth += 1
            elif code[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        spans.append((m.end(), i, m.group(1)))
    return spans


def helper_kind(ns: str, name: str) -> str | None:
    for kind, table in HELPERS.items():
        if name in table.get(ns, ()):
            return kind
    return None


def scan(root: Path, problems: list[str] | None = None) -> list[dict]:
    """every integer transfer under the root (.cpp and .h): every call of an Xfer's integer transfer (any receiver, any formatting) outside the value /
    checked helpers' own bodies, and every call of a value / checked helper. A helper of the snapshot namespaces with an integer transfer and no kind, a
    `using` of a helper namespace (its calls would not be qualified) or a macro that transfers an integer (its calls would not be seen) is a problem."""
    problems = [] if problems is None else problems
    sites = []
    files = sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.h")))
    for path in files:
        rel = path.relative_to(root).as_posix()
        code = strip_code(path.read_text(encoding="utf-8", errors="replace"))
        lines = code.split("\n")
        starts = [0]
        for line in lines:
            starts.append(starts[-1] + len(line) + 1)

        def line_of(offset: int) -> int:
            lo, hi = 0, len(lines) - 1
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if starts[mid] <= offset:
                    lo = mid
                else:
                    hi = mid - 1
            return lo

        for m in re.finditer(r"\busing\s+(?:namespace\s+)?(SnapshotXfer|AIXfer|SnapshotXferUtil)\b", code):
            problems.append(f"{rel}:{line_of(m.start()) + 1}: a using of {m.group(1)} (its helpers must be called qualified, so the audit sees them)")
        # every continued line of a macro (review r4: the backslash was consumed by the first line, hiding the body)
        for m in re.finditer(r"^[ \t]*#[ \t]*define\b(?:[^\n]*\\\n)*[^\n]*", code, re.M):
            if INT_XFER.search(m.group(0)) or HELPER_CALL.search(m.group(0)):
                problems.append(f"{rel}:{line_of(m.start()) + 1}: a macro that transfers an integer (its uses are not seen by the audit)")
        if rel == "Common/Xfer.h":
            known = set().union(*XFER_METHODS.values())
            for m in re.finditer(r"^\s*(?:virtual\s+)?(?:template\s*<[^>]*>\s*)?[\w:<>]+\s+(\w+)\s*\(", code, re.M):
                if m.group(1) not in known and m.group(1) not in ("Xfer", "XferException"):
                    problems.append(f"{rel}:{line_of(m.start()) + 1}: Xfer::{m.group(1)} is not in closed_sets.XFER_METHODS (integer, counted or none)")
        if XFER_FILES.match(rel):
            continue
        spans = namespace_spans(code)
        found: list[tuple[int, int, str, list[str]]] = []  # (offset, end, helper kind or '', args)
        for m in INT_XFER.finditer(code):
            if is_definition(code, m.start()):
                continue
            span = next((sp for sp in spans if sp[0] <= m.start() < sp[1]), None)
            if span:
                helper = function_of(lines, line_of(m.start()))
                kind = helper_kind(span[2], helper)
                if kind is None:
                    problems.append(f"{rel}:{line_of(m.start()) + 1}: {span[2]}::{helper} transfers an integer and is not in closed_sets.HELPERS "
                                    f"(value, checked or body)")
                    continue
                if kind != "body":
                    continue  # its calls are the sites
            args, after = call_args(code, m.end() - 1)
            found.append((m.start(), after, "ref" if m.group(0).startswith("xferObjectID") else "", args[:1]))
        for m in HELPER_CALL.finditer(code):
            kind = helper_kind(m.group(1), m.group(2))
            if kind not in ("value", "checked") or is_definition(code, m.start()):
                continue
            args, after = call_args(code, m.end() - 1)
            at = VALUE_ARG.get(m.group(2), 1)
            found.append((m.start(), after, "ref" if kind == "value" and m.group(2) in REF_HELPERS else kind, args[at:at + 1]))
        found.sort()
        seen: dict[str, int] = {}
        for offset, after, kind, args in found:
            expr = args[0] if args else ""
            if expr.startswith("&"):
                expr = expr[1:]
            name = name_of(expr)
            i = line_of(offset)
            fn = function_of(lines, i)
            base = f"{rel}::{fn}::{name}"
            seen[base] = seen.get(base, 0) + 1
            end = line_of(after)
            while end < len(lines) and lines[end] != "}":  # the rest of the function (its closing brace at column 0)
                end += 1
            window = code[after:starts[min(end, len(lines))]]  # after the transfer: a check before it (or earlier on its line) does not count
            text = " ".join(code[offset:after].split()) + ";"
            sites.append({"key": f"{base}#{seen[base]}", "file": rel, "line": i + 1, "function": fn, "name": name, "expr": " ".join(expr.split()),
                          "text": text, "window": window, "helper": kind})
    return sites


def classify(site: dict) -> str:
    """by the transfer: an object reference (xferObjectID, an id list helper); else by the name: an id; else a closed word and an open word both (m_maskCount, m_teamCount: which one is meant is not the name's to say) is
    listed (an entry names why); else open; else closed; else listed"""
    if site.get("helper") == "ref":
        return "ref"
    name = site["name"]
    if ID_NAME.search(name):
        return "id"
    is_open, is_closed = bool(OPEN_NAME.search(name)), bool(CLOSED_NAME.search(name))
    if is_open and is_closed:
        return "listed"
    if is_open:
        return "open"
    if is_closed:
        return "closed"
    return "listed"


def statements(window: str) -> list[str]:
    """the window's statements (split at ';', '{' and '}', whitespace folded): a check written over several lines is one statement"""
    return [" ".join(p.split()) for p in re.split(r"[;{}]", window) if p.strip()]


def checked(site: dict, cls: str) -> bool:
    """a check of the loaded value after the transfer in the rest of its function: a statement naming the variable with a check, or an `if` / a loop
    naming it whose next statements throw / check; a checked helper (enumIn, maskWords, maskWithin, objectId, xferBitFlagNames) checks its own value"""
    if site.get("helper") == "checked":
        return True
    name = re.escape(site["name"])
    parts = statements(site["window"])
    for k, part in enumerate(parts):
        if not re.search(r"\b" + name + r"\b", part):
            continue
        if CHECK.search(part) or (cls == "id" and ID_CHECK.search(part)):
            return True
        if re.search(r"\b(?:if|for|while)\s*\(|\|\||&&", part) and any(CHECK.search(p) for p in parts[k + 1:k + 4]):
            return True
    return False


def audit(root: Path, overrides: dict) -> tuple[list[str], dict]:
    problems: list[str] = []
    counts = {"closed": 0, "id": 0, "ref": 0, "open": 0, "listed": 0}
    sites = scan(root, problems)
    if not sites:
        return problems + [f"no integer transfer found under {root} (a wrong root or an empty tree)"], counts
    keys = {s["key"] for s in sites}
    for k, v in overrides.items():
        if k not in keys:
            problems.append(f"stale entry {k}: no such transfer")
        elif not isinstance(v, str) or not re.match(r"^(open: \S|closed: \S|checked by [\w:~]+ in \S+: \S)", v):
            problems.append(f"entry {k}: '{v}' is not 'open: <why>', 'closed: <the check>' or 'checked by <symbol> in <file>: <the check>'")
        else:
            m = re.match(r"^checked by ([\w:~]+) in (\S+): ", v)
            if m:
                f = root / m.group(2)
                sym = m.group(1).split("::")[-1]
                if not f.is_file() or not re.search(r"\b" + re.escape(sym) + r"\b", f.read_text(encoding="utf-8", errors="replace")):
                    problems.append(f"entry {k}: the checking symbol {m.group(1)} is not in {m.group(2)}")
    for s in sites:
        entry = overrides.get(s["key"])
        cls = classify(s)
        if entry is not None:
            cls = "listed"
            counts["listed"] += 1
            if entry.startswith("closed:") and not checked(s, "closed") and not checked(s, "id"):
                problems.append(f"{s['file']}:{s['line']} {s['key']}: listed closed ({entry}) but no check of '{s['name']}' follows")
            continue
        if cls == "listed" and (checked(s, "closed") or checked(s, "id")):
            cls = "closed"  # a transfer of no class whose value is checked: closed by its check
        counts[cls] += 1
        if cls in ("closed", "id") and not checked(s, cls):
            problems.append(f"{s['file']}:{s['line']} {s['key']}: a {cls} transfer of '{s['name']}' without a check ({s['text']})")
        elif cls == "listed":
            problems.append(f"{s['file']}:{s['line']} {s['key']}: '{s['name']}' is of no class and has no entry ({s['text']})")
    return problems, counts


def main(argv=None) -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(here.parent.parent / "engine" / "src"))
    ap.add_argument("--overrides", default=str(here / "closed_sets.json"))
    ap.add_argument("--list", action="store_true", help="print every transfer with its class")
    a = ap.parse_args(argv)
    root = Path(a.root)
    if not root.is_dir():
        print(f"closed_sets: {root} is not a directory")
        return 1
    try:
        overrides = json.loads(Path(a.overrides).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"closed_sets: the overrides file {a.overrides}: {e}")
        return 1
    if a.list:
        for s in scan(root, []):
            print(f"{classify(s):7} {s['file']}:{s['line']} {s['key']}  {s['text']}")
    problems, counts = audit(root, overrides)
    for p in problems:
        print(p)
    total = sum(counts.values())
    print(f"closed_sets: {total} transfers: {counts['closed']} closed, {counts['id']} ids, {counts['ref']} object references, {counts['open']} open, {counts['listed']} listed; "
          f"{len(problems)} problems")
    return 1 if problems or total == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
