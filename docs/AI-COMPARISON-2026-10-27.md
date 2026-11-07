# AI comparison with Open-BFME-2 — 27 October 2026

Audited OpenBFME-Godot `fc04e42ae75f7e8a5ae77ebc9a04c0c2b0d3bded` against sibling Open-BFME-2 `e190ef5fe4d1467dd46421018d275a64c64b2f63` (`../Open-BFME-2/Code`). This is a source audit and a targeted test run, not a claim of frame-by-frame retail parity. No simulation code was changed for the original audit. Findings describe that baseline; the implementation follow-up below records resolved items.

The implementation is a substantial **partial RotWK AI port plus always-active OpenBFME policies**. All four difficulty IDs exist, but matching difficulty probabilities does not make the whole AI match the donor. The largest differences are missing tactical families, incomplete spell decisions, absent Brutal production discounts, and enhanced decisions which bypass the donor's difficulty-controlled tactic path.

## Evidence and comparison limits

`docs/PLAN.md` makes RotWK 2.01 the target and BFME2 1.06 the second-priority donor. A BFME2/RotWK difference is not automatically a defect. Existing RotWK disassembly carries the repository's S-001 provenance caveat. This audit does not independently resolve that caveat.

Read the local skirmish manager/data, base/economy/unit builder, tactical layer, threat/stat collectors, upgrades, fortification and strategy controllers, special-power framework, difficulty grants, script difficulty selection, save representation and pathfinding/targeting stops. Compared corresponding donor bodies, including `AIDifficulty.cpp`, `AIBuilderUpdate.cpp`, `AITacticsGenerator.cpp`, wall builder, special-power source, AIPlayer and AISkirmishPlayer. Broader per-unit AI gaps are inventoried below from implementation and explicit stops; this is not a line-by-line equivalence proof for every pathfinding, emotion, bird, transport or campaign state.

The installed BFME2 `ini.big` and effective RotWK `_patch201ini.big` were read for difficulty settings; retail data is not copied into the repository. Probabilities below are per eligible decision check, **not per second**. Preconditions, cadence, random draws and script libraries further affect actual behavior.

## Difficulty matrix

UI Medium corresponds to internal `NORMAL`, ID 1.

| Rule | Easy (0) | Medium / Normal (1) | Hard (2) | Brutal (3) |
| --- | --- | --- | --- | --- |
| Farm/economy gate | 1/1050; maximum 3 built plus in-flight economy requests | 10/100; no configured cap | 1/1; no configured cap | 1/1; no configured cap |
| Special-power activation gate | 1/250 | 10/150 | 1/1 | 1/1 |
| Offensive tactic gate | denied in Rush, then 1/500 | 1/1 | 1/1 | 1/1 |
| Fortress included in Rush structure heuristic | excluded | excluded | included | included |
| Base-slot priority boost term | 0 | 75 | 150 | 225 |
| Tactic team move helper without an explicit angle | attack-move | formation-move path | formation-move path | formation-move path |
| Fortress upgrade heuristic interval | `(7-phase)*5` seconds | same | five seconds shorter | five seconds shorter |
| RotWK reserve saving fraction | phase PercentToSave / 3 | / 2 | / 1 | / 1 (difficulty clamped to Hard) |
| Reserve target floor | 0, raised by hero cost | 5,000, raised by hero cost | 10,000, raised by hero cost | 10,000, raised by hero cost |
| Script difficulty flag | easy | normal | hard | hard |
| Parsed Brutal cost/time reduction | none | none | none | 15% / 15%, **not applied by production paths** |

The probability rows match the donor's gate structure: numerator/denominator at least one passes without a draw, otherwise draw `[0, denominator-1]` and compare with numerator. Hard and Brutal use constructor-default tuning rows because the installed data defines only Easy and Normal overrides. No configured farm cap means the default signed `-1` compared unsigned, not zero farms.

