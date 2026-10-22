# Pathfinding comparison with Open-BFME-2

This pass compares the ground search, cell movement probes, path shortening,
and their object adapters with the recovered BFME2 implementation. It does
not replace established RotWK rules with BFME2 rules, and does not establish
complete binary or replay parity.

## Corrections implemented

The common source is Open-BFME-2's
`Code/GameEngine/Source/GameLogic/AI/PathfinderCheckForMovement.cpp`,
`checkCellUnits`, with full and incremental consumers in
`GameLogic/Pathfinder/pathfinder.cpp`. Corresponding RotWK instructions were
independently examined using the installed executable. All target findings
retain the disputed-binary provenance caveat in PLAN rule 9 / S-001; none is
a claim of a clean 2.01 oracle. No executable bytes are checked in.

| Branch | Previous behavior | Corrected behavior and evidence |
| --- | --- | --- |
| Transient unit probes | Considered moving allies, but skipped moving enemies unless the separate enemy flag was set | The transient boolean requests occupants of either relationship. BFME2 `info.m_11`; RW 0x6EB8FF..0x6EB934. The later flags and target checks still decide whether an enemy blocks. |
| Submarine movement | Fell through to normal ally/enemy blockers | SUBMARINE (kind 187) clears the goal-list hint and skips the unit blocker. Terrain checks still run. BFME2 kind 0xBB; RW 0x6EB99C..0x6EB9A9. The bit is resolved by name in both object adapters. |
| Attacker target exceptions | `ObjectPathfindAdapter::ignoresAsTarget` was a constant-false stub | Check the machine goal, then current victim. Compare the occupant and its immediate container against that target and its immediate container. BFME2 goal/current-victim branch; RW 0x6EBA09..0x6EBA5E. This permits approach through the targeted battalion's members while unrelated enemies still block. |

The submarine flag is included in the search memo so ordinary searches and
cached searches use the same rules. These corrections affect authoritative
movement; existing saves require no new fields, but mixed old/new executable
versions should not be used for deterministic multiplayer or replay comparison.

## Areas inspected and retained

The current A* already implements target-specific first-discovery behavior
(no reopening/decrease-key), 10/14 cell heuristics, turn and occupant costs,
and terrain/hierarchical penalties. The forward/backward path optimizer
already mirrors the donor's split traversal and absolute 0.9 direction test
(`PathOptimizeRva00364551.cpp`). Those rules were retained rather than swapped
for a generic A* or a different smoother. This was a source comparison of
these branches, not an exhaustive proof of every pathfinding function.

## Remaining work, in priority order

1. Rotated LARGE_RECTANGLE_PATHFIND footprint/reservation polygon fills
   (S-164). Current axis-aligned approximations affect battalion and large-unit
   clearance and destinations. Decode and port the shared rasterizer before
   changing its callers.
2. Complete-path rebuild from the unit's facing and the closest-cell third
   predicate (S-163/S-165). The donor now exposes arc helpers, but they require
   the correct caller/options and RotWK mapping; changing only the visible
   curve would invent behavior.
3. Bridge/wall/ladder layers, portals and intermediate routes (S-161/S-162).
   These require map classification, transitions and search together.
4. Incremental zone updates and scheduling details (S-162/S-164), then numeric
   verification and frame-counted retail/replay comparison (S-167/S-001).

The existing runtime stops continue to report these limits. No broad parity
claim is removed by this pass.

## Validation

`pathfind donor movement:*` checks moving enemies in full and incremental
probes, ally/ignore-ID behavior, and the submarine exception while retaining
terrain penalties. `pathfind donor retail:*` uses real infantry and battalions
and checks both machine-goal and current-victim paths, parent/member/sibling
matching, unrelated enemies, and movement probes through the targeted group.
The existing pathfinding, movement, attack, formation and retail-map suites
provide regression coverage.
