# Mordor walls from BFME2 1.09

The optional wall pack can include the 1.09 v3 Mordor wall system (original
source credits SEE/Nazgul). It adds the Mordor builder's outer hub, connecting
hubs and wall segments, gates, towers, and wall catapult emplacements. This is
an enhanced content addition, not RotWK 2.01 parity. Other 1.09 balance INIs and
changes to ordinary Mordor units are not imported.

Generate the local pack using the player's own 1.09 archives:

```sh
python3 tools/import_109_mordor_walls.py \
  --source "$BFME2_109/###BT2DC-v1.09v3.big" \
  --asset-source "$BFME2_109/#BT2DC-v1.09w.big" \
  --rotwk "$ROTWK_INSTALL" --bfme2 "$BFME2_INSTALL" \
  --base-pack /tmp/wall_towers_before_mordor.big
```

The base pack is the existing BFME2Lua wall tower pack, generated with
`tools/import_bfme2lua_wall_towers.py`. Preserve it before the merge. The importer
copies wall objects and their wall catapult child, resolves their 1.09 macros
locally, merges wall command/upgrade definitions into the existing retail-based
INIs, and follows referenced model/texture/portrait assets. It adds the hub
button to an unused builder command slot. The generated
`godot/mods/wall_towers.big` stays ignored and must never be committed.

The existing `new_towers` wall option enables this pack on the unmodded game;
other enabled mods suppress it to avoid overwriting their definitions. Mordor
starting-wall hub/gate defaults are included, and saved wall settings override
individual defaults without hiding newly added faction entries.

Validation: `godot/tests/mordor_walls_test.gd` loads the real world and models,
checks the builder and hub commands, previews a span, and builds actual wall
pieces. Run it with the local pack present and the retail install paths set.
