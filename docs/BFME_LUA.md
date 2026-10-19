# bfme Lua library

OpenBFME's console runs an object-based `bfme` API on **LuaJIT 2.1**, the VM of
[lua-gdextension](https://github.com/gilzoide/lua-gdextension) (see [Lua in Godot](#lua-in-godot-lua-gdextension) below). Its collections and player/template fields
follow the [BFME2Lua / Second Age API](https://docs.secondage.dev/#templates); functions are grouped into libraries
and actions are methods on handles. This is an independent implementation over OpenBFME's engine;
it does not load or inject the Second Age DLL. The retail game's Lua 4.0.1 VM continues to run its original scripts independently.

The API reads the game and offers functions that bind its behaviour and mechanics, plus engine handlers that report
its events. **It changes nothing by itself**: no script ships with the game, and the game only changes when a script
calls one of the actions below. Registering handlers, update callbacks or reading snapshots leaves the simulation
exactly as it would be without Lua (`tests/lua_events_test.gd` compares the state hashes of both).

Open the console with **Ctrl+Shift+C** in the menus or a match; **Escape** or Ctrl+Shift+C closes it. Backtick keeps
its game binding (RotWK's powers menu). The console has two views: **Command** (output and a syntax-highlighted
multiline editor) and **Log** (everything the console printed). Enter runs the whole editor; Shift+Enter inserts a
new line. Up / Down at the editor boundaries browse history. The last 256 commands persist between sessions in
`user://console_history.cfg`, including multiline scripts. `clear` empties the current view.

Entry scripts run at startup and again on **Ctrl+Shift+F5** or `bfme.console.reload()`; see [Lua paths](#lua-paths).
Console globals persist; reloaded functions replace their prior definitions.

```lua
local bfme = import("bfme") -- also available globally and through require("bfme")
bfme.players.localPlayer.money = 10000
local barracks = bfme.debug.getSelectedBuilding()
if barracks then barracks:trainUnit("GondorFighterHorde", 3) end
```

## Structure

`bfme` is a read-only module object with three kinds of members:

- **Collections** (`bfme.players`, `bfme.templates`, `bfme.unitTypes`, `bfme.buildingTypes`, `bfme.unitInstances`,
  `bfme.buildingInstances`) index engine data and return **handles**.
- **Handles** (`Player`, `Template`, `UnitType`, `BuildingType`, `Unit`, `Building`) expose fields and the actions
  that belong to them, called with `:` — `building:trainUnit(...)`, `player:construct(...)`, `unit:kill()`.
- **Libraries** (`bfme.match`, `bfme.input`, `bfme.selection`, `bfme.mutations`, `bfme.debug`, `bfme.console`, `bfme.events` ...)
  group functions that do not belong to a single handle.

The only globals are `bfme`, `import`, `print` and the standard Lua libraries. Each library is also reachable with
`import("bfme.<name>")` or `require("bfme.<name>")`, and its functions accept both `.` and `:` calls.
`bfme.capabilities` is a detached `{luaVersion, libraries}` table.

```lua
local debug = import("bfme.debug")
print((bfme.debug).getSelectedBuilding(), debug:getSelectedUnit())
for name in pairs(bfme.selection) do print(name) end
```

`bfme`, libraries, collections and handles cannot be replaced or extended with raw writes. A field a library does
not define reads as `nil`.

## Mutation tickets

Every action and field write queues work for the next game update and returns an integer **ticket**:

```lua
ticket = bfme.players.localPlayer:setMoney(10000)
-- On a subsequent update / console command:
print(bfme.mutations.getStatus(ticket)) -- pending, applied, a failure reason, or nil
print(bfme.players.localPlayer.money)
```

Expressions display their returned values directly; `=expression` is also accepted. Ordinary statements, functions, loops, `pairs`, `ipairs`, `#`, and modern Lua syntax work.
Globals persist across menu and match transitions. Handles from an earlier match expire. Look players up again after starting a new match.

## Collections

| API | Behavior |
| --- | --- |
| `bfme.players[id]` | Engine player slot, including neutral slot 0; unknown slots return nil |
| `bfme.players.localPlayer`, `.neutral` | Current local player and neutral player aliases |
| `bfme.templates[id]` | All loaded definitions by INI template name |
| `bfme.unitTypes[id]` | Selectable, non-structure definitions |
| `bfme.buildingTypes[id]` | Structure definitions |
| `bfme.unitInstances[id]`, `bfme.buildingInstances[id]` | Instances by engine object ID |
| `pairs(collection)`, `#collection` | Enumeration and count; player/instance keys are integers |

## Handles

Every handle has:

| Method | Behavior |
| --- | --- |
| `handle:set(field, value)` | Validate a field write and return its ticket (same as `handle.field = value`) |
| `handle:inspect()` | Detached `{label, kind, id, values}` snapshot |
| `pairs(handle)`, `tostring(handle)` | Scalar snapshot and readable identity, e.g. `Building[42]` |

### Player

| Method | Behavior |
| --- | --- |
| `player:setMoney(n)` | Set the balance (0..2,147,483,647) |
| `player:addMoney(n)` | Add or subtract from the committed balance; the result must stay in range |
| `player:revealMap()` | Explore the whole map once |
| `player:setFogOfWar(enabled)` | `false` reveals the map permanently for the player (the shroud's permanent reveal), `true` undoes it; repeated calls do not stack |
| `player:units()`, `player:buildings()` | Dense, ID-sorted lists of the instances this player controls |
| `player:construct(building, position, speed, onComplete)` | Construction on one of the player's foundation plots (`position.plotId`); see [Construction](#construction) |

| Fields | Access |
| --- | --- |
| `id`, `name`, `displayName`, `username`, `faction`, `isAI` | Read only |
| `money`, `gold` | Same balance; writable integer 0..2,147,483,647 through native deposit/withdraw bookkeeping |
| `commandPoints`, `commandPointsBase`, `commandPointsCap`, `commandPointsUsed` | Writable integer 0..1,000,000 |
| `commandPointsBonus` | Writable integer -1,000,000..1,000,000 |
| `commandPointsRemaining` | Read only; effective capacity minus usage |
| `powerPoints`, `powerpoint` | Writable integer 0..1,000,000 through native science-point updates |

### Template, UnitType, BuildingType

`id`, `name`, `displayName`, `faction`, `side`, `cost`, `refund`, `buildTime` / `trainTime`, `visionRange` and
`shroudClearingRange` are read only (a write returns a failed ticket: the definitions come from the game's INI files).
Fields absent from native data return nil. Display names expose the definition's native INI value, which may be a
localization key.

### Unit and Building

| Method | Behavior |
| --- | --- |
| `object:setInvulnerable(enabled)` | Boolean; queue native indestructibility on/off, including current contained units (battalion members or garrison). Returns a mutation ticket. |
| `object:kill()` | Normal death |
| `object:destroy()` | Native destruction and cleanup |
| `unit:construct(building, position, speed, onComplete)` | This unit builds for its owner; see [Construction](#construction) |
| `building:trainUnit(unit, count)` | Queue recruitment; see [Training units](#training-units) |

Fields include `id`, `objectId`, `typeId`, `type` / `template` (a UnitType/BuildingType handle), `playerId` / `player`, `ownerPlayer` (a Player handle),
`faction`, `displayName`, `position = {x,y,z}`, `x`, `y`, `z`, `rotation`, `health`, `maxHealth`, `invulnerable` (read only native body flag), and `level`. Buildings also expose `underConstruction`, `constructionPercent`, and `builderId`.
Coordinates use SAGE world axes. Position and health are read only; modifying a position copy does not move the unit.
`ownerPlayer` accepts an engine player ID or Player handle, and `level` uses the object's native experience system. Unsupported levels return a failed ticket.

```lua
for id, unit in pairs(bfme.unitInstances) do
    print(id, unit.typeId, unit.playerId, unit.health)
end
for _, building in ipairs(bfme.players.localPlayer:buildings()) do
    print(building, building.position.x, building.position.y)
end
```

```lua
local object = bfme.selection.first()
if object then
    object:setInvulnerable(true)
    -- Later: object:setInvulnerable(false)
end
```

This uses the existing body indestructibility flag: damage (including `:kill()`)
is blocked; explicit `:destroy()` still removes the object. It does not heal,
prevent targeting, or change other armor/modifier immunities. New contained units
follow the engine's native creation rules; the call updates current contents.

## Libraries

| Library | Functions |
| --- | --- |
| `bfme.match` | `getStatus()`, `getFrame()`, `getTimeMs()`, `setGameSpeed(x)`, `pause()`, `unpause()`, `isPaused()` |
| `bfme.input` | `getCursorPosition()` |
| `bfme.selection` | `all()`, `units()`, `buildings()`, `first()`, `count()` |
| `bfme.mutations` | `getStatus(ticket)`, `getBuilding(ticket)` |
| `bfme.debug` | `getSelectedBuilding()`, `getSelectedUnit()`, `inspect(handle)` |
| `bfme.console` | `reload()`, `log(text)` |
| `bfme.orders` | the unit and building orders below, for a handle or a list of handles of one player |
| `bfme.ai` | `list()`, `setEnabled(player, enabled)`, `isEnabled(player)`, `getStatus(player)` |
| `bfme.events` | `onUpdate(name, fn, intervalMs)`, the engine handlers (`on(event, name, fn)`, `onMatchStarted` ...), `engineEvents()`, `remove(name)`, `list()` |
| `bfme.layouts` | `save(name, data)`, `load(name)`, `list()`: JSON storage in `user://building_layouts/` |
| `bfme.map` | `getStartPositions()`, `getFarmSites()`, `getGroundHeight(x, y)`, `distance(a, b)` |

### bfme.match

`getStatus()` returns `{ready, generation, frame, paused, localPlayerId, networkGame, replay, playerCount, templateCount, unitCount, buildingCount, selectionCount}`.
`getFrame()` is the current logic frame and `getTimeMs()` a monotonic clock.

`setGameSpeed(0..100)` sets the simulation speed multiplier: 0 stops time, 1 normal, 2 twice as fast, 100 one hundred times as fast.
Decimals are allowed. Pause is independent: `pause()` / `unpause()` return tickets, `isPaused()` reads the sampled state.

### bfme.input

`getCursorPosition()` returns `{x, y, z}` in SAGE map coordinates, with `z` being terrain height. It uses the displayed camera
and the gameplay pointer, including controller pointer movement, just like native building placement. Returns `nil` without
an active HUD/map or when the pointer is outside the viewport or the ray misses terrain. This is the terrain underneath
the pointer, not the surface of a unit/building or the minimap's represented location.
The position is sampled at the start of the chunk and returned as a detached table.

```lua
local pos = bfme.input.getCursorPosition()
if pos then bfme.players.localPlayer:construct("GondorBarracks", pos, 2) end
```

### bfme.selection and bfme.debug

Selection reads use the **local client selection**, sampled at the start of a chunk, newest first; the first entry is
the object that drives the command bar. `all()`, `units()` and `buildings()` return handle lists, `first()` the newest handle.
Hordes appear as their container unit. Selected objects outside the unit/building collections (for example, props) are skipped.
Selection is read-only.

`bfme.debug.getSelectedBuilding()` / `getSelectedUnit()` return the most recently selected Building / Unit handle, or nil.
`bfme.debug.inspect(handle)` is `handle:inspect()` with an explicit type check.

### bfme.mutations

`getStatus(ticket)` returns `pending`, `applied`, `building` / `complete` (construction), a failure reason, or nil once
the result has aged out (1024 results retained; 128 pending mutations allowed). `getBuilding(ticket)` returns the Building
a construction ticket created while it exists in the current snapshot.

### bfme.console

`reload()` rereads the entry scripts and returns how many ran. `log(text)` writes one console line; `print(...)` also writes there.

## Scripted computer players

A computer player's skirmish AI can be switched off so a Lua script plays that player instead.
Nothing is removed: the native AI keeps its state while it is off and resumes from it when it is handed back.

```lua
local ai = bfme.ai.list()[1]          -- a computer player
ai:setAIEnabled(false)                -- the native AI stops deciding (a ticket, applied at the next update)
bfme.events.onUpdate("myAI", function()
    for _, b in ipairs(ai:buildings()) do
        if b.isFactory and b.productionCount == 0 then
            for _, t in ipairs(b:getTrainableUnits()) do
                if t.ok and not t.isBuilder then b:trainUnit(t.unit) break end
            end
        end
    end
end, 1000)
```

While a script controls a player, its native base builder, team builder, brain (targets and attacks) and automatic
unit ability casting do not run. Bookkeeping still runs: structures and builders are recorded, and a defeated AI shuts
down. `ai:setAIEnabled(true)` hands the player back. `player.hasAI`, `player.aiEnabled` and `player.scriptControlled`
show the state.

Only a script's explicit `setAIEnabled(false)` hands a player over; no script does so by default.

### Update callbacks

`bfme.events.onUpdate(name, fn, intervalMs)` runs `fn(name)` every `intervalMs` milliseconds of real time (default 500,
range 50 to 3,600,000) at a safe point between simulation frames. Each run sees a fresh snapshot and has the 250 ms
execution budget. Registering a name again replaces it, so reloading a script does not duplicate its callbacks.
A failing callback keeps its schedule and writes its error to the log once per new message. `bfme.events.list()` shows
each callback's runs, failures and last error, and `remove(name)` stops one. Output from callbacks goes to the session log
(`LUA ...` lines). Callbacks survive match changes; check `bfme.match.getStatus().ready`.

### Engine events

Engine handlers report what the game does. Each receives one value-only event table:

| Event | Function | Fields | When |
| --- | --- | --- | --- |
| `matchStarted` | `onMatchStarted(name, fn)` | `frame` | once after a successful live map load |
| `matchEnded` | `onMatchEnded(name, fn)` | `frame` | when the game is left (victory, defeat, quit or restart) |
| `buildingPlaced` | `onBuildingPlaced(name, fn)` | object fields, `builderId` | native construction accepts a building: dozer, plot or immediate build |
| `constructionComplete` | `onConstructionComplete(name, fn)` | object fields | an object finishes construction (or is created complete during a match) |
| `unitTrained` | `onUnitTrained(name, fn)` | object fields, `producerId` | a building produces an object: a horde, then each of its members |
| `upgradeComplete` | `onUpgradeComplete(name, fn)` | object fields (the producer), `upgrade` | research completes |
| `objectDied` | `onObjectDied(name, fn)` | object fields, `killerId` | an object dies (`killerId` 0 without a known killer) |
| `playerDefeated` | `onPlayerDefeated(name, fn)` | `playerId` | the victory rules eliminate a player |

The object fields are `objectId`, `typeId`, `playerId`, `frame` and `position = {x, y, z}`. `bfme.events.on(event, name, fn)`
registers any of them by its event name, and `bfme.events.engineEvents()` lists the names. Placement previews,
rejected orders and the objects of the map itself do not emit events. IDs in a notification can have expired by
dispatch time.

The game calls a notification hook (`GameLogic::notifyScriptEvent`) that only copies these values into a queue; Lua runs
at the next safe point between frames, with a fresh snapshot and the same 250 ms budget. Nothing in the game reads the
hook. Handler errors are logged without stopping other listeners. Mutations and events generated by handlers wait until
a later safe point. Named listeners replace the same name for that event, survive match changes, and appear in
`bfme.events.list()` with an `event` field. `remove(name)` removes that name from timers and every engine event.
Queued notifications are discarded when a new match starts. Registration receives future events only.

```lua
bfme.events.onBuildingPlaced("construction-log", function(event)
    print("Placed", event.typeId, event.objectId)
end)
```

### Orders

Orders go through the same player commands a click sends (and the native AI uses). They run in the next logic frame
for the objects' owner. Each order selects the objects, gives the order, and then restores that player's previous
selection, so a human's next click is unaffected. A ticket's `applied` means the order was issued.
Positions are `{x=..., y=...}` tables or handles (their position).

| Unit method | Building method | Order |
| --- | --- | --- |
| `moveTo(pos)`, `attackMoveTo(pos)`, `forceMoveTo(pos)`, `addWaypoint(pos)` | | move |
| `attack(target)`, `forceAttack(target)`, `attackGround(pos)` | `attack(target)`, `attackGround(pos)` | attack a Unit/Building or the ground |
| `stop()` | `stop()` | stop |
| `enter(building)` | `evacuate()` | garrison / empty a garrison |
| `repair(building)`, `resumeConstruction(building)` | | builders |
| `setStance(name)` | | `battle`, `aggressive`, `holdGround`, `porcupine` |
| `useAbility(name, target?)` | `useAbility(name, target?)` | a special power: no target, a position or a handle |
| `construct(building, pos, speed, onComplete)` | `trainUnit(unit, count)` | see [Construction](#construction), [Training units](#training-units) |
| | `researchUpgrade(name)`, `cancelUpgrade(name)` | upgrades |
| | `setRallyPoint(pos)`, `sell()` | |

`bfme.orders.<order>(objects, ...)` gives the same order to a list of handles at once, which all must belong to one
player. Players have `purchaseScience(name)` and `castPower(name, target?)` (a spell-book power: no target, a position or a handle).

### What a script can ask

These are read-only engine answers, computed at once.

| Method | Answer |
| --- | --- |
| `building:getTrainableUnits()` | its command set's units: `{unit, ok, status, cost, isBuilder, isHero}` |
| `building:canTrain(unit)` | `ok, status` (`ok`, `not enough gold`, `prerequisites missing`, `production queue full`, ...) |
| `building:getUpgrades()` | its upgrades: `{upgrade, kind, ok, status, cost}` (`complete`, `researching`, ...) |
| `builder:getBuildableStructures()` | `{building, cost, affordable, onPlot, isEconomy, isFactory, radius}` |
| `player:findBuildSite(building, near, radius?, builder?, options?)` | the nearest valid site `{x, y, z}` within `radius` (default 600), or nil; `options.clearance` keeps that gap to every structure, `options.angle` is the facing to test, `options.avoid` lists `{x, y, r}` circles to keep free |
| `player:canBuildAt(building, pos, builder?, angle?)` | `ok, reason`: the same check for one position |

With a builder, a site is valid when the builder's own construction check passes (terrain, objects and the owner's
shroud, as `construct()` checks) and the builder has a path to it. Without one, the builder-less construction check applies.
| `object:getAbilities()`, `player:getPowers()` | `{name, ready, percentReady}` |
| `player:getSciences()` | `{science, owned, canPurchase, cost}` |
| `object:isKindOf(kind)` | a KindOf test (`CAVALRY`, `ARCHER`, `MONSTER`, ...) |
| `player:isEnemy(other)`, `player:isAlly(other)`, `player:getStartPosition()`, `player:getAIStatus()` | |

The snapshot also carries what an AI decides from every second. Players have `defeated`, `observer`, `relations`,
`hasAI`, `aiEnabled` and `scriptControlled`. Units and buildings have `isHero`, `isBuilder`, `isHorde` and `isIdle`.
Buildings also have `isEconomy`, `isFactory`, `radius` (how far its footprint reaches), `productionCount`, `producing` and `productionPercent`.

### Limits

For now Lua stays out of multiplayer entirely. Entry scripts still load in the menus, but once a network game starts
no engine handler fires, no update callback runs and the console refuses commands (`Lua is not available in
multiplayer games`), so a script can never desynchronise peers. `godot/tests/lua_network_test.gd` checks this with a
LAN host and joiner.

Gameplay actions (writes, orders, construction, training and the AI switch) are also refused during replay playback
with a failed ticket (`gameplay Lua writes are available in local games only`), so a script cannot make a replay
diverge from its recording. Reads, handlers and update callbacks work in local games and replays.

## Construction

```lua
local p = bfme.players.localPlayer
local ticket = p:construct(
    "GondorBarracks",              -- building template name or BuildingType handle
    {x = 1500, y = 2200, plotId = plotId}, -- one of the player's foundation plots (its position is used)
    1,                             -- construction speed: only the native speed (1) is supported
    function(building)
        print("Construction complete:", building.id)
    end
)
-- or have a builder walk over and build it for its owner (anywhere it may build):
bfme.unitInstances[builderId]:construct("GondorBarracks", {x = 1500, y = 2200})
```

`bfme.layouts.save(name, data)`, `.load(name)` and `.list()` provide restricted
JSON persistence in `user://building_layouts/`, available without a match. Names
use 1..80 ASCII letters, digits, underscores or hyphens. Files are versioned,
limited to 4 MiB, and replaced through a temporary file. These helpers do not
expose arbitrary paths or executable script loading.

`building:getLayoutInfo()` reads a building's completed object upgrades, castle IDs, producer pad coordinates and
linked wall IDs, and `building:giveUpgrade(name)` grants an object upgrade through native upgrade modules.

`player:construct` builds on `position.plotId`: a living, owned foundation ID. This uses native plot construction and
attachment bookkeeping, including fortress upgrade inheritance. The plot supplies the actual position. Construction
elsewhere needs a builder (`unit:construct`); a request without one fails.

`unit:construct` uses a living builder; it walks to the site and builds through its native builder AI. Both forms
charge the owner the normal gold cost and build at the native speed (a speed other than 1 fails). Scripts can choose a
building outside the builder's command set; terrain and overlap checks still apply, and the builder path also uses its
normal placement/pathfinding checks.
Add `rotation` in radians to the position table if needed. Unsupported building modules, units that cannot build,
blocked sites and insufficient gold produce explicit ticket errors.

The method queues the request and returns a ticket, not an immediate building. On a later console command, inspect
`bfme.mutations.getStatus(ticket)` (pending, building, complete, or an error) and `bfme.mutations.getBuilding(ticket)`.

The optional callback receives the completed Building handle exactly once, at a safe
point between simulation frames. It is not called if construction is cancelled or the
building is destroyed. Callback errors appear in the ticket status. A callback can enqueue
more Lua actions; those apply at the next safe point. The existing 250 ms Lua execution
budget applies to callbacks. Requests and callbacks are discarded on registry/match reset.
Callbacks are session-only and are not serialized. Script reload retains already registered callbacks; a new match/load invalidates them.

## Training units

```lua
local barracks = bfme.debug.getSelectedBuilding()
local ticket = barracks:trainUnit(
    "GondorFighterHorde",         -- unit template name or UnitType handle
    3                             -- number of recruitment orders (default 1)
)
```

Count must be an integer from 1 to 1000. The building's owner pays normal costs; production follows native build
times, rally points, prerequisites, upgrades, unit limits, and queue capacity.
Each order recruits the template's normal unit/battalion quantity. The building
must be alive, completed, enabled, and offer that unit in its command set.

This queues native recruitment orders at the next safe point; it does not instantly
spawn units or wait for queue space. Inspect `bfme.mutations.getStatus(ticket)` in a
later chunk: `applied` means all orders were queued, while, for example,
`queued 2 of 3: not enough gold` means two were accepted and remain queued.
Unsupported buildings, expired IDs, and unknown unit types fail explicitly.

## Lua paths

The game reads entry scripts from these directories, in order:

1. `res://lua/` (`godot/lua/`): scripts shipped with the game (none by default).
2. `user://lua/`: the player's own scripts (on macOS `~/Library/Application Support/Godot/app_userdata/OpenBFME/lua/`).
3. Each directory of `--lua-path=<dir>` (command line) or the `OPENBFME_LUA_PATH` environment variable, several separated by `;`.

A later directory's file replaces an earlier file **of the same name**, so a player overrides a shipped script by
saving a file of that name in `user://lua/`. The entry scripts then run sorted by file name. All are compiled before
any runs: a syntax error keeps the previous definitions. `GameWorld.get_lua_paths()` lists the directories, and the
session log prints them at startup (`GAME Lua paths: ...`). See [`godot/lua/README.md`](../godot/lua/README.md).

`require("name")` loads `lib/name.lua` from the Lua paths (`require("a.b")` loads `lib/a/b.lua`), taking the **last**
path that has it, so modules are overridden the same way. Files under `lib/` are not entry scripts. Module names use
letters, digits, `_`, `-` and dots.

## Execution and compatibility limits

Reads use detached snapshots taken when a console chunk starts. Assignments and actions enqueue mutations; they do not update reads within that chunk.
Mutations re-resolve engine IDs between simulation frames with the logic worker held. Missing objects and expired registries fail explicitly.
Inspection and position tables are detached copies.
Lua console use does not change save availability. Lua is unavailable in network games and gameplay writes are refused in replay playback
(see [Limits](#limits)). Local edits are not recorded in command-log saves or replays. A chunk that fails after queuing
writes does not roll those writes back.

This does **not** implement Second Age's complete API: selection edits and command UI,
diplomacy/team edits, power catalogs/unlocks, durability edits, shared values/messages, JSON/dialogs,
controller/camera routing, save sync, or shared script state/source synchronization.
Second Age's flat helpers (`ExecuteAction`, `GetFrame`, global `setGameSpeed`) are not provided; use the handle methods and libraries above.
`require` loads `bfme`, `bfme.<library>` and the `lib/` modules of the [Lua paths](#lua-paths); IO, OS, debug and native module loading are not exposed.
Console execution has a 250 ms Lua instruction-hook budget and a bounded output buffer; this is not a transactional script sandbox.
The language is LuaJIT's: Lua 5.1 with LuaJIT's Lua 5.2 extensions (`goto`, `bit` library, `table.unpack`...). There is no
integer subtype (`3 // 2`, `math.type`, `math.tointeger` and the `utf8` library do not exist; use `math.floor(a / b)`).
LuaJIT's JIT compiler is off in this state: the instruction-hook budget does not reach compiled code, so the console runs on
LuaJIT's interpreter.

## Lua in Godot (lua-gdextension)

[lua-gdextension](https://github.com/gilzoide/lua-gdextension) 0.8.2 (MIT) is built from source into the openbfme
extension, on LuaJIT (lane LUAGD-1; `engine/src/Libraries/LuaGDExtension/README.md`). Its classes are registered like the
engine's own: `LuaState`, `LuaTable`, `LuaFunction`, `LuaCoroutine`, `LuaError`, ..., and Lua as a Godot script language
(`.lua` scripts that return a class table, see the upstream README).

The `bfme` API runs on one of its `LuaState`s. `GameWorld.get_lua_state()` returns it, so GDScript can use what the console made:

```gdscript
var lua: LuaState = world.get_lua_state()
var bfme = lua.globals["bfme"]
var money = lua.do_string("return bfme.players.localPlayer and bfme.players.localPlayer.money")
```

That state opens Godot's Variant types (`Vector3(1, 2, 3)`, `Color`, String methods) and global enums alongside the `bfme`
API. It does not open Godot classes, singletons, `res://` file loading or the utility functions (`bytes_to_var_with_objects`
makes objects), which would reach past the sandbox above. Code run through `get_lua_state()` directly has neither the
console's execution budget nor its output capture. Use `world.execute_lua(code)` for both.

A separate `LuaState.new()` is an ordinary lua-gdextension state with every library it offers. The `bfme` API is not in it.

## Extending the API

The API lives in the bootstrap in `engine/src/GameLogic/ScriptEngine/BfmeLuaRuntime.cpp`.

- **Something one object does** belongs on its handle: add it to `methods[<collection>]` (`players`, `unitInstances`, `buildingInstances`).
- **Anything else** goes in a library: `library('name', {fn = function(...) ... end})`.

Both use the core helpers: `record(handle)` for checked lookups, `proxy(kind, id)` for handles, `templateName(...)`
for template arguments, `numeric(...)` for validation, `queue(...)` for writes and `copy(...)` for detached results.
New engine state is added to `BfmeLuaRuntime::Snapshot` and filled in `GameWorld::bfme_snapshot()`, which runs with
the logic worker held. Object definitions use an immutable shared catalog, marshalled into Lua only when the
catalog changes: loading the definitions again or starting a match discards it. Live players, objects, selection, and match state still refresh for every chunk or due callback.
This keeps periodic status callbacks from rebuilding the entire template catalog on the render thread.
New queued actions are applied in `GameWorld::bfme_apply()`. Add a contract test to
`engine/tests/test_bfme_lua.cpp` and document it here.

- **An order** is a new case in `GameWorld::bfme_order()` (`GodotBfmeLuaAI.cpp`), which builds the player command message, plus an entry in `orderFns`.
- **A question** is a new name in `GameWorld::bfme_query()`, which must not change state, plus a method that calls `query(name, ...)`.

- **An engine event** is a `notifyScriptEvent` call where the game does it (notification only), a name in the runtime's event lists
  (`engineEvents` and `onEvent`), and its fields in `GameWorld::attachLuaEvents()`.

## Verification

- `engine/build/openbfme_tests --test-case='bfme Lua*'` checks module identity, the LuaJIT sandbox, library and handle
  structure, Lua syntax alongside the retail VM, handle identity, enumeration, detached copies, money tickets, validation,
  expired handles, queue limits, construction, training, selection, orders, queries, the AI switch, update callbacks,
  every engine event, `require` of `lib/` modules and error recovery.
- `godot --headless --path godot --script res://tests/lua_events_test.gd` (installed retail data) checks the engine
  handlers in a live game (placement, construction, training, death, match start and end), and that registering
  handlers leaves the state hash unchanged.
- `godot --path godot --script res://tests/lua_console_test.gd` (no game files) exercises the console: the Ctrl+Shift+C
  toggle, the Command and Log views, history, and the Lua paths' override and `require` order.
