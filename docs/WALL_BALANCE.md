# BFME2Lua wall rules

The console’s **Wall rules** tab saves options to `user://wall_balance.cfg`. Match rules are captured before object creation; restart the match after changing them. Restart the app after changing the tower asset option. Defaults are in [wall_balance.cfg](../godot/dev/wall_balance.cfg).

The profile supplies:

- Wall and fortress damage modes: native, wall-capable attackers only, or siege engines only. Healing, scripted kills, and damage without a source keep their native behavior.
- Health multipliers: walls 6×, hubs 30×, gates 6×.
- Cost multipliers: walls 0.5×, run hubs 0.7×, standalone hubs 0.3×, expansions 0.8×.
- Fortress and standalone hub wall ranges: 2×.
- Intermediate hubs disabled; all `WALL_HUB` entries inside a span are replaced with its default wall segment, including outer and expansion hub templates used by Mordor packs. Endpoint hubs remain. Native gate clearance is optional.
- Fortress arrow upgrade sharing, including towers completed after research.
- Elven and Isengard wall arrow towers using the BFME2Lua models, commands, upgrades, and replacement behaviors.
- Optional offline completed starting wall rings with front and back gates. The reference layout uses eight hubs, radius 450, and hub-pair spacing 300. The radius shrinks near map borders. AI players receive starting walls when enclosure is required. Faction hub and gate names are data in the configuration; unsupported factions are skipped.
- Enclosure required for human non-resource building placement. Resources, walls, fortresses, fixed expansion parts, AI players, and Goblins/Wild are exempt. Gates count as closed. Enclosure follows ground navigation and terrain boundaries; breaches invalidate the cached regions.
- A 2× income multiplier for enclosed resource buildings, preserving native fertility and upgrade calculations.

Disabling the master rebalance restores native wall gameplay; the tower asset option is independent. These custom settings are included in the simulation state hash. Use matching profiles and asset packs for deterministic peers/replays; host profile negotiation from BFME2Lua is not implemented here.

The local tower pack contains extracted retail-derived assets and is ignored by Git. Rebuild it with:

```sh
python3 tools/import_bfme2lua_wall_towers.py \
  --source /path/to/BFME2Lua \
  --retail-ini workspace/retail-extract/ini/data/ini
```

The importer combines the new tower definitions and models with the current retail command INIs; it avoids replacing them with BFME2Lua’s unrelated full command tables.

## Automatic paving

Completed wall enclosures now apply a terrain texture during gameplay. Gates
count as closed; breaches remove the paving and closing the gap restores it.
Natural isolated land without walls is not paved. The existing enclosure test
requires at least half of a region's boundary to be wall edges for paving.
Buildings and vegetation are excluded from this visual topology, so paving
continues underneath enclosed buildings. Enclosed map vegetation and props marked
for clearing on construction are hidden while paved; buildings remain visible.
The paving mask is smoothed at its boundary to soften diagonal wall edges.

This is a client-only terrain layer: it retains terrain lighting and fog, leaves
roads visible, and does not change map tiles, pathfinding, saves, or simulation
state. It is independent of the master wall rebalance switch. The `[paving]`
section in `dev/wall_balance.cfg` (overridden by `user://wall_balance.cfg`) sets
`enabled`, the mounted terrain.ini `texture` name, and `tile_size` in map units.
The default uses `CobblestoneType2` from the player's original game files.
Restart the match after changing these settings. Missing textures report an
error instead of silently substituting another texture.
