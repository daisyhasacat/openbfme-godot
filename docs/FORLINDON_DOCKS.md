# Forlindon dock capture correction

The installed RotWK map `maps/map wor forlindon/map wor forlindon.map` has three
ShipWright objects, each beside a separate CaptureFlag. Infantry target the flag:
ShipWright has `LINKED_TO_FLAG`, while CaptureFlag has `CAPTURABLE`.

The capture action checks for a linked building within 150 units of the flag,
measured between object centers in 3D. Both the context action's permission check
and the special ability's eventual capture lookup use that limit. A flag without
a linked building in range receives no capture order.

Before the correction, the map placements and observed results with a GondorFighterHorde were:

| Flag position (x, y, z) | Shipwright position (x, y, z) | Distance | Result |
| --- | --- | --- | --- |
| 245.870, 2668.652, 67.592 | 300.735, 2490.308, 54.181 | 187.1 | No capture order; both remain neutral |
| 1035.753, 825.210, 58.845 | 1143.997, 921.515, 54.328 | 145.0 | Flag and dock captured |
| 2662.660, 262.625, 73.629 | 2523.178, 311.429, 53.984 | 149.1 | Flag and dock captured |

The first pair is outside the capture lookup. The other two work through the
normal HUD right-click path. A builder without a capture power cannot capture
these flags; the tests use infantry with the capture ability.

Evidence:

- Open-BFME-2 `Code/GameEngine/Source/Common/RTS/ActionManagerSpecialPowerChecks.cpp`,
  reconstructed `canCaptureBuilding` at BFME2 0x81C79C: CAPTURABLE target,
  LINKED_TO_FLAG filter, `getClosestObject(..., 150.0f, 1, ...)`.
- Existing RotWK capture port: `CommandTranslator::canCaptureBuilding`
  (RW 0x82D0A1) and `SpecialAbilityUpdate`'s capture trigger (RW 0x8544F2).
  `PartitionManager.h` defines distance type 1 as `FROM_CENTER_3D`.
- `test_factc_sweep.cpp`, `Forlindon docks: ...`: loads the actual map, creates
  capture-capable infantry, right-clicks each dock's flag, and now asserts successful orders and ownership transfer for all three docks.

## Applied fix

The named fork correction `forlindon-dock-flag-v1` moves only the misplaced flag
50 world units toward its dock (y decreases from 2668.652 to 2618.652). Its height
is resampled from the terrain while preserving its authored height offset. The
resulting center distance is approximately 139.7 units, within the unchanged
150-unit capture lookup. All three flags and docks now transfer ownership in the
normal infantry right-click regression test.

The correction table is `engine/data/map_object_corrections.inc`; content names
and coordinates live there rather than in capture or simulation code. The shared
map-classification path applies a row only when the map name and original map-file
CRC32 match. The tested file has CRC32 `0x0b5173f0` after archive extraction. A
modified map with different bytes is left as authored. An exact matching file
whose expected placement cannot be uniquely found produces a loading error.
Application is reported as `[MAP-FIX forlindon-dock-flag-v1]` in classification
stops. No game archive is rewritten and no retail assets are committed.

This is a deliberate, named correction in this fork, not a claim about original
retail behavior. RotWK binary provenance still carries S-001, and the original
retail client was not tested. The engine compatibility ID hashes both the
correction code and its data table, so multiplayer peers need matching rebuilt
engines; old native saves/replays retain their existing compatibility checks.
