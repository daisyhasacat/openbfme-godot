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

This is an experimental asset profile, not a claim of playable AOTR support.
The tested installation mounts, but object-world initialization currently fails
on duplicate macro definitions (including `MORDOR_HARADRIM_HEALTH`) and unresolved
AOTR constants. These errors remain visible; selecting the game does not bypass
parsing or provide the behavior of AOTR's patched Windows executable.

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