Local evidence: `SkirmishAIData.{h,cpp}`, `AIBaseBuilder.cpp::difficultyAllowsFarm`, `updateSlot`, `saveMoney`; `AITacticalAI.cpp::offensiveAllowed`, `enemyStructureHeuristic`, `teamMove`; `AIUpgrade.cpp::evaluateFortress`; `AISpecialPowerUpdate.cpp::update`; `ScriptEngine.cpp::evaluateScript`. Donor gate evidence: `GameEngine/Source/GameLogic/SkirmishAI/AIDifficulty.cpp` (BFME2 RVAs 0x58AEBC, 0x58AF47, 0x58AFB3).

Object difficulty bonuses are a separate mechanism and **are wired**: `Object.cpp::Player::applyDifficultyBonusesForObject` grants the appropriate multiplayer/single-player upgrade; the ordinary upgrade/modifier system applies it. Installed multiplayer damage modifiers are 100% at every difficulty; Easy multiplayer also has the data's 10% experience modifier. Do not invent a Brutal multiplayer damage boost. The installed single-player Brutal modifier references the Hard damage constant despite a separate Brutal constant existing; that is a data fact, not a reason to override it in engine code.

## Findings

### A1 — High: enhanced policies are not isolated from donor behavior or difficulty

`AIBaseBuilder.cpp::updateUnitBuilder` tries strategy counters before breach units, heroes and ordinary composition. `chooseUnit` also maintains siege after Rush even without wall immunity. `update` invokes up to eight direct paid upgrade purchases on the player's once-per-second phase, before the native builders. `AITacticalAI::updateTeamBuilder` invokes `AIFortification::update` and `AIStrategy::update`. These paths have no `SimulationProfile` check and no Easy/Medium probability gate.

Consequences: Easy can perform deterministic local counter-targeting, cavalry pike avoidance, hero/cavalry/flyer recovery, capture selection and fortification orders without passing `offensiveAllowed`. Medium gets the same enhanced controller thresholds as Hard. This does **not** mean every Easy army always attacks: ordinary offensive generation retains its Easy gate. It means that gate no longer governs all commands.

These were previously requested features, documented in `AI-REFERENCE.md` and `AI-STRATEGY.md`; their existence is intentional. Their unconditional integration is an incompatibility with a retail-behavior profile, not grounds to delete them. A future parity lane should select them explicitly in profile identity and test both paths.

### A2 — High: Brutal production cheats are parsed and hashed but unused

`SkirmishAIData.cpp` parses `BuildCostReduction` / `BuildTimeReduction`, and includes them in its data hash. `BuildAssistant.cpp::calcCostToBuild` explicitly omits Brutal reduction (S-204); `calcTimeToBuild` does not consume the cheat fields. `PlayerHeroList.cpp` documents the same omission for hero cost and time. A search of consumers finds parser/hash reads, not production application.

Brutal therefore lacks its configured cost/time advantage for these paths. It still differs from Hard in base priority boost and difficulty-granted upgrade identity. Hard and Brutal sharing several probability rows is expected; the unused reductions are not. Exact order/rounding and hero interactions need the target-specific implementation, not a blanket multiply added to every price. This is already a registered production stop, not newly discovered unreported behavior.

### A3 — High: defensive and strategic expansion families do not execute

`AITacticalAI.cpp::updateChooser` counts DEFENSIVE/EXPANSION heuristic types as unported; `updateBrain` counts their tactics as unported. `AIStatCollector.cpp` omits the spatial position sampler used by defense/cluster decisions. The donor contains `AITargetHeuristicBaseDefense.cpp`, `AITargetHeuristicExpansionBase.cpp`, `AITacticDefensive.cpp`, and `ExpansionTactics/AISimpleExpansionTactic.cpp`; its generator creates defensive and expansion pools.

Effect: ordinary combat reactions and enhanced home-threat micro exist, but do not implement donor defensive team selection, defense target scoring or strategic secondary-base expansion. Farms and fortress-pad expansion purchases **do work** and must not be conflated with missing expansion bases. Affects all difficulties (S-417/S-418).

### A4 — High: native siege and wall construction are incomplete

