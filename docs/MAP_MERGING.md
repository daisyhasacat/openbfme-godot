# Visible-border map grids

`tools/maps/merge_maps.py` creates a native map by cropping each source to its
visible terrain rectangle and joining those rectangles in a grid. Input order
is top-left, top-right, then the next row below. Copies keep their orientation.
Only the outside of the finished grid retains the source's hidden border.

With the 2.02 map archive explicitly mounted, generate North Buckland's four
copies and install them into the map browser:

```sh
python3 tools/maps/merge_maps.py \
  --map-key 'maps/map mp buckland iii/map mp buckland iii.map' \
  --godot workspace/godot/Godot.app/Contents/MacOS/Godot \
  --columns 2 --rows 2 --name 'North Buckland 2x2' \
  --out 'workspace/generated-maps/north buckland 2x2/north buckland 2x2.map' \
  --install
```

Configure `ROTWK_INSTALL` and `BFME2_INSTALL` as for the game. The worker uses the
same verified installation and explicitly mounted mods. The source key is the
2.02 archive's internal name for North Buckland. Generated game assets belong
in ignored `workspace/`, never in Git. Existing outputs are refused.

A single `--source path.map` repeats a disk map. Repeating `--source` supplies
one map per tile. Compatible source variants must share the chunk dictionary,
terrain palette, visible dimensions, border and global settings. Scripts,
nonempty water/river/trigger/camera/link data, differing globals and unknown root
chunks are refused; arbitrary unrelated maps need additional reference and
palette remapping. Terrain raster layers and packed bitplanes are copied,
roads are clipped to visible edges, objects translated, and names, waypoint IDs,
player sides and starts made unique. Companion map.ini/map.str content is not
merged: use maps whose gameplay is supplied by the mounted profile.

Internal height seams are blended over 16 cells on either side, to connect
unequal edge heights. `--seam-cells 0` preserves exact source heights, which can
leave cliffs at the joins. This is geometric stitching, not a guarantee that
all joins are traversable by every unit.

The adjacent `.merge.json` records source hashes, dimensions, start and object
counts, and native validation/install results. Providing `--godot` validates
through the native parser; `--install` adds the validated map to Loaded maps.

North Buckland 2x2 has a visible 1100 by 1100 cells (11000 world units per side)
and 32 available starts, eight per tile. In the new skirmish browser select it,
open Players, and use **Fill players & spread starts** for 16 players distributed
across the four copies. Each player can be reassigned to any available start.

Matches above eight players currently support local skirmishes only. They have
an extended local colour palette. LAN/online packets, retail profiles and replay
formats retain eight slots. Recordings and saves are explicitly unavailable for
16-slot matches (see `docs/STOPS.md`).

Validation:

```sh
python3 -m unittest tools/maps/test_merge_maps.py
engine/build/openbfme_tests --test-case='local sixteen:*'
# With the merged map installed, pass its installation.key from the manifest:
Godot --headless --path godot --script res://tests/skirmish_sixteen_test.gd -- '<map key>'
```
