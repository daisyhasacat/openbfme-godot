# Large-selection destination spacing

Normal move and attack-move commands with at least 16 eligible ground objects
assign distinct destinations on a compact grid around the click. A battalion
counts as one object. Smaller selections retain the existing destination
policy. Force moves, waypoints and dragged formations retain their existing
command paths.

This is a requested enhancement, not a reconstruction of RotWK's same-point
planning orders. The initial version used full, separated battalion envelopes;
the requested revision occupies about one-tenth of that area. It scales each
axis by `1/sqrt(10)` and allows battalion envelopes to overlap. Centres remain
at least one 10-unit pathfinder cell apart. Destination snapping can therefore
make small layouts slightly larger than exactly one tenth: the synthetic
100-unit layout now spans 90 by 90 instead of 270 by 270, and the 16-battalion
layout spans 120 by 120 instead of 360 by 360 (one ninth the area in both).

Base spacing still derives from the largest selected object's footprint and
bounding geometry. Map-edge margins retain room for that full footprint, while
the grid itself uses compact spacing. Assignment uses stable object ID order,
so repeated orders do not reshuffle slots as units change position.

During synchronous dispatch, the selected units and their battalion members
may share goal reservations with one another. Straight-route checks also ignore
position reservations of selected allies that are receiving the same order.
Reservations belonging to other
units, terrain footprints, wall checks and reachability tests still apply.
The scoped sharing list is restored before the frame proceeds; it is not
persistent simulation state and does not alter collision handling. Regression
checks verify ordinary reservation refusal after dispatch.

Existing goals of the selection are released before new ones are assigned.
A blocked or unreachable slot keeps its unit near its current position rather
than falling back to the common click. If a layout cannot fit, excess units
also retain their positions. Physical collision and formation behavior can
still affect the final resting arrangement; overlapping target envelopes do
not disable those systems.

Tests cover 100 distinct individual-unit goals, compact battalion goals,
repeated move/attack-move orders, edge clicks, sealed walls and reopened exits,
reservation-scope cleanup, and matching state hashes across two runs. The
focused movement, pathfinding, profiling and save-transfer suite passes 151
cases / 6,749 assertions.
Installed-data retail sweeps previously reported failures for `Sheep` and
`NeutralGollum_RingHero`; the original binary reproduced both. Their checks
were not changed or relaxed.

The opt-in `buckland_army_bench.gd` reports active destination count and bounds
for the mounted profile and saved base. The saved layout's known incomplete
replay remains documented in `LARGE_ARMY_PERFORMANCE.md`.


## Large-order response

During the same synchronous compact-order scope, a ground unit with a fully
passable straight line gets a two-node path immediately. This avoids both the
path-request queue and the recent-path cooldown for clear routes, including
rapid retargets. The line test still checks terrain, structure footprints,
movement restrictions, layers, unselected reservations and enemies; pinched
cells refuse the shortcut. A blocked line falls back to the existing budgeted
search. Once dispatch ends, ordinary collision and pathing rules apply again.
This is a requested enhancement, not a claim about retail command scheduling.

The native reproduction with 600 selected units on open ground initially gave
only 16 paths on the command frame, leaving 584 waiting. The revised code
gives all 600 a path on that frame. Of those, 495 were
queued; the remaining 89 had been refused by the 512-slot ring (511 usable
entries) while still being marked as waiting. Overflowed requests now retain
their destination and request type and retry on the next logic frame until a
slot opens. The existing serialized retry timer carries this state; queue
capacity, FIFO order, cell budget and snapshot formats are unchanged. Stopping
or replacing an order cancels its retry through normal movement state cleanup.
The overflow retry is an enhancement of the port's former lost-request behavior;
retail's caller-side recovery has not been established.


Regression coverage includes command-frame paths for 600 individual units,
64 battalions and their members, rapid attack-move retargets, a wall that forces
queueing, full-ring retry over multiple frames, and cancellation before retry.
The 50-unit arrival/budget regression now routes around a wall so it continues
to exercise A* and retain its queue/cell-budget assertions. Existing enclosure,
reservation cleanup, formation, determinism and save-transfer tests also pass.
