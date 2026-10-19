#!/usr/bin/env python3
"""Lane GHPR-1: the policy for the public repository's GitHub folder (.github/), checked on the tree a release syncs (autorelease.sh,
before anything is pushed; --plan too) and on every pull request (.github/workflows/pr.yml).

A community pull request can change .github/ like any other file, and a sync carries what the archive holds, so a workflow that could reach
a secret, write to the repository or run on a private machine must never get there. The rules (a hit is a failure; nothing is skipped):
  * .github/ holds only APPROVED files: the pull request template and workflows (.github/workflows/<name>.yml); anything else there
    (other templates, dependabot, funding, CODEOWNERS ...) is refused until it is reviewed and added to APPROVED;
  * a workflow parses as YAML (PyYAML's safe loader) into a mapping: GitHub refuses a file it cannot parse and runs none of its jobs
    (lane PUBLICCI-1: a plain `run:` value holding ": " made every run of pr.yml fail as a "workflow file issue" while the text rules
    below passed it); without PyYAML the check fails, it is never skipped;
  * a workflow runs on `pull_request` / `merge_group` / `push` / `workflow_dispatch` only: never `pull_request_target`, `workflow_run` or
    `issue_comment` (they run with the base repository's token and secrets on a contributor's code; `merge_group` runs the merge queue's
    commit of base-repository code, like `push`);
  * no `secrets.` reference at all (GITHUB_TOKEN included), no `id-token`, no `: write` permission anywhere, and a top-level
    `permissions:` block that is exactly `contents: read`;
  * every `uses:` names an action by its full 40-hex commit sha (a tag can be moved) or a local `./` path;
  * GitHub-hosted runners only (`runs-on: ubuntu-*`, `windows-*`, `macos-*`): a `self-hosted` label or any other runner name is refused (it
    would also name private machines).
The checks read the YAML as text, line by line, and are deliberately stricter than YAML: a construct they cannot read (a flow-style
`on: [..]` with a refused trigger, a `uses:` in a form they do not know) fails.

    python3 tools/release/public_workflows.py [--git-dir DIR] --tree TREE    (exit 1 and PUBLIC WORKFLOWS lines on a problem)
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys

APPROVED = (re.compile(r"\.github/pull_request_template\.md"), re.compile(r"\.github/workflows/[a-z0-9][a-z0-9_-]*\.yml"))
REFUSED_TRIGGERS = ("pull_request_target", "workflow_run", "issue_comment")
ALLOWED_TRIGGERS = ("pull_request", "merge_group", "push", "workflow_dispatch")
USES = re.compile(r"^\s*(?:-\s+)?uses:\s*(\S+)\s*(?:#.*)?$")
PINNED = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+@[0-9a-f]{40}")
RUNS_ON = re.compile(r"^\s*runs-on:\s*(.*?)\s*(?:#.*)?$")
HOSTED = re.compile(r"(?:ubuntu|windows|macos)-[a-z0-9.-]+")


def yaml_problems(path: str, text: str) -> list[str]:
    """the file must load as a YAML mapping; PyYAML missing is a failure (fail closed), not a skip"""
    try:
        import yaml
    except ImportError:
        return [f"{path}: PyYAML is not installed, so the file cannot be checked to parse as YAML (pip install PyYAML)"]
    try:
        doc = yaml.safe_load(text)
    except yaml.YAMLError as e:
        mark = getattr(e, "problem_mark", None)
        where = f":{mark.line + 1}:{mark.column + 1}" if mark else ""
        what = getattr(e, "problem", None) or str(e).splitlines()[0]
        return [f"{path}{where}: not valid YAML ({what}): GitHub would refuse the whole workflow"]
    if not isinstance(doc, dict):
        return [f"{path}: the YAML is not a mapping (a workflow is one)"]
    return []


def workflow_problems(path: str, text: str) -> list[str]:
    probs = yaml_problems(path, text)
    lines = text.splitlines()
    for n, line in enumerate(lines, 1):
        code = line   # comments included: a refused word in a comment fails too (stricter than YAML, never looser)
        low = code.lower()
        for t in REFUSED_TRIGGERS:
            if re.search(rf"(?<![a-z_]){t}(?![a-z_])", low):
                probs.append(f"{path}:{n}: the trigger {t} (it runs with the base repository's token and secrets)")
        if "secrets." in low or "secrets[" in low:
            probs.append(f"{path}:{n}: a secrets reference")
        if "id-token" in low:
            probs.append(f"{path}:{n}: an id-token permission")
        if re.search(r":\s*write\b", low) or re.search(r"\bwrite-all\b", low):
            probs.append(f"{path}:{n}: a write permission")
        if "self-hosted" in low:
            probs.append(f"{path}:{n}: a self-hosted runner")
        if re.match(r"^\s*(?:-\s+)?uses:", code):
            m = USES.match(code)
            if not m or not (m.group(1).startswith("./") or PINNED.fullmatch(m.group(1))):
                probs.append(f"{path}:{n}: an action not pinned by its full commit sha")
        m = RUNS_ON.match(code)
        if m and not HOSTED.fullmatch(m.group(1).strip("'\"")):
            probs.append(f"{path}:{n}: runs-on is not one GitHub-hosted runner (ubuntu-*, windows-*, macos-*)")
    # the triggers: the top-level `on:` block's keys (block style), or a refusal
    on = [i for i, line in enumerate(lines) if re.match(r"^(?:on|\"on\"|'on'):", line)]
    if len(on) != 1:
        probs.append(f"{path}: no single top-level `on:` block")
    else:
        rest = lines[on[0]].split(":", 1)[1].split("#", 1)[0].strip()
        if rest:
            probs.append(f"{path}:{on[0] + 1}: `on:` is not written as a block (one trigger per line)")
        for line in lines[on[0] + 1:]:
            if line and not line.startswith(" "):
                break
            m = re.match(r"^  ([A-Za-z_]+):", line)
            if m and m.group(1) not in ALLOWED_TRIGGERS:
                probs.append(f"{path}: the trigger {m.group(1)} is not one of {', '.join(ALLOWED_TRIGGERS)}")
    # the top-level permissions: exactly `contents: read`
    perm = [i for i, line in enumerate(lines) if line.startswith("permissions:")]
    block = []
    if len(perm) == 1:
        for line in lines[perm[0] + 1:]:
            if line and not line.startswith(" "):
                break
            if line.split("#", 1)[0].strip():
                block.append(line.split("#", 1)[0].strip())
    if len(perm) != 1 or lines[perm[0]].split("#", 1)[0].strip() != "permissions:" or block != ["contents: read"]:
        probs.append(f"{path}: the top-level permissions are not exactly `contents: read`")
    return probs


def tree_problems(git_dir: str | None, tree: str) -> list[str]:
    g = ["git"] + (["-C", git_dir] if git_dir else [])
    r = subprocess.run(g + ["ls-tree", "-r", "-z", "--full-tree", tree, "--", ".github"], capture_output=True)
    if r.returncode:
        return [f"cannot list the tree {tree}: {r.stderr.decode(errors='replace').strip()}"]
    probs = []
    for e in filter(None, r.stdout.decode().split("\0")):
        meta, path = e.split("\t", 1)
        mode, kind, oid = meta.split()
        if kind != "blob" or mode not in ("100644", "100755") or not any(a.fullmatch(path) for a in APPROVED):
            probs.append(f"{path}: not an approved file of the public GitHub folder (public_workflows.py APPROVED)")
            continue
        if path.startswith(".github/workflows/"):
            data = subprocess.run(g + ["cat-file", "blob", oid], capture_output=True, check=True).stdout
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                probs.append(f"{path}: not UTF-8 text")
                continue
            probs += workflow_problems(path, text)
    return probs


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    ap.add_argument("--tree", required=True)
    a = ap.parse_args(argv)
    probs = tree_problems(a.git_dir, a.tree)
    for p in probs:
        print(f"PUBLIC WORKFLOWS: {p}")
    print(f"PUBLIC WORKFLOWS {'FAILED' if probs else 'ok'}: {a.tree}")
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
