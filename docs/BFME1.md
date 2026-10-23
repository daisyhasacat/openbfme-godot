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
- **Distant build plots:** translucent, non-depth-writing `W3DFloorDraw` markings use base-level linear sampling.
  Their alpha-blended source textures otherwise blur under minification; the earlier alpha-test coverage correction
  does not apply to these materials. The overlays also render at transparent priority 32, after the depth-writing
  castle floor. Sharing its default priority allowed the floor to cover every plot at some start positions,
  including the second castle on Fords of Isen. Depth testing stays enabled, and opaque castle floors retain mipmaps. This is an OpenBFME presentation policy
  (S-5017), not a verified retail sampler setting; very distant thin lines can alias.
- **Create-a-Hero and saves:** BFME1 does not initialize or open the BFME2/RotWK hero builder, or switch to their menu
  presentation. OpenBFME saves use `user://bfme1/Save` (unless `--save-dir` overrides it), without consulting RotWK's
  `gi.dat` or hero folder. This separate namespace is OpenBFME policy; retail BFME1 save import is still unavailable.
- **Build-plot picking:** plot surfaces use `W3DFloorDraw`, so mouse picking tests their visible floor triangles
  as well as scripted model draws. Hidden floors remain unpickable.
- **Skirmish maps:** the lobby lists multiplayer `map mp` entries with at least two players, excluding the
  `test`/`nctest` utility maps. Patch 2.22 also marks co-op campaigns and utility maps as multiplayer;
  those stay loadable by name but are omitted from the skirmish list.
- **Texture replacements:** PNG/JPEG files are accepted by BFME1 texture lookup (including `GBCampFloor`), and image signatures are recognized even when replacements retain `.dds` filenames.
- **Palantir commands:** BFME1 uses six fixed `CommandButtons.Command1` … `Command6` slots and `SetCommandButtonState`, rather than BFME2's dynamic content frames. Slot state updates only run when content changes, so show animations can finish.
- **Command tooltips:** BFME1's legacy cursor blocks are skipped to their `End` when loading help fonts. BFME1 has no `OnHelpBoxLoaded`:
  the tooltip is `Palantir.apt`'s own bordered `HelpBox` clip in the movie-authored center position.
  The separate bottom-right device panel is removed; both frame and text use the native clip.
  A live attached HUD waits for Palantir's `OnInitialized` before sampling width, since BFME1 has no load handshake. The RotWK help box state machine drives it through the root's
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

Battle School audio follows `GameWorld.get_movie().audio_events`, including the
lesson's separate DialogEvent narration (for example TutorialMovesAndAttacks
names Tutorial2.mp3). BFME1's `AudioEventRTSAdjustForLocalization.cpp` keeps an
existing direct audio file; otherwise `AudioEventRTSGenerateFilenamePrefix.cpp`
uses `lang/<language>/Data/Audio/...`. The audio device derives that language
from the mounted language audio archive and applies the same lookup to streams.
Missing files still reach the audio error report.

`shell_movie_audio.gd` owns each embedded movie's audio handles, removes them
when switching/closing/finishing, and keeps menu music silent through the book
session. It restores the prior menu track on close. The original
WindowTransitions.ini `MainMenuToBattleSchool` requests SOUNDFADE, TACTICAL,
LeaveSilent=Yes (30-frame delay and 30-frame fade). This implementation fades
menu music with the existing audio fade; exact view-wide transition timing is
still unverified under S-5017. The BFME1 movie regression checks actual narration
voices and music suppression/restoration; `shell_movie_audio_test.gd` checks
handle ownership without retail assets.

## Skirmish menu

BFME1 offers Official Maps and Player-made Maps, with the native two-column
map list (honors, name with player count). The built-in catalog comes from the
original mounted `Maps.big` rather than a patch's `mapcache.ini`: only original
retail multiplayer maps are offered, even when a 2.22 pack marks additional
maps official. Map contents retain mounted patch precedence. Installed BFME1
user maps appear separately under Player-made Maps; campaign and scenario maps
are excluded. A missing or ambiguous original archive is a reported boot error.

The profile dropdown supports selection, New Profile and Delete Profile.
The main and expanded profile panels read BFME1's `<profile>SkirmishStats.ini`,
including per-faction results, streaks and rank progress. A Random army shows
retail's dash fields. S-3504 covers remaining statistics recording and platform
date differences. Source evidence: BFME1 MpGameSetupComboPopulation,
MpGameSetupOnInitGadget (0x525AB0), Rva00453480MapPointerCollector,
SkirmishScreenRefreshProfile, SkirmishProfileMessage0057E9A0,
BfmeAptScreenSkirmish::_bfme_updateProfileDisplay (0x57A470),
Rva005674F0Profile, and RankProgress00579990.

Validation: `godot/tests/bfme1_skirmish_menu_test.gd` with `BFME1_INSTALL`
checks the retail catalog, translated names, filtering, scrolling and profile
actions against the user's installed assets. `test_big_archive.cpp` pins
archive-specific catalog reads without changing patch precedence;
`test_apt_skirmish.cpp` pins BFME1 statistics and rank providers.

## Replacement settings menu

