# Buckland outpost capture approach

Boromir could receive a capture order and reach the outpost without raising its
flag. The ability approached the flag as an ordinary obstacle. Destination
adjustment stopped the unit outside StartAbilityRange, so repeated approaches
ended in the ability's unchanged timeout.

Open-BFME-2 `SpecialAbilityUpdate.cpp`, `rva00450AE9` (0x450AE9), explicitly calls
`ai->ignoreObstacle(target)` before the ability's object approach.
`AIMoveToStateOnEnter.cpp` (0x34C7BD) disables destination adjustment when the goal
object is the ignored obstacle. The rebuild already implemented that movement
branch but omitted the ability's ignore call. Restoring it allows the unit to
reach capture range; it does not change capture range, preparation time,
association distance or the abort timeout.

Regression: the installed retail skirmish map `map wor buckland`, using Boromir
and GondorFighterHorde from eight directions. Orders use the HUD context-command
path by clicking the outpost. Each case requires START_CAPTURE on the flag and
ownership transfer of both flag and outpost. Before the fix, five battalion
approaches failed and Boromir also reproduced the stationary-flag failure.
