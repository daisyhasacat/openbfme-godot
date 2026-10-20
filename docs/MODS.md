# Mods

OpenBFME loads RotWK 2.01 mods the way the retail game's `-mod "<folder>"` does:
a mod folder sits in front of the verified game files. Its loose files
(`data\ini\...`, `art\...`, `maps\...`) override the archives, and the `.big`
files inside it are mounted too, below its loose files. A single `.big` file
also counts as a mod.

## The mod manager

Age of the Ring is a separate choice in the launcher's game picker and the
in-game game selector, alongside BFME1 and standard RotWK. **AOTR files…** accepts
the installed `aotr` folder, its enclosing `AotR` folder, or its CrossOver
`drive_c` folder. On macOS the BFME Launcher's AgeOfTheRing CrossOver installation
is detected automatically. `AOTR_INSTALL` or `--aotr-install=<folder>` also
selects its files; `--game=aotr` selects the game for one launch.

AOTR uses verified RotWK 2.01 and BFME II 1.06 base installations plus its own
asset overlay. Standard RotWK mods and the optional wall pack are excluded from
this game choice. Its menus use the AOTR overlay rather than the BFME II menu
presentation source. Executables and DLLs from the mod are not run.

The AOTR profile also mounts the installed runtime's sibling
`rotwk/#aotr_patch202.big` before the `aotr` folder. Its GameData macros provide
the dependency definitions; AOTR definitions then override them. Every INI reader
uses that same explicit profile, including settings and effects. Standard RotWK
and BFME1 retain strict duplicate-macro checks.

AOTR data support includes nested filter macros (cycles are errors), separated
object-filter signs, scenario map-cache blocks, the Rohan/Mirkwood/Guldur hero
factions, and SPAM/SLAYER/PATROL AI categories. Retail category IDs and the retail
AI hash range are preserved. AOTR's terminal STR `END` and slash-comment font
substitutions are accepted without accepting unterminated entries or blocks.
Its APT exports also use nonzero boolean flags such as word-wrap value `111`;
these are interpreted as true only in the AOTR profile. The standard APT parser
continues to require canonical `0`/`1` flags.

The AOTR skirmish Rules menu uses the mounted `MultiplayerSettings` initial-credit
choices and `GameData DefaultStartingCash` instead of RotWK's fixed resource
table. In this installation these are 500, 1,000, 1,500 (default), 2,000 and 2,500.
Both the APT and native menus apply these values; Reset restores the AOTR default.
Infinite resources is labelled as an OpenBFME addition. Other rule tables retain
the supported RotWK semantics; this is data-driven menu integration, not a claim
that all rules of AOTR's patched executable are implemented.

Zero-offset starting units, including AOTR's `CommandPointSpawn`, use the retail
nearby-position search around the map's rally point or the generated base. They
are spawned as real objects with their normal behaviors; zero offsets are valid.
The optional AOTR startup test uses Rohan and checks that its marker was created.
Set `AOTR_TEST_MAP` to run that check on a different installed map. Custom maps
still need matching object definitions and script libraries in the mounted game;
missing dependencies remain load errors.

This remains an experimental compatibility profile. Macro override/inheritance
rules and added category IDs are inferred from the installed data; they have not
been verified against AOTR's patched executable. Loading its assets does not
establish gameplay or network parity with that executable. Unknown syntax and
missing assets remain errors. `godot/tests/aotr_startup_test.gd` exercises an
external installation when `AOTR_INSTALL`, `ROTWK_INSTALL` and `BFME2_INSTALL`
are set.

On macOS, archive mounting raises the process's open-file soft limit to at most
4,096 (within the OS hard limit). Finder launches otherwise inherit a limit of
256, which can exhaust descriptors with AOTR and the renderer before all archives
and loose assets are opened. Archive-open errors include the OS reason.

The **Mods** link under **Logs** (below the version number in the menus) opens
the mod manager (`godot/scripts/mod_manager.gd`). It lists:

- every folder and `.big` file in `user://mods`
  (macOS: `~/Library/Application Support/Godot/app_userdata/OpenBFME/mods`);
- every folder added with **Add mod folder…**, wherever it is.

Tick a mod to enable it. Use ▲▼ to change the order: mods lower in the list win.
**Remove** forgets a mod but leaves its files where they are. Changes take
effect after a restart (**Restart to apply**).

A downloaded mod often keeps its game files one level down, next to its
readme and screenshots. RJ RotWK, for example, keeps them in
`RJ RotWK 1 06 3 3/`. The manager uses the folder itself as the mod when it
holds `data`, `art`, `maps`, `apt`, `libraries` or a `.big`. Otherwise it uses
the one subfolder that does.

The choice is stored in `user://mods.cfg`:

```ini
[mods]
order=["/abs/path/modA", "/abs/path/modB"]
enabled=["/abs/path/modB"]
```

For a single run, `--mod=<path>` (repeatable) adds a mod on the command line.

## How a mod is mounted

