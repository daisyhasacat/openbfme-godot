# Lua scripts

The `bfme.*` scripting API ([docs/BFME_LUA.md](../../docs/BFME_LUA.md)) runs the `.lua` entry scripts of
the **Lua paths** when the game starts, and again on **Ctrl+Shift+F5** or `bfme.console.reload()`.
The shipped game has none: scripts only add functions and handlers, and nothing changes the game
unless a script calls a `bfme.*` action. For now Lua does nothing in multiplayer games: handlers
and update callbacks stop and the console refuses commands once a network game starts.

## Lua paths

Scripts are read from these directories, in this order:

1. `res://lua/` (this directory): scripts shipped with the game.
2. `user://lua/`: the player's own scripts. On macOS that is
   `~/Library/Application Support/Godot/app_userdata/OpenBFME/lua/`; on Windows
   `%APPDATA%\Godot\app_userdata\OpenBFME\lua\`.
3. Every directory named by `--lua-path=<dir>` on the command line or the `OPENBFME_LUA_PATH`
   environment variable (several separated by `;`), in order.

A file in a later directory replaces the file **of the same name** in an earlier one, so a player
can override a shipped script by saving a file of that name in `user://lua/`. The resulting entry
scripts run in file name order (`01_setup.lua` before `02_events.lua`).
`GameWorld.get_lua_paths()` lists the directories in effect, and the session log prints them at
startup (`GAME Lua paths: ...`).

## Modules

`require("name")` loads `lib/name.lua` from the Lua paths (`require("a.b")` loads `lib/a/b.lua`),
taking the **last** path that has it. Modules can then be shared by entry scripts and overridden
the same way. Files under `lib/` are not run as entry scripts.

## Reloading

Every reload rereads all files from disk. All entry scripts are compiled before any runs, so a
syntax error keeps the previous definitions. A runtime error stops at that file, and files before
it have already run. Globals persist across reloads, and a handler registered again under the
same name replaces the old one.
