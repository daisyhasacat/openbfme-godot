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

## Pull request destination

- Submit contribution PRs to `Open-BFME/openbfme-godot` targeting its current `main`.
- Push contribution branches to the personal fork only as the head repository; do not open PRs against the personal fork unless explicitly requested.
- Start upstream contributions from upstream main and port only the requested feature. Exclude unrelated fork features and integration merges; adapt APIs and run relevant checks against the upstream-based branch.
- Check for an existing upstream PR before opening another submission of the same work.
# Agent and contributor rules: game editions

OpenBFME is growing two editions of the game, each with its own rules:

- **BFME2 1.06**: the base. Its rules come from the clean BFME2 1.06
  `game.dat` (md5 `34391b253b0ee12d6cec1178f46d3bf9`); the Open-BFME-2
  decompilation is the reading aid, not the authority.
- **RotWK 2.01**: BFME2 plus its differences. Its rules come from the RotWK
  2.01 `game.dat` (cited as `RW 0x...`).

Today every game runs RotWK's rules. The BFME2 edition exists only behind a
developer switch (`OPENBFME_EDITION=bfme2`, see `docs/EDITIONS.md`). BFME1
keeps its current mode (RotWK's rules on BFME1 data, stop S-5017).

## Rules for every change

1. **State the editions.** Every simulation change says in its commit /
   pull request / report which editions it affects ("RotWK only", "both",
   "BFME2 only").
2. **Cite both sources where the games differ.** A function whose behaviour
   differs between the editions cites the RotWK address (`RW 0x...`) and the
   BFME2 one (the BFME2 1.06 `game.dat` address, `BFME2 0x...`, or the
   decomp's file / `Rva` row). `tools/re/rw2decomp.py` and
   `tools/re/pair_diff.py` find the pair; `tools/re/edition_census.py` lists
   which functions differ.
3. **Never fall back silently.** A rule that one edition has and the other
   lacks is a named error, an `unverified` report line or a registered stop
   (`docs/STOPS.md`), never the other edition's rule used quietly.
4. **Per-system variants, not scattered checks.** Edition differences live
   in one variant per system (AI, hordes, castles, War of the Ring, ...)
   reached through one edition accessor, not in per-function `if` tests on
   strings or install paths spread through the code.
5. **Mods declare their edition.** A mod is loaded for the edition it names.

Everything else (sources, numerics, tests, the commit gate, privacy) is in
`CLAUDE.md` and `docs/PLAN.md`.