Local SimpleSiege/SiegeGates bodies are never started through the current applicability/path inference; native siege bodies remain missing. The donor provides `Common/AITacticSiege.cpp`, its idle-siege helper and generator prototypes. `AIWallBuilder` update/library/segments are present in donor source, but the local `AIBaseBuilder::update` omits the wall-builder stage (S-412).

The enhanced breach controller can produce engines, reserve escorts and attack barriers; it does not reproduce these donor families. It also changes passive wall acquisition. User starting walls and legal wall construction are separate from an AI deciding to build a defensive wall plan.

### A5 — High: most targetless behavior is missing or replaced

Local targetless pool has eleven RotWK prototypes, but `targetlessApplies` only implements FarmKillSquad. FlagCaptureSquad, RingHero, StructureCreep, LumberMillBuild, ReturnTheRing, StartWoTRBattle, RoamingDefense, WoTRForfeit, WallTactic and FarmBuilder refuse applicability and increment the unported counter. FarmKillSquad itself selects a nearest farm instead of the donor's full selection/draw logic (S-893).

The donor generator has six targetless prototypes: farm kill, Ring hero, structure creep, return Ring, start WoTR, roaming defense. Corresponding bodies such as `AIStructureCreepTactic.cpp` and `AIRoamingDefenseTactic*.cpp` exist. Enhanced neutral capture is a substitute policy, not FlagCaptureSquad parity. Ring acquisition opportunity scoring is not the full return/recruit Ring hero lifecycle. Missing draws can also change downstream simulation RNG.

### A6 — High: 5 of 53 special-power decisions return no

`AISpecialPowerUpdate.cpp` implements 48 decision cases and records the rest under S-1421. Missing: SOUL_FREEZE, ATTACK_HEAL_AOE, spellbook HEAL, DEBUFFECONOMYBUILDING and DEBUFFPRODUCTIONBUILDING. Summoned-helper steering is also incomplete. Some depend on team registries, enemy selection or spatial cluster records rather than just an isolated cast function.

The donor now has useful sources including `AISpellBookHeal.cpp`, `AISpellBookArmyBreaker.cpp`, `AISpellBookStructureBaseKill.cpp` and power framework/pickers; the local header's statement that no matching Open-BFME-2 source exists is stale. Available donor bodies need individual comparison: not every missing RotWK decision is thereby proven byte-identical to BFME2.

Power readiness, range, random decision gate and difficulty chance are substantially wired. STANCEHOLDGROUND and CAPTURE_BUILDING returning false in the decision switch are explicitly target-derived no-op cases, not counted as the five missing implementations.

### A7 — Medium/high: enhanced recruitment and waves change difficulty pacing

`AITacticalAI.cpp::setupTeam` sets three-unit Simple/Flank structure attacks; `updateTeamBuilder` caps structure teams at twelve and permits handover at eight regardless of target threat. `updateBrain` creates extra 8–12-unit waves when ordinary target slots are saturated. Recruitment age is shortened by the enhanced policy. These affect all difficulty levels; the extra wave does retain `offensiveAllowed`.

Native Formation/Flank/Pincer/Feint/BasePenetration launch/update bodies and threat retreat are present locally. Reports claiming only SimpleAttack exists are stale. Missing native formation-manager refusals/layouts, group speed matching and team-reassignment paths still prevent equivalent coordinated movement (S-895/S-1301/S-418).

### A8 — Medium/high: targeting and threat inputs remain approximate

`TargetFinder.h`, `AIUpdateCombat.cpp` and S-326 describe incomplete retail partition filters/order and target search. A parsed AttackPriority name is not proof that the complete retail priority-table ranking is executed. `AIHunt.cpp` omits its attack-priority info and crate pickup (S-1188). `AIThreatFinder.cpp` fixes an unidentified object rank to constructor value 1 and handles raw ThreatBreakdown with an unknown category left at -1, whereas retail rejects an invalid category (S-1300).

`AIStatCollector` lists/listeners/counts are substantially present, and spatial sampling is now ported; target threat scans and equal-score order have documented inferences. This affects target choices, defensive coverage, abilities and recruitment on every difficulty. Enhanced role targeting further changes the result.

### A9 — Medium: builder update ordering no longer matches the stated assumption

