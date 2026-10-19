"""Shared pytest set-up for the release tools' tests.

The tests create throwaway repositories with `git init` (some `--bare`). Run with GIT_DIR or a sibling variable exported
(inside a git hook, `git bisect run`, `git rebase --exec`), git applies those commands to the repository the variable
names instead of the temporary folder: on 2026-10-19 a run in a lane worktree turned the coordinator's real repository
bare (core.bare=true in the shared .git/config). Every test runs without git's repository-locating variables."""
import pytest

GIT_LOCATION_VARS = ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY",
                     "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE", "GIT_PREFIX", "GIT_CEILING_DIRECTORIES")


@pytest.fixture(autouse=True)
def _no_inherited_git_location(monkeypatch):
    for var in GIT_LOCATION_VARS:
        monkeypatch.delenv(var, raising=False)
