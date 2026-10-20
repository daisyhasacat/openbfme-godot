# Connected paths

The previous map renderer emitted an independent rectangle for every road
pair. Even correctly authored connected paths had overlapping outside edges,
open inside corners and no rounded-corner artwork. Duplicate suppression also
ignored road type, deleting a differently textured segment at the same location.

The renderer now finds degree-two endpoints within each road type, independently
of endpoint direction and object ordering. Shallow bends and authored ANGLED
corners intersect their two offset edges. Other bends trim the adjoining legs
and insert the normal or tight curve pieces from the road texture atlas. The
normal radius is 1.5 road widths; tight is 0.5. Each piece covers up to 30 degrees.
Corners too short to trim fall back to the donor miter branch. Different road
types at the same coordinates are preserved.

## Evidence and adaptations

Donor: Open-BFME-1 matching `game/GameEngineDevice/Source/W3DDevice/GameClient/`
`W3DRoadBuffer.cpp`: `miter`, `insertCurveSegmentAt`, `loadCurve`, and
`loadFloat4PtSection`. The shallow-angle cutoff is 0.9 times 30 degrees;
the remaining-leg cutoff is 0.5. Curve UV origins are (4/512,255/512), or
(4/512,425/512) for tight corners. The donor's asymmetrical curve quads and
projected UVs are retained. Terrain columns retain the existing maximum-height
sampling across their width and presentation lift.

This is a donor-based adaptation, not verified RotWK byte equivalence. The
small renderer represents roads as line pairs rather than the donor's reordered
RoadSegment array: it discovers degree-two adjacency explicitly, matches the
appropriate opposite edges for reversed segments, and builds tangent points
and curve centers in 2D. Endpoint ANGLED/TIGHT flags are combined at a shared
node. The final curve piece uses the remaining turn angle. Available leg length
accounts for an earlier trim at the far end. These choices are renderer-only;
simulation, pathfinding and authoritative map data are unchanged.

S-036 remains open for tees, crosses, cross-type alpha joins, BFME2 stacking,
and exact curve parity. Such junctions retain their authored straight geometry;
they are not silently treated as degree-two bends.

## Validation

Road tests cover a hand-derived 90-degree miter in both segment directions,
normal/tight curve atlas selection and tangent placement, preservation of two
road types on the same line, straight-segment UVs, pairing and terrain height.
The Harlindon Metal capture builds 74 original road pairs, 23 mitered joins and
35 rounded joins (159 strips including curve pieces). The map still reports
its pre-existing missing SkyEnv.tga standing-water texture.

Final checks: 9 road/corpus tests passed (2,938,654 assertions), including all
pure RotWK/BFME2 map parses and their existing oracle checks. Metal overview
and close captures of Harlindon were inspected for connected edges and curved
artwork. Native Release build passed.