BFME2 `AIBuilderUpdate.cpp::update` runs base, wall, unit, team, economy, unit-upgrader and structure-upgrade stages in that order before pending request processing. Locally the team builder runs after the entire `AIBaseBuilder::update`, after economy, upgrades and processing. The comment justifying this says it draws no random value and touches only tactic teams.

That justification is no longer sufficient: the team-builder entry now calls fortification and strategy policies which issue orders and alter recovery/reservation state. Unit counter selection and `homeThreat` assessment also run earlier. This is a confirmed ordering difference; its exact resulting frame/RNG delta needs an instrumented comparison. Do not assert retail update-order parity from that comment.

### A10 — Medium: object lifecycle, saves, scripts and movement constrain AI parity

Native AI teams are represented locally as tactic structs while objects remain in player teams; priority stealing/defensive reassignment, some event filters and shutdown/group tasks are missing (S-412/S-418/S-890). Unit request completion uses queue disappearance rather than the donor's production event (S-416). Create-a-Hero build-order integration and living-world hooks remain incomplete even though ordinary named heroes can be built.

`SkirmishAISnapshot.cpp` explicitly saves an OpenBFME native block rather than the retail AI xfer layout (S-4220). Deterministic local round trips do not establish retail save/replay compatibility.

`LiveGame.cpp` loads PlayerAIType script libraries; `ScriptEngine.cpp` selects easy/normal/hard flags (Brutal shares hard). Skirmish science purchases can originate in those scripts. S-1875's recorded target analysis says there is no native RotWK skirmish science buyer; `AIUpgradeScienceBuilder` buys building upgrades/expansions, not spellbook sciences. Missing non-skirmish AIPlayer `doUpgradesAndSkills` matters for campaign/mod SkillSets and must not be mislabeled as a universal skirmish science bug.

Ground pathfinding works, but bridge/wall/ladder layers, portal/mineshaft transitions and parts of formation/attack path checks are not complete (S-161–S-167). This affects reachable targets and armies on relevant maps on all difficulties. Naval, aerial, emotion, garrison and scripted team machines require separate exhaustive state-by-state validation; this audit does not certify them by counting files.

### A11 — Medium: status documentation contradicts implementation

`AITacticalAI.h` still says only SimpleAttack and zero-threat behavior exist. Manager stops simultaneously say the money reserve is ported and unported; some say listeners/merge are missing while other clauses document their implementation. `AI-REFERENCE.md` contains historical descriptions predating AI5 upgrades. These are not reliable inventories without checking current consumers. Preserve unresolved stops, but rewrite obsolete claims in a dedicated maintenance change with exact stop tests updated.

## BFME2 differences that should not be “fixed” blindly

- BFME2's source generator lists five offensive and six targetless prototypes; the local ten/eleven pools cite RotWK evidence. Removing RotWK-only prototypes would regress the target.
- RotWK's money saver/reserve is deliberately added beyond the BFME2 builder; its difficulty is clamped to Hard for saving. Do not remove it to make the donor update text look identical.
- RotWK has extra special-power kinds and request checks. Some BFME2 source uses narrower enums, address-derived owner names and inferred views; check the matching function, not names alone.
- Seven-faction RotWK data and mod-provided army/base definitions are expected; content must stay data-driven.

## Validation performed

Command (original game data mounted):

```sh
ROTWK_INSTALL="$PWD/workspace/aio-install/RotWK" \
BFME2_INSTALL="$PWD/workspace/aio-install/BFME2" \
engine/build/openbfme_tests \
  -sf='*test_skirmish_ai_data.cpp,*test_skirmish_ai_manager.cpp,*test_skirmish_ai_u22.cpp,*test_ai_strategy.cpp,*test_ai_fortifications.cpp,*test_aipow1.cpp'
```

74 executed cases: **72 passed, 2 failed**; 1,576 assertions: 1,571 passed, 5 failed. Two failing cases:

