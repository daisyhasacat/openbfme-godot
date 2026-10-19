#!/usr/bin/env python3
"""Lane PUBLICCI-1: the pre-push hook that runs the release's privacy gate before ANY git push to the public repository
(Open-BFME/openbfme-godot), whoever pushes and from whatever script (plan 2026-10 section 10: the private privacy scan runs before
anything is pushed public). autorelease.sh audits the sync tree itself before its push; this hook makes the same scan a property of the
repository, so a hand-made `git push origin <branch>` (which would carry the private archive's history) is refused before a byte leaves.

Install it in the operator's checkout (docs/RELEASE.md, "Pushing to the public repository"); never bypass it (`--no-verify`):

    .git/hooks/pre-push:
        #!/bin/sh
        exec python3 "$(git rev-parse --show-toplevel)/tools/release/public_push_guard.py" "$@"

git calls it with the remote's name and URL and one stdin line per ref: `<local ref> <local sha> <remote ref> <remote sha>`.
A push whose URL is not the public repository passes untouched (the private dev repository, the backup). For the public one:
  * the operator's private denylist (OPENBFME_RELEASE_DENYLIST, autorelease.py load_denylist) is required: none, or one that does not
    load, refuses the push;
  * a deletion passes (it publishes nothing);
  * every commit the push would publish (reachable from the pushed object, not from a remote-tracking ref of a public-repository remote
    nor from the ref's current remote value) is audited: its tree (autorelease.privacy_gate: the built-in rules and the denylist, plus
    the .github policy, public_workflows.py) and its raw object text (author, committer, message); an annotated tag's raw text too;
  * more than MAX_NEW such commits is refused unread: the release pushes one sync commit on top of the public main, and a branch of the
    private history would publish thousands (run `git fetch <public remote>` first when the remote-tracking refs are stale);
  * a pushed object that is not a commit or a tag of one is refused.
Exit 0: the push may go; 1: refused (PUBLIC PUSH REFUSED lines); the gate's own lines are sanitized as in a release.

    python3 tools/release/public_push_guard.py <remote name> <url> < refs
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease  # noqa: E402
import public_workflows  # noqa: E402

PUBLIC = re.compile(r"[/:]open-bfme/openbfme-godot(?:\.git)?/?$", re.IGNORECASE)
ZERO = re.compile(r"0{40}")
MAX_NEW = 10


def is_public(url: str) -> bool:
    return bool(PUBLIC.search(url.strip()))


def git(*args: str, ok: bool = False) -> str | None:
    r = subprocess.run(["git", *args], capture_output=True)
    if r.returncode:
        if ok:
            return None
        raise RuntimeError(f"git {' '.join(args[:2])} failed: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout.decode()


def public_tracking_refs() -> list[str]:
    """the remote-tracking refs of every remote whose URL is the public repository: what it already has, as far as this repository knows"""
    refs = []
    for name in (git("remote") or "").split():
        urls = (git("remote", "get-url", "--all", name, ok=True) or "").split()
        if any(is_public(u) for u in urls):
            out = git("for-each-ref", "--format=%(objectname)", f"refs/remotes/{name}/") or ""
            refs += out.split()
    return refs


def check(remote: str, url: str, lines: list[str]) -> list[str]:
    """the refusals of one push (empty: it may go)"""
    if not is_public(url):
        return []
    if all(len(f) == 4 and ZERO.fullmatch(f[1]) for f in map(str.split, filter(str.strip, lines))):
        return []   # deletions only (or nothing): nothing is published
    deny = os.environ.get("OPENBFME_RELEASE_DENYLIST", "")
    if not deny:
        return ["no private denylist (OPENBFME_RELEASE_DENYLIST): a push to the public repository needs the release's privacy gate"]
    try:
        rules = autorelease.load_denylist(Path(deny))
    except SystemExit as e:
        return [f"the private denylist does not load ({e})"]
    known = public_tracking_refs()
    probs: list[str] = []
    texts: list[tuple[str, str]] = []   # (label, raw object text)
    commits: list[str] = []
    for line in lines:
        f = line.split()
        if not f:
            continue
        if len(f) != 4:
            return [f"a ref line git did not write: {line!r}"]
        lref, lsha, rref, rsha = f
        if ZERO.fullmatch(lsha):
            continue
        kind = (git("cat-file", "-t", lsha, ok=True) or "").strip()
        if kind == "tag":
            texts.append((f"the tag object of {rref}", git("cat-file", "tag", lsha)))
        commit = (git("rev-parse", "--verify", "-q", f"{lsha}^{{commit}}", ok=True) or "").strip()
        if kind not in ("commit", "tag") or not commit:
            probs.append(f"{rref}: the pushed object {lsha[:12]} is not a commit or a tag of one")
            continue
        base = list(known)
        if not ZERO.fullmatch(rsha) and git("cat-file", "-e", f"{rsha}^{{commit}}", ok=True) is not None:
            base.append(rsha)
        new = (git("rev-list", commit, "--not", *base) if base else git("rev-list", commit)).split()
        if len(new) > MAX_NEW:
            probs.append(f"{rref}: the push would publish {len(new)} commits the public repository does not have (more than {MAX_NEW}: "
                         "a branch of the private history? fetch the public remote first if its refs are stale)")
            continue
        commits += [c for c in new if c not in commits]
    if probs:
        return probs
    with tempfile.TemporaryDirectory(prefix="public-push-") as tmp:
        files = []
        for c in commits:
            texts.append((f"commit {c[:12]}", git("cat-file", "commit", c)))
        for i, (label, text) in enumerate(texts):
            p = Path(tmp) / f"{i:03d}-{re.sub(r'[^A-Za-z0-9]+', '-', label)}.txt"
            p.write_text(text, encoding="utf-8")
            files.append(p)
        trees = []
        for c in commits:
            t = git("rev-parse", f"{c}^{{tree}}").strip()
            if t not in trees:
                trees.append(t)
        for t in trees:
            for p in public_workflows.tree_problems(None, t):
                probs.append(f"tree {t[:12]}: {p}")
            if autorelease.privacy_gate(None, t, [], rules) != 0:
                probs.append(f"tree {t[:12]} failed the privacy gate (above)")
        if files and autorelease.privacy_gate(None, None, files, rules) != 0:
            probs.append("the pushed commits' or tags' text failed the privacy gate (above)")
    if not probs:
        print(f"PUBLIC PUSH GUARD ok: {len(commits)} new commit(s), {len(trees)} tree(s) audited for {remote}")
    return probs


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: public_push_guard.py <remote name> <url> < refs (git's pre-push hook)", file=sys.stderr)
        return 2
    try:
        probs = check(argv[0], argv[1], sys.stdin.read().splitlines())
    except Exception as e:   # noqa: BLE001 - any failure refuses the push (fail closed), never lets it through
        probs = [f"the guard failed ({type(e).__name__}: {e})"]
    for p in probs:
        print(f"PUBLIC PUSH REFUSED: {p}", file=sys.stderr)
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
