# BFME1 support

BFME1 is an experimental, separate game mode. It uses a BFME1 installation for
both menus and gameplay, without mixing in BFME2 or RotWK data.

Build the extension normally, then launch:

```sh
BFME1_INSTALL="/path/to/BFME1" godot --path godot -- --game=bfme1
```

Alternatively pass `--bfme1-install=/path/to/BFME1` alongside `--game=bfme1`.
The installation must contain its BIG archives, including `apt` and `lang`.
Installed patch archives are included in BFME1 archive order; launcher-generated
`!!!` graphics overrides are excluded and named in the mount report (`excluded_archives`). This mode fingerprints the installed
archives for content identity, but does not certify them against a retail 1.03
manifest. The existing RotWK verification policy is unchanged.

The launcher offers **Battle for Middle-earth I** in the Play screen's game selector
and passes the asset folder when starting BFME1 (`BFME1_INSTALL`, a remembered
`BFME1_INSTALL=` in the launcher's config, `<games folder>/BFME1`, or an existing
macOS AIO installation). The command-line equivalent is `--game=bfme1 --play` on
the launcher; the selection persists. With **Battle for Middle-earth I** chosen,
**Game files** offers an OPTIONAL download of BFME1 1.03 (the `original-BFME1`
package, from BFME Arena / bfmeladder.com, a third party: OpenBFME does not host
it); it goes into `<folder>/BFME1` and is found from then on
(docs/AIO.md, "BFME1 (optional)").
The main menu's **Games / Mods** link also switches between BFME1 and
RotWK/BFME II. Choose the game and press **Restart to apply**. **BFME1 files…**
selects an existing installation if it was not detected. The last selection is
remembered in `user://game-selection.cfg`; an explicit `--game=...` takes
precedence. RotWK mod choices are retained while BFME1 runs, and BFME1 never
mounts those mods. Switching back restores the RotWK menu and its profiles.

BFME1 startup skips Create-a-Hero initialization and the RotWK campaign overlay;
its own menu handles unsupported campaign actions through the existing refusal path.
The complete startup regression is `godot/tests/bfme1_startup_test.gd` (run with
`BFME1_INSTALL` set and `-- --game=bfme1`).

BFME1 has separate `bfme1-Options.ini` and `bfme1-Skirmish.ini` settings.
`--game=bfme1` and `--bfme1-install=...` are normal player options, so profiles persist between launches.
Automation flags such as `--auto` still use an isolated temporary profile file.
Its archive fingerprint cache is `bfme1-archive-cache.cfg` in the user data folder.

## Source and implementation

The donor checkout is `../Open-BFME-1` (inspected at `77b050212e`). Relevant sources:

- `game/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem_loadBigFiles.cpp`:
  descending archive order, first archive wins.
- `game/GameEngine/Source/GameClient/GameText.cpp`, `parseCSF` at `0x00437570`:
  complemented UTF-16 strings, first string per label, optional voice records.
- `game/GameEngine/Source/GameClient/GUI/AptMainMenuConstructor.cpp` and
  `BfmeAptScreenMainMenuProvider.cpp`: BFME1 menu commands and providers.
- `game/GameEngine/Source/Common/Thing/ThingTemplate.cpp` and
  `GameClient/Drawable/Behavior/RandomSoundSelectorFieldParse.cpp`:
  legacy object and voice fields.
- `game/GameEngine/Source/Common/INI/INIAudioSettings.cpp` and
  `GameClient/Eva.cpp`: BFME1 audio settings and predefined EVA names.

The shared engine reads BFME1's subsystem legend and omits expansion-only
Stances/Create-a-Hero initialization. Language files under `lang/english` are
exposed at the root, as expected by existing menu readers. Both the shell and
in-game text consumers understand CSF.

## Isolation

