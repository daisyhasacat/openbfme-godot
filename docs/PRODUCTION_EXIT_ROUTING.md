# Production exit routing

Queue exits use the building's authored create point and natural rally point.
On Harlindon's west Men start, a hero leaving MenFortressCitadel was routed
through a side wall, roughly 75 units away from the door centreline, before
walking around to the correct front rally point. Builders share this path.

The unit begins inside the producer's obstacle. Without an obstacle exemption,
the grid search tunnels toward the nearest clear cell before pursuing its goal.

The OpenBFME doorway-routing fix identifies the producer in the exit command
and ignores that obstacle only for the first authored exit segment. Other
buildings and terrain remain obstacles. The exemption is cleared before later
segments (including the player's rally route), and when the exit state ends or
is interrupted. No new simulation state or save fields are introduced.

This is an explicit OpenBFME correction: RotWK's queue exit passes a null source
to the follow-exit-path command. It is not a claim of retail path parity.
