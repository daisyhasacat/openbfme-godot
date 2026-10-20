# Building placement guides

Hold **Shift** while placing a building to show its snap grid, nearby foundation
outlines and green alignment guides. A green line connects the placement ghost
to the nearest matching building owned by the local player in each aligned
direction, within 1,500 map units. Centers must share a row, column or either grid diagonal to within
one map unit of perpendicular distance. Each of the eight directions uses only
the nearest match. Different building types do not produce matching guides.

**G** rotates the placement grid by 45 degrees; the guides follow those rotated
axes. The controller's snap-grid mode provides the same guides. Releasing grid
placement or cancelling placement hides them. The lines follow terrain height
and are visual aids only; they do not change placement legality or simulation.

`godot/tests/snap_structure_footprints_test.gd` checks straight and diagonal alignment,
misalignment, rotated alignment and hiding when snap mode ends, with an optional
`--shot=<png>` render capture.