BFME1 uses the shared OpenBFME settings menu used by BFME2/RotWK. Its original
Options movie needs `assignOpen("normal")` (or `"online"`) after frame-zero
initialization completes (BFME1 BfmeConv1072, 0x55DC00). The replacement waits
for its animated controls to initialize, hides the two BFME1 settings clips
and their separate borders and tooltip regions, and handles the BFME1
Cancel/Reset/Save buttons through the shared replacement menu events.
`godot/tests/bfme1_options_test.gd` verifies that the replacement opens,
original controls stay hidden, its bottom bar is available and it reopens.

## Opening armies, Q and distant markings

BFME1's opening battalions come from the mounted `Multiplayer_Start_Teams`
script library, not the commented-out PlayerTemplate StartingUnit rows.
The library's team definitions are now instantiated for each receiving player;
`SET_UNIT_REFERENCE` binds the base reference and
`CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION` creates its configured team at
that base. References use the receiving side's named cache, whose IDs already
participate in CRC and save/load. Spawn layout retains the existing S-1186
reinforcement-placement inference. Evidence: BFME1 ScriptActionsReferences
(0x2F5A10), the shipped library scripts and SidesList's library-team merge.
The asset-based test checks all four factions; the tested installed library
configures two battalions for Gondor/Rohan/Mordor and one for Isengard.

The shipped BFME1 CommandMap.ini contains no bindings. Q now has the requested
select-army default when no explicit SELECT_ALL binding or other Q binding
exists. Both the live input map and the settings listing use this default.

Particle lookup uses BFME1's flat texture layout, with DDS signature detection
for DDS streams stored under TGA names. Missing textures remain reported.
As an OpenBFME rendering fix, alpha-tested BFME1 textures retain alpha coverage
in their mip levels. The foundation textures' smallest authored mip levels
fall below the alpha cutoff and otherwise disappear at distant zoom. Only
mip alpha changes; base-level pixels, colours and simulation state stay intact.
The same processing applies to live texture replacements and precedes texture
quality reduction. RotWK/BFME2 keep their existing rendering path.

Validation: `bfme1_starting_armies_test.gd` checks the mounted opening armies
and live Q selection, and loads fire/smoke particle textures. `test_input1.cpp` pins binding precedence;
`test_fx_review.cpp` checks flat particle lookup and shrinking alpha cutouts.

`godot/tests/bfme1_plot_zoom_test.gd` checks Gondor/Isengard live plot shaders and renders camera heights
500, 800, 1200 and 1600. Run with a graphical renderer and `BFME1_INSTALL`; set `BFME1_PLOT_SCREENSHOTS`
to an output directory for visual inspection. `godot/tests/bfme1_save_folder_test.gd` checks the mode-specific
save namespace, explicit override, disabled CAH entry and ignored BFME2/RotWK style requests without retail data.

`godot/tests/bfme1_live_plot_zoom_test.gd` starts the normal game scene, clicks through the lobby with `--auto`,
and keeps the simulation, HUD, shroud and tactical camera active. It checks 1x, 1.5x, 2x, 2.5x and 3x zoom:
hiding only the floor-overlay nodes must change pixels at a local empty plot. This catches an overlay that is
instantiated but covered by the castle floor. Run with the graphical renderer:

```sh
godot --path godot --script res://tests/bfme1_live_plot_zoom_test.gd -- --game=bfme1 --auto --advance=600 \
  "--map=maps/map mp fords of isen/map mp fords of isen.map" --faction=FactionGondor --start-spot=1
```

Use `--bfme1-install` to select an installation, `--faction=FactionIsengard` for Isengard, and
`BFME1_PLOT_SCREENSHOTS` to save the live views. The second start is essential: the first castle did not
reproduce the draw-order failure in the original regression.

`bfme1_hud_test.gd` checks the fixed `HelpBoxText` render callback and the native
`SampleHelpBoxTextWidth`, `HideHelpBox` and `ShowHelpBox` calls as well as visible tooltip text.

`bfme1_live_tooltip_test.gd` uses the normal game scene, opens Skirmish through its host command, starts
through the lobby, selects a live plot and hovers a radial build command. It verifies the native callback,
show calls and rendered text after the shell-to-game transition. Use the live plot test command above with
this script; `BFME1_TOOLTIP_SCREENSHOT` optionally saves the full live frame to a PNG path.

BFME1 unit-position reinforcements now resolve each mobile unit's spawn through the existing
`Pathfinder::adjustDestination` before its build-complete callback populates the horde. Starting directly
at the named base origin put the first battalion inside the capital. This is an OpenBFME correction,
reported under S-1186; the original BFME1 action's placement algorithm is still unverified. A failed
search reports the missing clear footprint and removes that spawn instead of leaving it embedded.
Waypoint reinforcement placement and BFME2/RotWK behavior are unchanged. The opening-army regression
checks the actual soldiers against both rotated Gondor keep footprints, as well as all four factions'
starting counts; a live match on Fords of Isen start 2 confirms both battalions stand outside the keep.

For battalions in all games, an incomplete coarse-corridor path is retried once with the existing
cell search without corridor ordering. Castle rear walls can otherwise leave the battalion
at a partial endpoint despite an open gate providing a longer route. This is an OpenBFME
pathfinding correction, not a verified retail algorithm. Both searches retain their
cell limits, and a retry replaces the original result only when it reaches the destination.
The regression issues one move behind Gondor's castle on Fords of Isen and Rohan's castle
on Anorien and checks arrival of the battalion and each soldier.
