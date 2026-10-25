# Named players in Lua and the map editor

Players are presented by their actual map/player names and current faction:
`Player_4 (Men)`, `CreepFaction`, and `NeutralFaction`. Factions are read from
live players, including custom mod factions. Internal player IDs and start
positions remain unchanged in simulation, commands, saved maps and replays.
`Player_4` is the actual player name, not a conversion from a lobby slot or row.

## Lua

```lua
print(bfme.players.table())
local player = bfme.players.Player_4
local samePlayer = bfme.players["Player_4 (Men)"]
print(player.label, player.key, player.faction)
player.money = 10000
unit.ownerPlayer = "CreepFaction"
unit.ownerPlayer = bfme.players.NeutralFaction
bfme.match.getUnitRoster("Player_4")
for name, player in pairs(bfme.players) do print(name, player) end
```

The console's **Players** tab displays a text table of player labels, factions
and display names, refreshed on opening that tab or with its Refresh button.
`tostring(player)` and `player:inspect().label` use the same readable label.
`bfme.players.list()` returns handles ordered by internal ID for scripts needing
an array. `pairs(bfme.players)` now yields player-name keys rather than indices.
Ambiguous names fail explicitly; `list()` and existing numeric lookup remain
available for disambiguation. Unknown names are errors, not substitutions.

Named lookups support the actual player name, its current `name (faction)`
label, and the special faction aliases. `NeutrualFaction` is accepted as an
input spelling alias, but displays use `NeutralFaction`. A faction-qualified
label changes when the player's faction changes; prefer the plain player name
for stored scripts. Numeric lookup, `localPlayer`, `neutral`, and numeric player
arguments remain compatible. Player handles or names can be used for owner
assignments, player-specific methods, roster queries and planning markers.

## Map editor

Object placement and ownership changes use a selectable text table with
**Player / faction**, **Name**, and **Control** columns. Only live players are
listed. Inspector ownership uses the readable label. The editor refreshes its
player list when opened; it never treats the row number as a player ID.

Wave spawner owner controls also display player names, using the live mapping
from start positions to players. Their existing start-position storage and
runtime behavior are preserved. Unoccupied positions are explicitly labelled
`Unassigned start N`; they are not assigned to a guessed player.

## Validation

`openbfme_build_filter_tests --test-case='bfme*'` exercises named lookup,
mutation routing, presentation labels, ambiguous names and backward-compatible
numeric access. `godot/tests/player_table_test.gd` checks sparse IDs, selection
and editor loading. `godot/tests/lua_console_test.gd` covers the Players tab and
existing console behavior. Tests generate their own data and do not bundle
retail assets.
