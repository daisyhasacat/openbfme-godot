# BFME2 tutorial script audit

Scope: every condition and action occurrence in the mounted BFME2 Basic and Advanced tutorial maps. This is an instruction census, not a claim that either tutorial can yet be completed. No retail script bodies or assets are included.

BFME2 source: the local Open-BFME-2 checkout, revision `73df4683fb677c052c9ad49a331515a78f5196c6`, under `Code/GameEngine/Source/GameLogic/ScriptEngine/`. Resolve the checkout relative to the developer directory; no machine-specific path is required.

## Ports in this change

- `NAMED_SELECTED` and `TYPE_SELECTED`: `ScriptConditions_evaluateTeamStateIs.cpp`, BFME2 1.06 addresses 0x3E9B6A and 0x3E6BC5. Multiplayer returns false. Named selection uses exact object names; type selection uses ObjectTypes membership. We read command-synchronized local-player selection and recompute rather than keeping the drawable selection cache. UI-to-command latency remains an architectural difference.
- `NAMED_ATTACKED_BY_OBJECTTYPE`: the same source file, 0x3E9556. Resolve the most recent damage source in the live world; test its template name against the type set.
- `NAMED_BUILDING_IS_EMPTY`: `ScriptConditions_evaluateIsBuildingEmpty.cpp`, 0x3E4079. Missing object or containment returns false. A valid containment with zero occupants returns true. The BFME2 count call has an extra zero argument whose meaning is unidentified in the source; this runtime exposes the occupant count without that argument.

## Remaining progression work

Selection and camera scrolling alone do not complete these tutorials. Basic still waits on construction and command-button abilities; Advanced additionally waits on stance/submenu, formation, spell-store interaction and transport setup. Unported instructions remain reported by name through S-1180. Recorded presentation requests remain covered by S-1182; a recorded request is not proof that its effect is applied.

Start with the source-backed unit setup actions (`ScriptActions_doStandGround.cpp`, `ScriptActions_doTeamAttackTeam.cpp`), then trace nearest-unit references, construction counting and instant garrison through the BFME2 dispatch. The objectives condition now uses a single-player command bridge and resets when menus close on load. Other UI conditions still require a deliberate client-to-logic event bridge; returning true unconditionally would skip the lesson. Human-impassable areas require pathfinder integration rather than a cosmetic overlay. None of these gaps is silently treated as completed here.

## Instruction inventory

Counts include inactive scripts and all branches. “Existing route” means there is an engine or presentation dispatch path and still requires behavior/parity review. “Gap” means the instruction is not implemented in the current runtime. “Presentation audit” means a client request exists but its complete effect needs verification. The camera-distance and reveal routes retain S-6103 and S-6102 respectively.

### Conditions

| Instruction | Basic | Advanced | Review |
|---|---:|---:|---|
| `BUILT_BY_PLAYER` | 3 | 0 | Gap (S-1180) |
| `CAMERA_SCROLL_DISTANCE` | 2 | 0 | Existing route |
| `CONDITION_TRUE` | 8 | 3 | Existing route |
| `FLAG` | 1 | 4 | Existing route |
| `HAS_FINISHED_AUDIO` | 54 | 30 | Existing route |
| `NAMED_ATTACKED_BY_OBJECTTYPE` | 0 | 1 | Ported in this change |
| `NAMED_BUILDING_IS_EMPTY` | 0 | 7 | Ported in this change |
| `NAMED_DESTROYED` | 11 | 23 | Existing route |
| `NAMED_ENTERED_AREA` | 0 | 3 | Existing route |
| `NAMED_INSIDE_AREA` | 3 | 0 | Existing route |
| `NAMED_OWNED_BY_PLAYER` | 1 | 1 | Existing route |
| `NAMED_SELECTED` | 7 | 2 | Ported in this change |
| `OBJECTIVES_SCREEN_IS_OPEN` | 2 | 0 | Native Objectives.apt |
| `PLAYER_ACQUIRED_SCIENCE` | 0 | 2 | Existing route |
| `PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA` | 6 | 0 | Existing route |
| `PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT` | 12 | 0 | Existing route |
| `PLAYER_HAS_OBJECT_COMPARISON` | 7 | 1 | Existing route |
| `PLAYER_ISSUED_FORMATION_ORDER` | 0 | 1 | Gap (S-1180) |
| `SKIRMISH_PLAYER_HAS_UNITS_IN_AREA` | 8 | 12 | Existing route |
| `SPELL_STORE_IS_OPEN` | 0 | 4 | Gap (S-1180) |
| `TEAM_DESTROYED` | 1 | 3 | Existing route |
| `TIMER_EXPIRED` | 10 | 12 | Existing route |
| `TOGGLE_STANCE_SUBMENU_IS_OPEN` | 0 | 1 | Gap (S-1180) |
| `TYPE_SELECTED` | 1 | 0 | Ported in this change |
| `UNIT_USING_STANCE` | 0 | 1 | Gap (S-1180) |

