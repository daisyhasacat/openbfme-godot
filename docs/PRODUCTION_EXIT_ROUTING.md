# Production exit routing

Queue exits use the building's authored create point and natural rally point.
On Harlindon's west Men start, a hero leaving MenFortressCitadel was routed
through a side wall, roughly 75 units away from the door centreline, before
walking around to the correct front rally point. Builders share this path.

The unit begins inside the producer's obstacle. Without an obstacle exemption,
the grid search tunnels toward the nearest clear cell before pursuing its goal.

The OpenBFME doorway-routing fix identifies the producer in the exit command
and ignores that obstacle only for the first authored exit segment. That segment
uses the two-node continuous exit path (RW 0x666036), already used for battalion
members, rather than a grid-snapped route that can kink into the gate wall. Other
buildings and terrain remain obstacles. The exemption is cleared before later
segments (including the player's rally route), and when the exit state ends or
is interrupted. No new simulation state or save fields are introduced.

This is an explicit OpenBFME correction: RotWK's queue exit passes a null source
to the follow-exit-path command. It is not a claim of retail path parity.

Regression coverage constrains the first segment to its continuous centreline
within binary32 integration error, rather than allowing a 12-unit grid deviation.

A preceding allied recruit may still occupy or reserve the doorway. The scoped
exit-through-units check permits that ally without accepting a detour into a gate
wall; terrain, enemy occupants and other buildings still undergo passability checks.

## Allied clearing animation

Units asked to clear an exit use `AI_MOVE_OUT_OF_THE_WAY` (26), the same
state used when allies clear an ordinary movement path. Its installed path
already moved the unit, but the port omitted the internal move state's
animation-condition update and cleanup. The state now shares that condition
update with normal movement and clears MOVING, BACKING_UP, CLIMBING and
RAPPELLING on exit, including interruption.

Evidence: RotWK state 26's update at 0x749497 delegates to the internal move
update at 0x748E46 (target caveat S-001); matching BFME2
`AIStatesBfmeMoveOnEnters.cpp` also delegates to the internal move state.
This restores the missing animation bookkeeping without changing clearing
paths, speeds or destinations. A synthetic movement regression checks animated
displacement, arrival and interruption without requiring game assets.
