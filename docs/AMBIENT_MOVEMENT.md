# Ambient movement

Harlindon's flying Crow and Dove_white_in_game objects use AIUpdateInterface
and CrowLocomotor, not AnimalAIUpdate. Their idle goal reaches
Locomotor::locomotorMaintainCurrentPosition every logic frame. The previous
WINGS branch only reported S-1750 and never moved them.

RotWK 2.01 0x5E74C1 steers airborne wings around the captured hold position:
CirclingRadius chooses the radius and direction, zero uses minimum speed /
maximum turn rate, and the aim is pi - pi/8 beyond the direction toward the
hold point. It invokes 0x5E740D (the wings wrapper of 0x5E6835) at minimum
speed. The hold's height worker and final transform write must also run, so a
bird placed on terrain can climb to PreferredHeight before circling. The
hold remains valid across updates; an explicit movement order invalidates it.
No render timer or render-side random draw drives movement. Existing logic
snapshots interpolate it between the five logic updates per second.

The WINGS branch and its height/transform tail are ported from that target
body. S-081 still applies to numeric parity. S-1750 remains for the unported
height/constant-calling tail of other appearances. Perched doves have no such
flying locomotor and remain where the map placed them.

Ground AnimalAIUpdate and civilian WanderAIUpdate previously ran only the
base AI class. AmbientAIUpdate now implements their ordinary idle wandering
from Open-BFME-2's AnimalAIUpdate.cpp and WanderAIUpdate.cpp, with the donor
module-data defaults. Animal wandering preserves the original position,
percentage roll, angle/distance draw order, authored radius and nearby
SALT_LICK center; ordinary civilian wandering chooses offsets within
WanderDistance. Active orders are preserved. Origin state participates in
native saves and the simulation hash.

This donor port is explicitly incomplete (S-5910): RotWK-specific parity,
animal enemy fleeing and special-layer return checks remain unverified or
unported. Destination validation uses the existing pathfinder. WildBeast
acquisition is refused with that report rather than treated as civilian
wandering. This does not add arbitrary movement to static scenery or replace
map-scripted cinematic paths.

Validation covers clockwise/counterclockwise flight, ground-to-air ascent,
perched birds, percentage-zero animals, civilian wandering, preserving active
orders, and all 41 flying birds plus seven perched doves on pure retail
Harlindon.

Retail save regressions cover the new AI subclass declaration, snapshot writes,
loading an earlier running game and saving the same bytes again. Enabling
animal wandering consumes its donor logic RNG draws and therefore changes
subsequent skirmish outcomes compared with the previously stationary animals.
