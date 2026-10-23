# Optional fishing ships

This is user-requested OpenBFME gameplay, separate from retail behavior. It is
active only when an explicitly mounted mod provides `data/ini/openbfme_fishing.ini`.
Without that file the module registry and ordinary enter orders are unchanged.

Generate the authored definitions with:

```sh
python3 tools/create_fishing_mod.py workspace/fishing-mod
```

Launch OpenBFME with `--mod=<absolute path to workspace/fishing-mod>` after Godot's
`--` separator. It requires an extension rebuilt with this feature. The mod uses
BFME2's Elven transport hull, movement, animation and sound at runtime. It adds a
separate Fishing Ship to the good and evil port queues and the shipwright queue;
ordinary transports keep their normal behavior.

On map load the mod automatically places neutral fishing schools in deep water.
It requires both the ship pathfinder's deep-water classification and at least 20
world units of water depth, samples a 120-unit clearance around each spot, and
keeps schools at least 650 units apart. Maps without suitable water receive none.
The cap is 64 spots per map, including any authored spots. Locations are chosen
deterministically without consuming gameplay RNG, so clients agree. Parameters
are in the generated `data/ini/openbfme_fishing_placement.ini`. Remove that file
to disable automatic placement. Authored `OpenBFMEFishingSpot` objects are preserved
and reserve their surrounding area.
Select a fishing ship and hover over a spot: the enter cursor signals the fishing
interaction. Right-click orders the ship there. It stops beside the point, stays
visible and selectable, and earns 25 resources every five seconds, starting after
a full interval within 180 world units. Passing near fish without an order earns
nothing. A new AI order (including Stop or Move), death, capture, or destruction
of the point ends the task. Leaving range resets the interval; if displaced while
idle, issue fishing again. Fish spots are renewable; multiple ships can use one.

The fishing module's filter, radius, interval and income are mod data. Income
uses the existing economy multiplier and floating resource notification. The task,
owner and next payment frame are included in deterministic hashes and native
snapshots. The contextual order uses the existing two-object `MSG_ENTER` envelope
and rechecks the issuing player's selection; it never puts the ship inside the
fish. This extension is not supported by the original BFME2 executable.

The original models and textures are not copied into the repository or mod.

A playable demo places a fishing ship beside an automatically generated school on Evendim's lake:

```sh
godot --path godot res://scenes/fishing_viewer.tscn -- --mod=<absolute path to workspace/fishing-mod> --no-audio
```

Set `BFME2_INSTALL` to your installation before running the demo. It mounts BFME2
alone, uses the real selection/command HUD, and starts with right-click orders.
The fish spot uses a tropical fish school with a larger selectable footprint. The first
version reuses the animated enter cursor as the fishing interaction indicator.

Automatic-placement smoke check (with `BFME2_INSTALL` set):

```sh
godot --headless --path godot --script res://tests/fishing_auto_smoke.gd -- --mod=<absolute path to workspace/fishing-mod>
```

It checks the real Evendim water map and Tournament Udun dry map, including neutral
ownership, minimum depth, and spacing. The playable demo also verifies a raw
mouse order and an income payment at an automatically generated school.
