# Forlindon dock capture investigation

The installed RotWK map `maps/map wor forlindon/map wor forlindon.map` has three
ShipWright objects, each beside a separate CaptureFlag. Infantry target the flag:
ShipWright has `LINKED_TO_FLAG`, while CaptureFlag has `CAPTURABLE`.

The capture action checks for a linked building within 150 units of the flag,
measured between object centers in 3D. Both the context action's permission check
and the special ability's eventual capture lookup use that limit. A flag without
a linked building in range receives no capture order.

The map placements and observed results with a GondorFighterHorde are:

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
  capture-capable infantry, right-clicks each dock's flag, and asserts the order
  and ownership results above after allowing the capture to complete.

This establishes the cause in the port with the installed map data. It does not
establish that the original retail executable exhibits the same map issue:
RotWK binary provenance still carries S-001, and this investigation did not run
an original retail client. A retail capture attempt at the out-of-range flag is
the next useful comparison before changing the range or map data.
