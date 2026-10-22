# Treasure placement and pickup input

Ordinary context clicks on `CRATE` objects issue a move to the crate position,
before the enemy-owner attack branch. This lets units walk into treasure from
an enemy lair. Force-attack mode retains its existing handling. The crate kind
and retail `UNATTACKABLE` / `NOT_AUTOACQUIRABLE` flags establish that these are
pickup objects; the explicit context-click priority is an adaptation to the
current command translator, not a verified RotWK context-command port.

`ObjectCreationList` now parses and applies SpreadFormation, its three radius
fields and object-ignore flags. The behavioral source is Open-BFME-2's
`GenericObjectCreationNuggetReallyCreate.cpp`, the spread branch at source line
1808: after object creation, sample the inner radius, search out to the outer
radius with FindPositionAround, fall back to the source position on failure,
and then apply the nugget offset. The shared position search supplies its own
random angle and terrain/object checks. The retail lair lists configure four
or two chests, inner radius 50..60, outer radius 80, and IgnoreAllObjects.

The donor asks for the highest layer. Our existing ground-only search is used
and S-530 reports the missing highest-layer lookup (S-1280/S-161); exact bridge
placement and full OCL binary parity remain incomplete. Invulnerability time,
fade and other OCL fields remain under S-530. No retail assets are added.

`treasure hud:*` checks actual right-click input on an enemy-owned chest.
`treasure retail:*` checks the mounted retail lair OCL makes four distinct
positions within its configured distance bounds. `ring delivery hud:*` also
checks that a ring carrier hovering its fortress gets the DeliverRing cursor;
the existing cursor loader reads and animates retail OneRing.ani.
