# Large-selection destination spacing

Normal move and attack-move commands with at least 16 eligible ground objects
now distribute their destinations over a near-square grid around the click.
A battalion counts as one object. The grid grows with the selection instead
of repeatedly searching the same small ring around a single destination.

This is an explicitly requested movement enhancement, not a reconstruction
of RotWK's same-point planning-order behavior. Smaller selections retain the
existing destination policy. Force moves, waypoints and dragged formations
continue to use their existing command paths.

Slot spacing uses the largest selected object's pathfinder footprint and
bounding-circle diameter, with an extra pathfinder cell of clearance, rounded
up to whole cells. This gives battalions room for their full geometry. Units
are assigned in current y/x order with object ID as the tie-breaker, while
commands still execute in the original selection order. No RNG is used.

The whole grid shifts inside the playable extent when the click is near an
edge. Existing goals of the affected selection are released before assigning
new ones, so repeated orders do not fight stale reservations. Each slot still
passes destination adjustment and a reachability check. A blocked or
unreachable slot keeps its unit near its current position rather than falling
back to the shared click. If the layout cannot fit on the map, excess units
also retain their current positions.

Regression checks cover 100 individual units with distinct destinations on a
270-by-270 grid; 16 battalions on a 360-by-360 grid; edge clicks; repeated move
and attack-move orders; sealed walls and reopening an exit; and matching
simulation hashes across two runs. The synthetic movement suite passes
78 cases / 3,102 assertions.

The opt-in `buckland_army_bench.gd` now reports unique active destinations and
their bounds. With the configured HD mod, 667 battalions and the user's
restored snow Buckland base, the first order tick reported 666 unique active
destinations for 667 battalions, spanning 3,130 by 2,990 world units. One
battalion had no active destination. This checks the actual mounted workload;
it does not establish that every battalion has arrived. The saved layout
still reported 233 completed entries and 212 replay errors, as documented in
`LARGE_ARMY_PERFORMANCE.md`.

Compiler and linker simulation checks pass. The full simulation audit remains
blocked by unavailable libclang and the existing unclassified FloatingText
source; its allowances were not changed.

With verified retail installations enabled, the wider movement run passed
77 cases and failed the existing all-template sweep on `Sheep` and
`NeutralGollum_RingHero` (15,597 assertions passed, two failed). The original
pre-change binary reproduces both failures with the same diagnostics; that
sweep calls individual `aiMoveToPosition` directly and does not use the group
arrival layout. These failures were not hidden or relaxed.
