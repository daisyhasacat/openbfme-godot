# Contributor instructions

Follow `docs/PLAN.md` for engineering rules and acceptance gates. Read
`CLAUDE.md` for the repository layout and build guidance.

## macOS app packaging

- Keep the app bundle named `OpenBFME.app` when building or packaging updates.
- Do not add feature names or other suffixes to the `.app` name unless the user
  explicitly requests a different name.

## Commits

- After each completed implementation task, create a new local Git commit
  containing that task's changes and relevant tests and documentation.
- Do not amend an earlier commit unless the user explicitly requests it.
- Stage explicit paths. Preserve unrelated work and existing staged changes;
  do not include them in a task commit without authorization.
- Run the relevant checks and the installed `tools/precommit.py` commit gate.
  Fix gate failures; never bypass the hook or weaken its checks to commit.
- Report the commit hash and validation results in the final response. If a
  commit is blocked, explain the blocker instead of claiming completion.
- Keep commits local unless the user requests a push.
