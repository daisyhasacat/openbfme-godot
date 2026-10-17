# AI adaptation from Open-BFME-2

The simulation already implements skirmish base construction, economy and unit
production, enemy selection, threat evaluation, team recruitment, offensive
tactics, farm raiding, retreat and reinforcement. Player units share its combat
state machines, idle acquisition, stance guards and attack-move behavior.

This update uses the sibling `../Open-BFME-2` repository as a read-only donor.
RotWK evidence already recorded in the engine remains authoritative where it
establishes a difference.

| Donor source under `Code/GameEngine/Source/GameLogic/` | Adaptation |
| --- | --- |
| `Object/Update/AIUpdateInterfaceGetNextMoodTarget.cpp` | Share status, ability, containment and attack eligibility gates between idle and attack-move; require YES for idle acquisition; honor NOTWHILEATTACKING. Keep stealth restrictions specific to idle scans. Add the container radius to acquisition range. |
| `AI/AIGetAdjustedVisionRange.cpp` | Reuse the existing RotWK guard range calculation for nearby acquisition, including runtime VISION modifiers and computer mood multipliers. |
| `AI/AIClosestEnemySearch.cpp` | Preserve edge-based target range in the broad-phase search even for structures larger than the old 80-unit margin. The grid and object-id tie order remain implementation choices, not proven retail partition parity. |
| `SkirmishAI/AITacticalAI/AITacticsGenerator/AITactic.cpp` | Preserve pending team prototypes through recruitment. The donor separates `m_protos` from created `m_teams`; this engine represents both with `AITacticTeam`, distinguished by `handedOver`. Only created empty teams are discarded. |

Regression coverage is in `engine/tests/test_combat_ai.cpp`. Existing combat,
stance, containment, stealth and skirmish tests exercise the shared paths.
Retail-dependent tests require the original BFME2 and RotWK installations.

This is not full retail AI parity. Remaining gaps include AttackPriority table
ranking, complete partition/fog/line-of-sight acquisition filters, the base
position sampler used by defensive target heuristics, defensive and expansion
tactic families, and most targetless tactics (including capture squads and
roaming defense). Their donor bodies depend on subsystems that are not yet
ported here. Existing simulation stop reports retain those limitations; no
placeholder tactic is substituted for a missing implementation.

Validation for this change:

- Built `openbfme_tests` and the macOS Godot extension with CMake.
- Passed 33 combat AI, horde, hash and pending-team regression cases.
- Passed retail stance tests, offensive tactic tests, deterministic repeat-run
  checks, unfinished-building acquisition, and the seven-faction attack test.
  The latter accepts actual building damage as well as entry into the arrival
  circle, so ranged attacks outside that circle count as attacks.
- Passed the headless Godot `godot/tests/lua_ai_test.gd` integration test with
  the local BFME2 and RotWK installations.

## Second pass: behaviour measured in AI-vs-AI games

Probe tools (not pass/fail tests; they print a log to read):

- `godot/tests/ai_skirmish_probe.gd`: a headless skirmish between computer
  players, logged once per game minute: money, command points, army, buildings,
  every tactic (started or forming, its teams, where they stand, the distance to
  the target), the armies' spread, the build queue (`build=1`), per-unit AI
  state (`inspect=1`) and the buildings left at the end. `walls=0` plays without
  the wall-balance profile.
- `godot/tests/ai_player_units_probe.gd`: a human player that gives no orders
  (an idle army is spawned for it) against a computer player: the human units'
  AI states, idle units with an enemy nearby, the hordes' melee state, and with
  `raid=1` an attack-move into the computer's base.
- `GameWorld` debugging calls used by them: `get_object` now reports the AI
  state, victim, goal, mover (path, waiting, blocked) and vision terms;
  `get_ai_build_report`, `debug_find_path`, `debug_pathfind_cell`,
  `debug_placement`.

Faults found in those logs and fixed (sources read for each):

| Symptom in the probe | Cause | Fix |
| --- | --- | --- |
| Attack teams stood for ten minutes at a lake shore or cliff edge | `AIMover::computePath` gave up when the destination was not a valid cell; retail falls back to `findClosestPath` (Open-BFME-2 `Object/Update/AIUpdateInterface_computePath.cpp`) | The closest-path fallback, with `findPath`'s closest cell standing in for the unported `FindClosestPath` (stop S-166) |
| Hordes chased an unreachable creep lair forever | The approach state never gave up (BFME2 `AI/AIAttackApproachTargetStateOnEnter.cpp`, `updateInternal`) | A victim `CanApproachToTarget` refuses is waited on for 10 seconds, then the attack ends |
| Archer hordes acquired targets 1100+ away | `ShroudManager::templateVision` defaulted the vision bonus rows to 0; with no `VisionBonusTestSegments` the ground reference was 0, so a unit's whole height counted as elevation (BFME2 `Common/Thing/ThingTemplateConstructor.cpp`: 0.5, -0.5, 100, 8) | The constructor's defaults |
| No computer player ever built a hero | The money reserve (stop S-413) was not ported | The saver and the reserve read in the RotWK disassembly: RW 0x8F0502 (every 2 s, PercentToSave / (3 - min(difficulty, 2)) of the money moves into the reserve while it is below max(hero cost, difficulty x 5000) and the player has more than 2 economy structures), RW 0x99E773 / 0x99E79A, the processor's check RW 0x961975 (the reserve deposited for the check of a flagged request) and execute RW 0x96190B (the shortfall drawn when the reserve holds all of it), and the 1000-cost flag RW 0xC88214 |
| A computer player re-requested a hero every second | The reserve check priced a hero with `calcCostToBuild` while `queueCreateUnit` charges the hero list's cost | The check now runs with the reserve deposited, as RW 0x961975 does; the shortfall uses the same price |
| 82 farm requests for 7 farms in 20 minutes | Two of Evendim's farm markers are 70 apart: a farm on one blocks the other; every request failed the overlap test and the marker was retried after one skip | INFERENCE: a marker the economy structure cannot stand on is passed over before a builder is sent |
| Under the wall-balance profile a beaten fortress stood for ever | Only `CAN_ATTACK_WALLS` units may damage a fortress there; the retail unit picks never produce one, and some base layouts hold no structure that trains one | OpenBFME rule awareness: past the Rush phase an AI with none trains one of its army's units of that kind; when no structure of its own trains one, it requests the structure its builders construct that does (Ent Moot, Siege Works, Workshop) at the first legal site on rings around its base |

Results on Evendim, Hard against Hard, 30 minutes, retail rules (`walls=0`):
every economy builder builds every farm it requests, computer players build
heroes, tactics advance and end, and Isengard eliminates Elves in 20 minutes.
Men against Mordor and Dwarves against Wild trade armies without a decision in
30 minutes.

Still not ported (each a reported stop): the DEFENSIVE heuristic RW 0x97D74F
and tactic RW 0x90C436 (they need the player record's position sampler, RW
0x8E3EF8 / 0x8E4456), the EXPANSION tactics, the upgrade / science builder
(RW 0x9ED82A is its reserve-aware check), the wall builder, and bridge / wall
pathfinder layers (S-161; Evendim has no bridges, so its lair island is
unreachable by ground for retail as well).
