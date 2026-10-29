# Upstream sync, 2026-10-29

Merged upstream/main at 87579746eb into the local main branch. Retained local Lua/controller integrations, BFME1 support, wall rules, shell camera/fade fixes and Buckland capture approach behavior while integrating upstream targeting, pathfinding, rendering, campaign UI and online changes.

The combined replay writer uses format 12 and a tagged optional local-rule setup extension. Readers retain upstream layouts and validate legacy fork replay layouts 7–9 as a whole when the upstream layout fails. ScriptEngine and SkirmishAI snapshots use version 5 because the branches reused earlier version numbers for incompatible layouts; incompatible snapshot blocks are refused explicitly.

Validation:

- Native library and test executable build successfully.
- Native regression suite: 4,991 passed, 0 failed; 44 cases excluded, including the five MP3 fixture checks. Retail-dependent cases without their required environment report their own skips.
- Lobby suite: 305 passed.
- Python conflict/tolerance/stop-inventory checks: 94 passed.
- Godot script loading audit: all 174 scripts load without parse errors.
- Buckland outpost capture with installed game data: 194 assertions passed (Boromir and battalion, eight approaches).
- Both main-menu presentation styles passed their shell checks; fade smoothing passed with a 1 Hz decoration budget.
- Required precommit gate passed.

Validation limits: the MP3 reference fixtures cannot be reproduced by this host's LAME 3.100/mpg123 combination: generated streams differ from the pinned hashes. The broader shell integration test returns from in-game Options in three to four frames, exceeding its two-frame timing assertion; both main-menu paths pass. The localhost impairment test failed intermittently during an earlier concurrent run, passed in isolation, and passed in the final complete run.
