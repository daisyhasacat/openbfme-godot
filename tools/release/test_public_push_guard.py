"""Lane PUBLICCI-1: public_push_guard.py driven the way git drives it: a real `git push` runs it as the pre-push hook. The "public
repository" is a local bare repository at a path ending in open-bfme/openbfme-godot.git (the guard matches the URL's path, any host);
the denylist is test_autorelease's fictional one."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import public_push_guard  # noqa: E402
from test_autorelease import GIT_ENV, TEST_DENYLIST  # noqa: E402

# the fictional names come from test_autorelease's denylist and the addresses are built at run time: this file is copied into the test
# archives and audited there (only test_autorelease.py may spell them)
HOST, HOST2 = TEST_DENYLIST["rules"][0]["pattern"]
RUNBOOK = "docs/" + "runbook" + "-restart.md"
AT = chr(64)
# a noreply identity like the release's (the built-in rules refuse any other e-mail address in public text)
IDENT = {"GIT_AUTHOR_NAME": "Rel", "GIT_AUTHOR_EMAIL": f"1+rel{AT}users.noreply.github.com",
         "GIT_COMMITTER_NAME": "Rel", "GIT_COMMITTER_EMAIL": f"1+rel{AT}users.noreply.github.com"}


class Repos:
    def __init__(self, tmp: Path):
        self.tmp = tmp
        self.public = tmp / "gh" / "open-bfme" / "openbfme-godot.git"
        self.backup = tmp / "backup" / "openbfme-godot.git"   # same repository name, another owner: not public
        for bare in (self.public, self.backup):
            bare.parent.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(bare)], check=True)
        self.work = tmp / "work"
        self.git("init", "-q", "-b", "main", str(self.work), cwd=tmp)
        self.hooks = tmp / "hooks"
        self.hooks.mkdir()
        hook = self.hooks / "pre-push"
        hook.write_text(f"#!/bin/sh\nexec {sys.executable} {HERE / 'public_push_guard.py'} \"$@\"\n")
        hook.chmod(0o755)
        self.deny = tmp / "denylist.json"
        self.deny.write_text(json.dumps(TEST_DENYLIST))
        self.git("remote", "add", "origin", str(self.public))
        self.git("remote", "add", "backup", str(self.backup))
        # the public main every test starts from, pushed past the hook (the hook is only on the test's own pushes)
        self.commit({"README.md": "public\n"}, "public start")
        self.git("push", "-q", "origin", "main")
        self.git("fetch", "-q", "origin")

    def git(self, *args: str, cwd: Path | None = None, env: dict | None = None) -> subprocess.CompletedProcess:
        r = subprocess.run(["git", *args], cwd=cwd or self.work, capture_output=True, text=True,
                           env={**os.environ, **GIT_ENV, **IDENT, **(env or {})})
        assert r.returncode == 0, r.stderr
        return r

    def commit(self, files: dict[str, str], message: str, env: dict | None = None) -> str:
        for name, data in files.items():
            (self.work / name).parent.mkdir(parents=True, exist_ok=True)
            (self.work / name).write_text(data)
        self.git("add", *files)
        self.git("commit", "-q", "-m", message, env=env)
        return self.git("rev-parse", "HEAD").stdout.strip()

    def push(self, *args: str, deny: str | None = "default") -> subprocess.CompletedProcess:
        env = {**os.environ, **GIT_ENV, **IDENT}
        env.pop("OPENBFME_RELEASE_DENYLIST", None)
        if deny is not None:
            env["OPENBFME_RELEASE_DENYLIST"] = str(self.deny) if deny == "default" else deny
        return subprocess.run(["git", "-c", f"core.hooksPath={self.hooks}", "push", *args], cwd=self.work, capture_output=True,
                              text=True, env=env, timeout=600)

    def remote_ref(self, bare: Path, ref: str) -> str:
        r = subprocess.run(["git", "-C", str(bare), "rev-parse", "-q", "--verify", ref], capture_output=True, text=True)
        return r.stdout.strip()


@pytest.fixture
def repos(tmp_path) -> Repos:
    return Repos(tmp_path)


def refused(r: subprocess.CompletedProcess, why: str) -> bool:
    return r.returncode != 0 and f"PUBLIC PUSH REFUSED: {why}" in r.stderr


def test_the_url_match():
    for u in ("https://github.com/Open-BFME/openbfme-godot.git", "https://github.com/open-bfme/openbfme-godot", f"git{AT}github.com:Open-BFME/openbfme-godot.git",
              f"ssh://git{AT}github.com/Open-BFME/openbfme-godot.git/", f"https://x-access-token{AT}github.com/Open-BFME/openbfme-godot"):
        assert public_push_guard.is_public(u), u
    for u in ("https://github.com/Open-BFME/openbfme-dev.git", "https://git.example.invalid/another-owner/openbfme-godot.git",
              "https://github.com/Open-BFME/openbfme-godot-extra.git", "https://github.com/someone/openbfme-godot.git"):
        assert not public_push_guard.is_public(u), u


def test_a_push_elsewhere_passes_untouched_without_a_denylist(repos):
    c = repos.commit({"notes.txt": f"{HOST} is ours\n"}, "private work")
    r = repos.push("backup", "main", deny=None)
    assert r.returncode == 0 and "PUBLIC PUSH" not in r.stderr, r.stderr
    assert repos.remote_ref(repos.backup, "main") == c


def test_without_a_denylist_or_with_a_broken_one_nothing_goes_public(repos):
    before = repos.remote_ref(repos.public, "main")
    repos.commit({"engine/a.cpp": "1\n"}, "a clean change")
    r = repos.push("origin", "main", deny=None)
    assert refused(r, "no private denylist (OPENBFME_RELEASE_DENYLIST)"), r.stderr
    for bad in ("{", json.dumps({"rules": []}), json.dumps({"other": 1})):
        repos.deny.write_text(bad)
        r = repos.push("origin", "main")
        assert refused(r, "the private denylist does not load"), (bad, r.stderr)
    r = repos.push("origin", "main", deny=str(repos.tmp / "missing.json"))
    assert refused(r, "the private denylist does not load"), r.stderr
    assert repos.remote_ref(repos.public, "main") == before


def test_a_clean_sync_shaped_commit_goes_and_a_deletion_goes(repos):
    c = repos.commit({"engine/a.cpp": "1\n"}, "Sync the rebuild: a clean change")
    r = repos.push("origin", f"{c}:refs/heads/sync/x")
    assert r.returncode == 0 and "PUBLIC PUSH GUARD ok: 1 new commit(s), 1 tree(s) audited" in r.stdout + r.stderr, r.stdout + r.stderr
    assert repos.remote_ref(repos.public, "sync/x") == c
    r = repos.push("origin", "--delete", "sync/x", deny=None)   # a deletion publishes nothing: no denylist needed, nothing audited
    assert r.returncode == 0, r.stderr
    assert repos.remote_ref(repos.public, "sync/x") == ""


@pytest.mark.parametrize("files,message,env,why", [
    ({"engine/b.cpp": f"// built on {HOST}\n"}, "a change", None, "tree "),
    ({RUNBOOK: "steps\n"}, "a change", None, "tree "),
    ({"engine/b.cpp": "2\n"}, f"a change tested on {HOST2}", None, "the pushed commits' or tags' text failed the privacy gate"),
    ({"engine/b.cpp": "2\n"}, "a change", {"GIT_AUTHOR_EMAIL": f"dev{AT}mail.example.org"}, "the pushed commits' or tags' text failed"),
    ({".github/workflows/x.yml": "on:\n  pull_request_target:\npermissions:\n  contents: read\njobs: {}\n"}, "a change", None,
     "tree "),
    ({".github/workflows/x.yml": "on:\n  push:\npermissions:\n  contents: read\njobs:\n  a:\n    run: x: y\n"}, "a change", None, "tree "),
])
def test_each_privacy_hit_refuses_the_push(repos, files, message, env, why):
    before = repos.remote_ref(repos.public, "main")
    repos.commit(files, message, env=env)
    r = repos.push("origin", "main")
    assert refused(r, why), r.stdout + r.stderr
    assert repos.remote_ref(repos.public, "main") == before


def test_a_hit_in_an_earlier_commit_of_the_push_refuses_it(repos):
    repos.commit({"engine/b.cpp": f"// {HOST}\n"}, "one")
    repos.commit({"engine/b.cpp": "clean\n"}, "two: the tip is clean, the history is not")
    r = repos.push("origin", "main")
    assert refused(r, "tree "), r.stderr


def test_a_branch_of_the_private_history_is_refused_unread(repos):
    for i in range(public_push_guard.MAX_NEW + 1):
        repos.commit({"engine/a.cpp": f"{i}\n"}, f"history {i}")
    r = repos.push("origin", "main:refs/heads/lane")
    assert refused(r, f"refs/heads/lane: the push would publish {public_push_guard.MAX_NEW + 1} commits"), r.stderr
    assert repos.remote_ref(repos.public, "lane") == ""


def test_stale_tracking_refs_count_against_the_push(repos):
    """the guard knows what is public only from the public remote's tracking refs: without them everything counts as new"""
    for i in range(public_push_guard.MAX_NEW):
        repos.commit({"engine/a.cpp": f"{i}\n"}, f"c{i}")
    repos.git("update-ref", "-d", "refs/remotes/origin/main")
    r = repos.push("origin", "main:refs/heads/x")
    assert r.returncode != 0 and "the push would publish 11 commits the public repository does not have" in r.stderr, r.stderr