1. `offensive AI: research and unit upgrades use paid queues and prerequisites` — four assertions: technology not completed/queued, arrows absent, combined money 1,600 instead of 1,300 (twice). This contradicts a blanket claim that the enhanced upgrader is fully validated. Inspect harness/production/request interactions before assigning a retail cause.
2. `AIPOW-1: end to end: a Hard computer's wounded Aragorn uses Athelas on his own ...` — cast occurred at frame 22, expected 23. The ability fires, but the exact timing pin fails; no retail equivalence can be inferred from this test.

Both failure names were already reported in the preceding handoff's baseline comparison. This audit independently reproduces current failures but did not rebuild a second baseline or change goldens. Binary-oracle cases printed SKIP because `RW_GAME_DAT` was not set; the optional long 2v2 case is disabled. No complete four-difficulty × seven-faction × map/topology external-oracle matrix was run. The passing tests establish local behavior, not equivalence to BFME2 or RotWK.

## Source index

Local files (paths relative to this report):

- [Difficulty data and defaults](../engine/src/GameLogic/SkirmishAI/SkirmishAIData.h), [parsers](../engine/src/GameLogic/SkirmishAI/SkirmishAIData.cpp)
- [Builder, production, reserve and enhanced purchases](../engine/src/GameLogic/SkirmishAI/AIBaseBuilder.cpp), [ordinary cost/time](../engine/src/Common/BuildAssistant.cpp), [hero prices/times](../engine/src/Common/PlayerHeroList.cpp)
- [Tactics, recruitment and difficulty gates](../engine/src/GameLogic/SkirmishAI/AITacticalAI.cpp), [manager/order and stops](../engine/src/GameLogic/SkirmishAI/SkirmishAIManager.cpp)
- [Enhanced strategy](../engine/src/GameLogic/SkirmishAI/AIStrategy.cpp), [fortifications](../engine/src/GameLogic/SkirmishAI/AIFortification.cpp), [native upgrades](../engine/src/GameLogic/SkirmishAI/AIUpgrade.cpp)
- [Threat calculation](../engine/src/GameLogic/SkirmishAI/AIThreatFinder.cpp), [player records](../engine/src/GameLogic/SkirmishAI/AIStatCollector.cpp)
- [Power decisions and stop inventory](../engine/src/GameLogic/Module/AISpecialPowerUpdate.cpp), [decision helpers](../engine/src/GameLogic/Module/AISpecialPowerDecisions.cpp)
- [AI save representation](../engine/src/GameLogic/SkirmishAI/SkirmishAISnapshot.cpp), [pathfinding limitations](../engine/src/GameLogic/AI/AIPathfindStops.h), [target search limitations](../engine/src/GameLogic/Combat/TargetFinder.h)
- [Paid upgrade failing case](../engine/tests/test_ai_fortifications.cpp), [Athelas failing case](../engine/tests/test_aipow1.cpp)

Donor files below `../Open-BFME-2/Code/GameEngine/Source/`:

| Concern | Source |
| --- | --- |
| Difficulty chances and farm cap | `GameLogic/SkirmishAI/AIDifficulty.cpp` |
| Constructor defaults | `GameLogic/SkirmishAI/SkirmishAIDataCtor.cpp` |
| Authoritative builder stages | `GameLogic/SkirmishAI/AIBuilderUpdate.cpp` |
| Tactic pools and draw selection | `GameLogic/SkirmishAI/AITacticalAI/AITacticsGenerator/AITacticsGenerator.cpp` |
| Defense/expansion scoring | `GameLogic/SkirmishAI/AITacticalAI/AITargetChooser/AITargetHeuristics/AITargetHeuristicBaseDefense.cpp`, `AITargetHeuristicExpansionBase.cpp` |
| Defense/expansion execution | `GameLogic/SkirmishAI/AITacticalAI/AITacticsGenerator/AITacticDefensive.cpp`, `ExpansionTactics/AISimpleExpansionTactic.cpp` |
| Targetless execution | `GameLogic/SkirmishAI/AITacticalAI/AITacticsGenerator/TargetlessTactics/` |
| Native wall decisions | `GameLogic/SkirmishAI/AIWallBuilder/AIWallBuilder.cpp`, `AIWallBuilder_canBuildAnyWall.cpp`, `AIWall_init.cpp` |
| Siege | `Common/AITacticSiege.cpp`, `Common/AITacticSiege_sideHasIdleSiegeWeapons.cpp` |
| Power behavior | `GameLogic/SkirmishAI/AISpecialPowers/` |
| Non-skirmish sciences/team production | `GameLogic/AI/AIPlayerTeamBuild_DoUpgradesAndSkills.cpp`, `AIPlayerTeamBuild.cpp` |