This is done in `GodotRetailFileSystem.cpp` `mountFrom`, using
`FolderArchiveFile.h`:

- Mods mount after the verified game files, in the order above. A mod's
  `.big` files go first (ascending name, so the first name wins, as in ZH),
  then its loose files.
- Mod files are not verified. The mount report lists each mod with its
  archive and loose file counts, and the log prints a `GAME mod:` line for each.
- **Programs are never mounted**: `.exe`, `.dll`, `.so`, `.dylib`, `.bat`,
  `.cmd`, `.sh`, `.com` and `.scr` are skipped, and so are dot files.
- Empty (0-byte) `.big` files are skipped, with a warning. RJ ships two:
  `art/test.big` and `art/w3d/ar/s.big`.
- **GameData from object files.** RotWK reads a `GameData` block from any INI
  it loads. A mod may keep its GameData in `data\ini\object\*.ini`; RJ leaves
  `data\ini\gamedata.ini` as a comment. Those rows are merged into one
  in-memory `data\ini\gamedata.ini`, after the base file's rows, with a
  warning.
- **Menu movies.** The BFME2-style menus draw BFME2's movies. A mod's `.apt` /
  `.const` files are RotWK's and don't pair with BFME2 movies, so the
  BFME2-style menu view leaves them out, with a warning. RJ ships only RotWK
  `.const` files. Choose the RotWK menus in Options to see a mod's own menus.
- **Texture packs.** A mod marked *Textures only* in the mod manager (`mods.cfg`
  `[mods] textures_only`) mounts only its `art\compiledtextures`, `art\textures`
  and `art\terrain` files over the game's. Its models, INI, maps, UI and
  `asset.dat` are not mounted, and its object INIs are not merged into
  GameData. Use this for HD texture mods such as RotWK 2.02 HD Edition.
- **Local packs stack under mods.** The wall tower pack and the 2.02 map pack
  mount first; enabled mods mount after them and win any file both provide.
  The tower pack's INI uses the stock game's macros, so a mod that removes
  them makes the INI fail to parse, which is reported as an error.

## Tolerated mod problems

These are warnings, not errors, because mods ship them:

- An `AIBase` whose `.bse` layout is missing: that base is not used by the AI.
  RJ has five, for example `mordor_bfme1_dunharrow_2`, whose folder holds a
  `_1.bse`.
- A map that keeps a team with the same name as a player's own team under
  another owner. RJ's `map mp evendim` has `teamPlayer_1` .. `teamPlayer_4`
  owned by the neutral side. RotWK keys teams by owner and name, so a player's
  default team is looked up as `<Player>/team<Player>` (`PlayerList.cpp`).

## Known gaps with RJ RotWK 1.06.3.3

- `AptComponents_001.tga` is RLE-compressed (TGA type 10). The TGA reader only
  reads uncompressed TGAs (`TGAFile.cpp`), so a few list-box, slider and
  rank-icon images do not draw.
- `rjnewsky01` asks for `rjsky01.tga`, but RJ ships `rjsky01.jpg`, so the sky
  texture is reported missing.

## Sharing content on the LAN

Run the Python 3.11+ server on the computer holding the packs:

```sh
python3 tools/lan_content/serve.py
```

It listens on port **8765**, without passwords, and shares configured OpenBFME
mods, texture packs, bundled wall towers/2.02 maps, installed maps, and the BFME
Launcher map libraries. On this Mac it also discovers AOTR (including its runtime
dependency) and Edain's mod archives. Base game installations and executables are
not distributed. Each receiving device still needs its own base game files.
Sharing a mod does not establish engine compatibility with it.

Use **Mods → LAN downloads… → Connect** on another device. The initial address
is `http://192.168.121.42:8765`; edit it if the host's address changes. A successful
connection saves the address. Downloads show progress and verify SHA-256 and
size before unpacking. Texture-only flags are retained; ordinary mods install
disabled and are enabled in Mods, followed by a restart. Wall towers and 2.02
maps use their existing Options switches. Maps use the installed-map importer;
reopen the map browser after importing. AOTR is stored as a separate game and
can be selected in Mods. Downloads never replace a running match's data.

Additional locations can be shared explicitly (repeat each option as needed):

```sh
python3 tools/lan_content/serve.py --mod /path/to/mod --textures /path/to/textures --maps /path/to/map-library
```

`--bind`, `--port`, `--user-dir`, and `--cache` override the defaults. Archives
are created lazily on the first request and cached; changed source files create
a new archive. Stop the foreground server with Ctrl+C. Only catalogued packs
are served; hidden files, symlinks and program files are excluded. No login is
required: any device that can reach the listening port can download shared packs.

Checks: `python3 -m unittest discover -s tools/lan_content`; the Godot
`res://tests/lan_content_test.gd` checks the client and can exercise an isolated
synthetic server with `OPENBFME_LAN_TEST_URL`. iOS exports include local-network
permission and HTTP access for numeric LAN addresses; device validation is still
required after rebuilding the iOS app.
