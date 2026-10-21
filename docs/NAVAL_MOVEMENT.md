# Naval shoreline movement

Water-only locomotor sets (`WATER`, `DEEP_WATER`, or both) keep their attack search in their actual starting cell. The generic attack search's half-cell start offset can otherwise select the next cell and repeatedly create short paths onto land when no firing position is reachable. This is a requested gameplay fix; the retail ship-specific attack search remains unported (S-784).

Short forward attack probes also check the actual water at their endpoint and validate the route. The grid's water classification alone is insufficient: a shoreline cell can be classified as water when only one corner is submerged. Amphibious and air locomotor sets retain their existing behavior.

`test_ship1_ships.cpp` exercises both bombardment ship types on Forlindon with object and ground attacks toward an unreachable inland target, checking every frame for dry positions. It also verifies delivered bombardment damage against a shore building. The existing Evendim tests cover ordinary sailing, island navigation, transports, unloading, and the full ship/hover template inventory.