## Recommended order of work

1. Separate donor behavior from enhanced policies in immutable simulation profile identity, retaining the requested enhanced features.
2. Add exact Easy/Medium/Hard/Brutal decision-gate, no-draw/draw and production-price/time cases; implement target-correct Brutal discounts. Include fixed seed/RNG position, not just final army size.
3. Resolve the two current failing cases without rewriting their expected values until their provenance is understood.
4. Restore authoritative builder ordering for the donor profile; keep enhanced command ownership deliberate.
5. Port the spatial player record and defensive/expansion families, then siege/wall and targetless families against matching sources.
6. Complete source-supported power decisions and summoned-helper steering; measure script-driven sciences separately.
7. Compare external frame-counted runs across all difficulties, factions, allied groups, water/bridge/fortified maps and save/resume. RNG sequence and native team/event ordering are acceptance gates.

Independent Sol review was attempted as required by `docs/PLAN.md`, but the reviewer failed to start because of its usage limit. This report has not received that independent review and should not be represented as a reviewed implementation lane.

## Implementation follow-up

The production lane now applies the registered Brutal AI's data-driven cost/time reductions, including upper-only clamping, frame-ceiling and the separate hero wrapper discounts. Exact synthetic tests cover all four difficulties, a human with difficulty 3, an unregistered computer, negative/over-100% reductions and hero rounding. This supersedes A2's description of the audited commit; the report above remains the original comparison.

The paid-upgrade failure was a stale fixture: it funded the old `AIBuildState::moneyReserve` while the live policy reads the AIStatCollector reserve. The fixture now supplies the live record and keeps its own detached bot under explicit test control. No product reserve fallback was added.

Builder recruitment now runs between unit and economy builders inside `AIBaseBuilder::update`, as the donor does; the delayed manager call is removed (A9). Sol 6.1 reviewed the discount/ordering/fixture implementation and found no blocking issues. Validation after these changes: 115 targeted AI/production/hero cases, 114 passed, with only the previously reported Athelas frame-23 pin failing (observed frame 22). The expected timing was not rewritten. Build and exact discount/paid-upgrade cases pass.

Outstanding after the first implementation commit: separate enhanced AI feature selection and profile identity, missing tactical/targetless/wall families, spatial records, missing power decisions, and the broader external-oracle matrix. This follow-up does not claim those recommendations are implemented.

Source AI is now the default at the owner's request. Enhanced policies require `OpenBFMEEnhancedAI = yes`: counter production, strategy micro/capture, fortification control, faster recruitment/waves, direct upgrade purchasing, siege quotas and farm-site prechecks are gated. The boolean is part of the game setup, replay consistency checks, profile identity and settings hash, independently of Uber execution mode. Native upgrade/science-building and difficulty gates continue in source mode.

Profile-split validation: build passed; 32 focused rule/strategy/fortification cases passed (571 assertions; retail capture case skipped without data in that run); eight profile/wire/replay/LAN cases passed with socket access; the Godot `uberjoin1_test.gd` lifecycle passed, including explicit AI opt-in/restore identity checks. A wider data-mounted core selection passed 63/64 cases; the Uber battle fixture differed at all 40 hash pins, while cross-thread/repeat checks passed. Those pins were not regenerated: that benchmark uses human slots, so a source/enhanced AI choice alone is not a sufficient explanation for changing them.

