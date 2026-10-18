#!/usr/bin/env python3
"""Lane GHPR-1: the public pull request check's audit of the tree a pull request would make (.github/workflows/pr.yml): the release sync's
privacy audit (autorelease.audit_tree: the public top level, no private folders, nothing a .gitignore rule ignores, the commit gate's
rules on every file, containers, binaries) with its BUILT-IN rules only (a public runner has no private denylist: this is not a
publishing check, the release audits again with the operator's list), plus the .github policy (public_workflows.py).

Fails closed: a tree without the public tree's core entries (an empty or truncated checkout) is refused, as is a failing git call.

    python3 tools/release/pr_audit.py [--git-dir DIR] [--tree TREE]    (TREE: HEAD^{tree} by default)
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease  # noqa: E402
import public_workflows  # noqa: E402

# what every public tree holds: a checkout without one of these is not the repository (empty, shallow-broken or truncated)
CORE = ("README.md", "LICENSE", "CONTRIBUTING.md", ".github", "docs", "engine", "godot", "launcher", "tools")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    ap.add_argument("--tree", default="HEAD^{tree}")
    a = ap.parse_args(argv)
    g = ["git"] + (["-C", a.git_dir] if a.git_dir else [])
    r = subprocess.run(g + ["rev-parse", "--verify", "-q", f"{a.tree}^{{tree}}"], capture_output=True, text=True)
    if r.returncode:
        print(f"PR AUDIT FAILED: {a.tree} is not a tree here")
        return 1
    tree = r.stdout.strip()
    top = set(subprocess.run(g + ["ls-tree", "--name-only", "-z", tree], capture_output=True, check=True).stdout.decode().split("\0")) - {""}
    probs = [f"the tree has no {c} (not a whole checkout of the public repository)" for c in CORE if c not in top]
    if not probs:
        probs += autorelease.audit_tree(a.git_dir, tree) + public_workflows.tree_problems(a.git_dir, tree)
    for p in probs:
        print(f"PR AUDIT: {p}")
    n = len(subprocess.run(g + ["ls-tree", "-r", "-z", "--name-only", tree], capture_output=True, check=True).stdout.split(b"\0")) - 1
    print(f"PR AUDIT {'FAILED' if probs else 'ok'}: {tree} ({n} files; the built-in rules only, not a publishing check)")
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