BFME1 support never changes RotWK / BFME2 behaviour (community PR #36 review). `Common/BfmeGame.h`:

- **Which game:** `isBfme1Data(fs)` (`SubsystemLegend.ini` without `SubsystemLegendExpansion1.ini` or BFME2's `CreateAHeroSystem.ini`), cached per INI
  environment (`INIEnvironment::bfme1Data`).
- **INI rows:** BFME1's extra rows are registered per table (`RegisterBfme1FieldRows`) and found only in BFME1 data; RotWK's tables stay
  the binary's, so RotWK data still fails on these fields as retail does. Value-level differences (AudioEvent `Control` names, special power
  `Enum` names, particle priorities, OCL dispositions, command points) ask `INI::bfme1Data()`.
- **Simulation:** differences ask the template (`ThingTemplate::bfme1Data()`, set by a ThingFactory built for BFME1 data): the vision default,
  build plots under their occupant, the castle flags. A RotWK game's state hash and saves are unchanged.
- **Files read through a file system ask that file system:** map chunks (`MapReadOptions::bfme1`, set by every map / base / library reader
  from its file system), the map cache, and the W3D model and texture layout (`W3DFileSource::FlatAssetLayout`, `Resolve_W3D_Texture`'s
  `flatLayout`), which the simulation's asset manager (launch bones) also reads.
- **Client paths with nothing to ask** (the texture decoder's PNG / JPEG sniffing, the Apt VM's type-8 constants, floor picking) ask
  `bfme1Mounted()`, set by the BFME1 mount once its archives pass `isBfme1Data` and cleared otherwise. It never decides a game rule.
- What BFME1 mode infers rather than ports is stop **S-5017** (docs/STOPS.md), in the BFME1 mount's report.

## How the BFME1 data differs

These are the places where BFME1's data or menus differ from RotWK's. Each one is handled when the BFME1 archives are mounted
(`isBfme1Data`, `Common/BfmeGame.h`: `SubsystemLegend.ini` without `SubsystemLegendExpansion1.ini` or `CreateAHeroSystem.ini`):

- **Models and textures** are flat: `art\w3d\<name>.w3d` and `art\textures\<name>.dds`, with no two-letter folders
  (`W3D_Set_Flat_Asset_Layout`, set by the mount).
- **Command points:** GameData has `GoodCommandPointsMP56` / `MP78` (5–6 and 7–8 players), not one row per count
  (Open-BFME-1 `Player_newMap.cpp`).
- **AIData has no `DeepWaterDepth`, and GameData has no `MaxCells*` pathfinder limits.** BFME1 compiles its limits in:
  5000 cells per frame, 400 adjust tries, 2000 patch path cells, 2500 attack path cells, and 30000 cell infos
  (Open-BFME-1 `AIPathfind.cpp`). INFERENCE: the horde-melee, melee-engagement, possible-destination and towards-goal
  budgets have no located constant, so they keep RotWK 2.01's values.
- **Script libraries** use `MPPositionInfo` version 0: the two flags and the team, with no LoadAIScript flag and no
  side restrictions.
- **Victory:** GameData has no `VictoryCondition*ObjectFilter` rows. INFERENCE: a base is a `STRUCTURE` that is not
  `IGNORE_FOR_VICTORY`, and an army is any unit that is not a `DOZER` (the compiled rule isn't in Open-BFME-1).
- **Vision defaults:** an omitted `ShroudClearingRange` defaults to `VisionRange`, as in Open-BFME-1 `Object.cpp`.
  Gondor's keep relies on this; treating it as zero hid the keep and castle floor behind shroud.
- **Last map:** selecting a skirmish map immediately saves the current profile's lobby. The preferred map is restored
  even when the remaining saved lobby settings cannot be reused.
- **Start castles:** a match starts with each player's castle standing. INFERENCE: a castle flag that uses
  `UseTheNewCastleSystemInsteadOfTheClunkyBuildList` (no RotWK castle has that row), and whose owner's faction has a
  `CastleToUnpackForFaction` entry, unpacks that castle at once and for free on its first update. Neutral flags keep
  the normal unpack price. The retail trigger (the caller of `CastleBehavior::initiateUnpack`) isn't in Open-BFME-1.
- **Castle layouts:** BFME1's new castle system places its authored layout without per-piece overlap checks;
  adjacent wall and gate geometry deliberately overlaps. Applying the RotWK placement check drops those pieces.
- **Occupied plots:** starting construction hides and deselects a regular build plot, closing its commands. The plot returns when its occupant is removed.
- **Ground texture alpha:** JPEG ground textures take priority over paired PNG alpha masks and load their alpha through the same packed-texture merge used by the UI.
- **Build-plot picking:** plot surfaces use `W3DFloorDraw`, so mouse picking tests their visible floor triangles
  as well as scripted model draws. Hidden floors remain unpickable.
- **Skirmish maps:** the lobby lists multiplayer `map mp` entries with at least two players, excluding the
  `test`/`nctest` utility maps. Patch 2.22 also marks co-op campaigns and utility maps as multiplayer;
  those stay loadable by name but are omitted from the skirmish list.
- **Texture replacements:** PNG/JPEG files are accepted by BFME1 texture lookup (including `GBCampFloor`), and image signatures are recognized even when replacements retain `.dds` filenames.
- **Palantir commands:** BFME1 uses six fixed `CommandButtons.Command1` … `Command6` slots and `SetCommandButtonState`, rather than BFME2's dynamic content frames. Slot state updates only run when content changes, so show animations can finish.
- **Command tooltips:** BFME1's legacy cursor blocks are skipped to their `End` when loading help fonts. BFME1 has no `OnHelpBoxLoaded`:
  the tooltip is `Palantir.apt`'s own bordered `HelpBox` clip. The RotWK help box state machine drives it through the root's
  `SampleHelpBoxTextWidth` / `HideHelpBox` / `ShowHelpBox(height, "%g")`, and the text is drawn into the clip typed `HelpBoxText`
  (Open-BFME-1 VA 0x00593E60, 0x005649A0, 0x00592640). INFERENCE: the width sample waits for the `Text` clip's shape, which is
  empty on the first frame after the call.
- **Audio Control flags:** BFME1's names are `LOOP RANDOM RANDOMSTART INTERRUPT FADE_ON_KILL FADE_ON_START` (VA 0x012A8264):
  bit 1 is RANDOM, and an event without it plays in order. RotWK inverted that bit to SEQUENTIAL, so BFME1 values are stored
  with bit 1 flipped. Reading `Control = random` as RotWK's unnamed bit 6 stripped every sound effect's `Data\Audio\Sounds\` folder
  and `.wav` extension, which silenced the game.
- **Skins:** patch 2.22 models have skin weights that sum to 99 or 101 (scaled to 1). Some also weight a second bone without a
  `VERTICES_2` stream, and these are drawn on the first bone, an inference from BFME1's influence loader (VA 0x00925380). Rigid
  meshes that carry an influence chunk ignore it. All three are reported as `[S-020]`. Before, such meshes were rejected, which
  made Gandalf invisible.
- **Button art:** patch 2.22 ships RLE (type 10) TGAs such as `uiGondorSoldier.tga`, and BFME1 UI textures decode them.
- **Health bars:** an absent `AllHealthBars` option means on in BFME1 (GlobalData + 0xA8E defaults to 1), so selected or
  hovered infantry and cavalry get bars. RotWK's default stays off.
- **Multi-selection portrait:** BFME1 maps the player's side to `UP<Side>_Army` (VA 0x00596D40). It has no
  `MultiSelectionPortrait` field.
- **Score screen:** BFME1 has no `TimeLine.apt`, and pushing it left only the red background. `ScoreScreen.apt` (`AptScoreScreen`)
  gets the per-player `PlayerTable:<row>:<field>` texts, colours and factions, and its Continue/Exit return to the menus
  (Open-BFME-1 VA 0x005770E0, 0x00576C20, 0x005731E0). INFERENCE: the score data has no alliances, so each row is its own team.
  The campaign page, Save Replay, Restart and unit renaming are not ported.
- **Pause menu:** BFME1 uses type-8 `PushData` constants to reference the active constant pool. Resolving these initializes each button's own text and callback, including the Restart/Exit confirmations.
- **Keys and voices:** only `Data\INI\CommandMap.ini` exists (there's no root `CommandMap.ini`), and there's no
  `CrowdResponse.ini`, because BFME1 has no crowd responses.
- **Main menu:** `MainMenu.apt` shows its menu bar only when `ShowMainMenu()` is called. INFERENCE: BFME1's
  `OnInitialized` makes that call (Open-BFME-1 declares `_bfme_onInitialized` without its body). Its button instance
  names don't match the buttons: the clip named `Skirmish` is the Campaign button, and `Tutorial` is Skirmish. Scripts
  find buttons by their `buttonName` label (`$SoloPlay`, `$Skirmish`), as a player does.
- **Skirmish lobby:** `Skirmish.apt` opens with `OpenDefault`, its persona popup opens with `PopUpPersona` and closes
  with `PopUpControl("close")`, and it sends `Skirmish::PersonaAccept` / `PersonaCancel` / `PersonaRemove` /
  `PersonaOk`. The Start button is `bottomNav.StartGame`.

## Limits

This is not yet a complete or retail-compatible BFME1 implementation. The shared
simulation still has unported modules, and BFME1-specific castle, horde, audio,
and combat behavior needs further comparison with the original game. Legacy
fields that lack runtime consumers are retained rather than discarded.
BFME1 particle priorities currently map to the shared renderer's quality tiers;
its original particle budget is not reproduced. One shipped unresolved mounted
Faramir voice reference (`UnitSpecificSounds` `VoiceGarrison = FaramirKnightVoiceGarrison`, which no AudioEvent defines) loads as BFME1 loads
it: BFME1's `UnitSpecificSounds` lines store the event name without looking it up (Open-BFME-1 `ThingTemplate_parseArbitrarySoundsIntoMap.cpp`).

The Living World Good/Evil campaigns and their saves are not implemented. Their
menu requests display the existing unavailable-screen message instead of trying
to start a BFME2 linear campaign. BFME1 currently uses the English language view.

## Checks

Synthetic CSF tests are included in `openbfme_tests --test-case='GameText:*'`.
The installed-data smoke test is:

```sh
BFME1_INSTALL="/path/to/BFME1" godot --headless --path godot \
  --script res://tests/bfme1_test.gd
```

It checks object loading, the real main menu and Skirmish shell, and a
Gondor-versus-Mordor Fords of Isen game. It skips when `BFME1_INSTALL` is unset.

The whole launch, scripted with real mouse events (main menu → Solo Play → Skirmish → persona → Start → 10 s of play):

```sh
BFME1_INSTALL="/path/to/BFME1" godot --headless --path godot -- --game=bfme1 --auto --check --advance=10 \
  "--map=maps/map mp fords of isen/map mp fords of isen.map" --faction=FactionGondor
```

`godot/tests/bfme1_quit_test.gd` checks the camp-floor material and all six pause-menu buttons with real movie clicks, including confirmations.

`godot/tests/bfme1_actions_test.gd` clicks a castle plot, builds a production
building, recruits a battalion through its command bar, verifies combat damage,
moves the battalion to a neutral outpost, verifies ownership changes, and unpacks
that outpost into faction build plots. It passes for all four factions on Fords
of Isen. Select a faction with `-- --faction-index=3` (Rohan), `4` (Gondor), `5`
(Isengard), or `6` (Mordor). The fixture supplies starting cash and a nearby enemy;
it uses player commands for construction, recruitment, movement and combat.

`game_switcher_test.gd` checks both selector choices and restart argument handling.
`game_switcher_mount_test.gd -- --game=bfme1` (or `bfme2`) checks that the selected
game mounts its own archives and opens the corresponding main menu and selector.

The BFME1 texture loader recognizes DDS, TGA, PNG and JPEG by their signatures:
the installed data includes TGA and PNG content named `.dds`. Mapped-image movie
operations and shell gadgets share that loader and BFME1's flat texture paths.
Skirmish rank/progression bars still have no battle-honors provider; their
`LevelBar*` errors remain explicit in the shell report. Profile statistics and
Living World campaigns remain outside the implemented gameplay flow.

Validation on 2026-10-17: all four faction action checks, BFME1 object/castle
smoke, HUD buttons/tooltips, pause menus, and both game-selection mount checks
passed. The packaged BFME1 app reached a skirmish with zero load and texture
errors; six LevelBar reads remain unresolved. The broader RotWK smoke check
reported seven failures across sort priorities, object setup, menu captions,
instance counts and move-order state hashes; it is not recorded as passing.
Those failures have not been isolated against an unchanged baseline. No simulation code was changed in this update.

Converted community maps can retain references to absent BFME2 side-script
libraries (for example the 2.22 Buckland map's Gollum-spawn library). BFME1
loads omit these absent map-authored references and report them in the load
notes. This is a compatibility inference, not verified retail behavior. Present
libraries must still parse successfully, and AI libraries remain required.
BFME2/RotWK retain strict dependency checking. BFME1 live loads also omit map
placements whose templates are absent, preserving them in the classification
census and reporting each omission. Case mismatches and errors in templates
that are present remain failures.

BFME1 `BloodthirstyUpdate` also accepts and stores `NumToSacrifice` and
`InitiateVoice2`. These legacy fields are refused in RotWK data. Their runtime
semantics remain unported and are reported by S-5017; the BFME1 donor constructor
initializes the sacrifice count to 1.

Standalone texture loads, including map weather particles, use the same
game-specific resolver as model materials. BFME1 names such as `exraindrop.tga`
resolve to `art/textures/exraindrop.dds`.
`godot/tests/bfme1_weather_test.gd` checks the resolved path and starts matches
on Ered Lithui and Nimrodel.

### Battle School movie callbacks

BFME1 MainMenu.apt uses `_CallOnLastFrame` to reveal the book's foreground and
advance lessons: `TutortialOpenBookDone`, `TutorialPageFlipDone`,
`TurtorialMovieDone`, and `TutorialCloseBookDone` (spellings from the assets).
The host dispatches these once after decoding the final frame, with the movie
instance path, and reopens a stream when its `_MovieName` changes.
BFME1 `Rva00465770MovieProperties.cpp` (0x465770) and its callback at 0x465670
(`BfmeConv1944.cpp`) establish the property and callback argument.

The main menu mirrors the host's `InGame` flag onto its root. This remains an
inference like the existing `ShowMainMenu()` initialization: its book's frame 13
reads `_root.InGame`, while other scripts read `_global.InGame`. Without the root
flag it takes the authored preview path and removes the opening movie early.
The native writer is still unverified (S-5017); no simulation behavior changes.
Other games' BinkMovie completion and hold-last-frame semantics remain S-2342.