### Actions

| Instruction | Basic | Advanced | Review |
|---|---:|---:|---|
| `ALLOW_DISALLOW_ONE_BUILDING` | 38 | 4 | Existing route |
| `CAMEO_FLASH` | 11 | 7 | Presentation audit (S-1182) |
| `CAMERA_FADE_MULTIPLY` | 0 | 1 | Existing route |
| `CAMERA_FOLLOW_NAMED` | 0 | 2 | Existing route |
| `CAMERA_LETTERBOX_BEGIN` | 3 | 3 | Existing route |
| `CAMERA_LETTERBOX_END` | 3 | 3 | Existing route |
| `CAMERA_STOP_FOLLOW` | 0 | 1 | Existing route |
| `CLOSE_OBJECTIVES_SCREEN` | 2 | 2 | Existing route |
| `COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE` | 1 | 3 | Presentation audit (S-1182) |
| `CREATE_NAMED_ON_TEAM_AT_OBJECTTYPE` | 2 | 0 | Gap (S-1180) |
| `CREATE_NAMED_ON_TEAM_AT_WAYPOINT` | 13 | 10 | Existing route |
| `CREATE_REINFORCEMENT_TEAM` | 1 | 0 | Existing route |
| `CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL` | 1 | 0 | Gap (S-1180) |
| `DESELECT` | 1 | 0 | Presentation audit (S-1182) |
| `DISABLE_INPUT` | 4 | 4 | Existing route |
| `DISABLE_PLANNING_MODE` | 1 | 1 | Gap (S-1180) |
| `DISABLE_SCRIPT` | 7 | 3 | Existing route |
| `DISABLE_SPELL_STORE` | 1 | 1 | Gap (S-1180) |
| `DISPLAY_NOTIFICATION_BOX` | 71 | 48 | Existing route |
| `ENABLE_INPUT` | 4 | 4 | Existing route |
| `ENABLE_OBJECTIVES_SCREEN` | 1 | 0 | Native Objectives.apt |
| `ENABLE_SCRIPT` | 72 | 44 | Existing route |
| `ENABLE_SPELL_STORE` | 0 | 1 | Gap (S-1180) |
| `FLASH_OBJECTIVES_BUTTON` | 1 | 0 | Presentation audit (S-1182) |
| `FLASH_SPELL_STORE_BUTTON` | 0 | 1 | Gap (S-1180) |
| `GATE_OPEN` | 4 | 0 | Existing route |
| `HIDE_MISSION_OBJECTIVE` | 9 | 7 | Existing route |
| `HUMAN_IMPASSABLE_AREA` | 2 | 8 | Gap (S-1180) |
| `MAP_REVEAL_IN_TRIGGER` | 1 | 0 | Existing route |
| `MAP_REVEAL_PERMANENTLY_AT_WAYPOINT` | 4 | 5 | Existing route |
| `MAP_REVEAL_PERMANENTLY_IN_TRIGGER` | 6 | 4 | Existing route |
| `MARK_MISSION_OBJECTIVE_COMPLETED` | 11 | 8 | Existing route |
| `MOVE_CAMERA_TO` | 4 | 2 | Existing route |
| `MOVE_NAMED_UNIT_TO` | 19 | 16 | Existing route |
| `MOVE_TEAM_TO` | 2 | 1 | Existing route |
| `NAMED_ATTACK_NAMED` | 3 | 1 | Existing route |
| `NAMED_KILL` | 15 | 10 | Existing route |
| `NAMED_RECEIVE_UPGRADE` | 0 | 2 | Existing route |
| `NAMED_TRANSFER_OWNERSHIP_PLAYER` | 7 | 4 | Existing route |
| `NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT` | 6 | 0 | Gap (S-1180) |
| `NO_OP` | 1 | 0 | Existing route |
| `OBJECT_CREATE_RADAR_EVENT` | 0 | 1 | Existing route |
| `OVERRIDE_PLAYER_COMMAND_POINTS` | 1 | 1 | Existing route |
| `PLAYER_ENABLE_BASE_CONSTRUCTION` | 1 | 1 | Existing route |
| `PLAYER_ENABLE_UNIT_CONSTRUCTION` | 1 | 1 | Existing route |
| `PLAYER_RELATES_PLAYER` | 1 | 0 | Existing route |
| `PLAYER_SCIENCE_AVAILABILITY` | 0 | 13 | Existing route |
| `PLAYER_SET_MAX_SPELLPOINTS` | 1 | 1 | Existing route |
| `PLAYER_SET_MONEY` | 1 | 1 | Existing route |
| `PLAYER_SET_RANKLEVEL` | 0 | 1 | Gap (S-1180) |
| `PLAY_SOUND_EFFECT` | 87 | 55 | Existing route |
| `PLAY_SOUND_EFFECT_AT` | 1 | 0 | Existing route |
| `RADAR_CREATE_EVENT` | 2 | 0 | Existing route |
| `RESET_CAMERA` | 1 | 2 | Existing route |
| `SET_FLAG` | 2 | 6 | Existing route |
| `SET_MILLISECOND_TIMER` | 10 | 12 | Existing route |
| `SHOW_MILITARY_CAPTION` | 3 | 7 | Existing route |
| `SHOW_MISSION_OBJECTIVE` | 9 | 7 | Existing route |
| `SOUND_DISABLE_TYPE` | 10 | 7 | Gap (S-1180) |
| `SOUND_PLAY_NAMED` | 2 | 0 | Existing route |
| `TEAM_ATTACK_TEAM` | 1 | 0 | Gap (S-1180) |
| `TEAM_FOLLOW_WAYPOINTS_EXACT` | 0 | 2 | Existing route |
| `TEAM_GARRISON_SPECIFIC_BUILDING_INSTANTLY` | 0 | 4 | Gap (S-1180) |
| `TEAM_HUNT` | 1 | 1 | Existing route |
| `TEAM_SET_ATTITUDE` | 1 | 0 | Existing route |
| `TEAM_SET_PLAYERS_NEAREST_UNIT_OF_TYPE_TO_REFERENCE` | 6 | 1 | Gap (S-1180) |
| `TEAM_TRANSFER_TO_PLAYER` | 2 | 5 | Existing route |
| `UNIT_AFFECT_OBJECT_PANEL_FLAGS` | 28 | 0 | Existing route |
| `UNIT_SET_TEAM` | 0 | 1 | Existing route |
| `UNIT_STAND_GROUND` | 4 | 1 | Gap (S-1180) |