The wider 81-case gameplay selection passed 78 and failed three: Athelas timing, the AI-4 reserve/hero scenario and the enlarged-CP Dwarven catapult scenario. Enhanced scenarios now explicitly opt in. Rerunning capture plus the opted-in AI-4 scenario passed capture but still failed AI-4: its old reserveSaved diagnostic remains zero and it reports 76 hero requests against its six-request bound. This is an unresolved integration failure, not a claim of source AI parity. The native saver test and the paid-upgrade fixture pass; no bounds or goldens were loosened. Missing subsystem families and these failures remain outstanding implementation work.

The real game startup/terrain smoke passed with the graphical renderer under source AI rules. Its accelerated headless run failed the five-second fade deadline; that result was retained, not treated as a renderer-independent fade validation.

The reserve execution wrapper now follows donor `AIBuildable_doBuild.cpp` (0x55ADE2): it withdraws only price minus current cash, provided the reserve covers that shortfall. Previously it required and withdrew the full price, making some requests pass affordability but fail execution, and draining reserve unnecessarily when cash already covered the price. Exact regression cases pin partial funding, sufficient cash, and insufficient combined funds. This corrects that wrapper; it does not resolve the broader AI-4 diagnostic/hero-request integration failure above. Target provenance remains subject to S-001.

The player-info spatial sampler is now ported: strict per-member adjacency, first-group bridge merging, expanding bounding spheres, registration order, and full rebuild after the first matching registration is removed. RW 0x8E45EA excludes an AI's OffensiveBuildings template matches; human structures are grouped too. Saves preserve groups, their selection flags and registration order, and reject inconsistent group membership. This supplies the shared dependency for defense targeting and dense-structure powers; their individual missing bodies remain open. S-001 still qualifies the target disassembly.

The structure power family is now ported: STRUCTURE_BREAKER chooses the first largest visible enemy structure group; PREF_WALLS first checks near a busy army unit; BASEKILL first checks for a nearby fortress and avoids wasting a cast elsewhere while an enemy fortress remains; CITADEL preserves the source's two random draws, interpolation and perpendicular displacement. Target facts distinguish DOZER_FACTORY from COMMANDCENTER. The regression test exercises human grouping, near-wall ordering, fortress preference and exact Citadel coordinates/draw sites. This raises supported power decisions to 46 of 53.

Structure-family validation: the focused scenario passes 54 assertions. The complete 16-case power suite passes 15 cases / 1,015 assertions and retains the previously recorded Athelas engine-pin failure (22 frames observed, 23 expected); this lane does not loosen that pin. The target binary fact checks for all four new decision vtables pass with the installed target, subject to S-001.

ARMY_BREAKER and TREE_KILLER are now ported, bringing the supported decision count to 48 of 53. Army Breaker deliberately keeps RotWK's attacked-at threat thresholds instead of BFME2's unit-count test, and its per-player 75–150-frame timer is independent from Assist Battle. Tree Killer shares group selection globally and marks a group before location validation. Complete cached tree groups survive native snapshots, including cleared positions after harvesting; the retail-load difference is explicit in S-4252. Focused power/snapshot regressions pass, as do the 66-case builder/manager/save/spell integration selection (1,955 assertions). A fresh isolated full build is blocked by the existing synthetic MP3 fixture/golden mismatch; a separate focused AI target compiles the same core and requested tests without changing that check or its goldens.

Native defensive targeting and SimpleDefense are now ported. The chooser scans each spatial base group for its first eligible enemy, retains RotWK's SUPPORT inclusion and exclusion mask, selects one group, and applies the 30/101 small-base gate. SimpleDefense bypasses the offensive difficulty gate, uses maxima 1 / 3 / half the army by group size, and launches through the native team framework. Synthetic tests cover all four difficulties, the exact rejected roll 30, large-group sizing, and snapshot restoration (197 assertions). Strategic expansion, native siege/walls and the remaining targetless families are still open.

SimpleDefense's recruitment origin is modeled separately from tactic idle history (TeamTemplateInfo +0x324/+0x328, RW 0x79FD4D). Its main and hero recruit passes use the native stable squared-3D-distance order (RW 0x9A2CA5), including the target's z²+y²+x² SSE operation order. Three retail version-window/hostile-input/AI-validation cases pass 3,689 assertions after updating snapshot offsets for the new timer list.