def test_tags_their_text_is_audited(repos):
    pub = repos.remote_ref(repos.public, "main")
    repos.git("tag", "-a", "v9", "-m", "OpenBFME v9", pub)
    r = repos.push("origin", "refs/tags/v9")
    assert r.returncode == 0 and "PUBLIC PUSH GUARD ok: 0 new commit(s)" in r.stdout + r.stderr, r.stderr
    repos.git("tag", "-a", "v10", "-m", f"built on {HOST}", pub)
    r = repos.push("origin", "refs/tags/v10")
    assert refused(r, "the pushed commits' or tags' text failed the privacy gate"), r.stderr
    assert repos.remote_ref(repos.public, "refs/tags/v10") == ""


def test_a_pushed_tree_or_blob_and_a_malformed_ref_line_are_refused(repos, monkeypatch, capsys):
    monkeypatch.setenv("OPENBFME_RELEASE_DENYLIST", str(repos.deny))
    monkeypatch.chdir(repos.work)
    blob = repos.git("hash-object", "-w", "README.md").stdout.strip()
    probs = public_push_guard.check("origin", str(repos.public), [f"x {blob} refs/heads/b {'0' * 40}"])
    assert probs == [f"refs/heads/b: the pushed object {blob[:12]} is not a commit or a tag of one"]
    probs = public_push_guard.check("origin", str(repos.public), ["refs/heads/main abc"])
    assert len(probs) == 1 and probs[0].startswith("a ref line git did not write"), probs


def test_a_crash_inside_the_guard_refuses(repos, monkeypatch, capsys):
    monkeypatch.setenv("OPENBFME_RELEASE_DENYLIST", str(repos.deny))
    monkeypatch.chdir(repos.tmp)   # not a repository: every git call fails
    monkeypatch.setattr(sys, "stdin", type("S", (), {"read": staticmethod(lambda: f"refs/heads/main {'1' * 40} refs/heads/main {'0' * 40}\n")})())
    assert public_push_guard.main(["origin", str(repos.public)]) == 1
    assert "PUBLIC PUSH REFUSED:" in capsys.readouterr().err