## Validation

The new condition tests pass (3 cases, 57 assertions). Basic and Advanced installed-data launch tests pass, including the existing camera and shroud checks; these do not exercise every lesson through completion.

The native `script*` suite with installed data passes 80 of 81 cases. The existing `script2 client audio` case fails three assertions: it expects missing named speakers to stay silent, while `LiveGameAudio` intentionally plays missing voice speakers without an object. The same three failures reproduce with the pre-change script-engine sources. That audio behavior and its test are unchanged here. Without installed data, all 81 cases pass, with retail cases reporting skips.

## Native objectives menu

`Objectives.apt` now runs through the native APT shell, opened from the Palantir flag or Tab in a campaign/tutorial. The Godot objectives panel remains a map-viewer diagnostic only. BFME2 sources: `AptPlayerStatusScreen.cpp` (0x4E4A45 registration, 0x4E41AB opening), `AptPlayerStatusObjectiveText.cpp` (0x4E4553), `AptPlayerStatusCallbacks.cpp` (0x4E4446), and `AptObjectivesMenu.cpp` (0x4E43F2 visible-row scan). Twelve row providers return `$label` and completed/bonus digits in mission definition order, skipping hidden rows.

The script enable/disable actions gate opening, and the close action closes the native screen. Return to Game and Escape respect scripted input lock in tutorials; campaigns keep the return button enabled, matching BFME2 Rva004E40A6. Campaigns pause while the menu is up; tutorials keep running, matching BFME2's mode-6 exception. The host identifies tutorial sessions through CampaignFlow while its economy mode stays single player. Open/closed presence reaches condition 198 through the enhanced single-player command stream (0x7002, not a retail message ID). It is transient UI state, excluded from snapshot bytes and hashing, and reset on snapshot load/new game; menus close when a game is loaded or restarted. No retail format is changed.

S-1922 now covers the remaining native objectives differences: exact opening transition/script-hold guards and closing animation. This caveat is reported by the native screen and pinned in a test. Tutorial GameData `Objective` presentation and loading voice (S-175) are separate from mission objective rows and remain unported.

Native-menu validation: 102 focused native script, save-block and shell cases pass (2,683 assertions; installed-data-only native cases skip without the games). Basic and Advanced installed-data integration tests cover native opening, actual Resume button pointer input, tutorial input lock, script close, Escape, enable/disable, campaign pause and restart cleanup. The Basic menu was also captured and visually inspected with the graphical renderer. Exact transition/script-hold parity remains S-1922.
