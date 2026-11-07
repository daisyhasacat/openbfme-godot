# Shift wall symmetry

While placing a wall span, hold Shift to mirror its destination across the
fortress's forward axis. A matching friendly hub on the opposite front corner
shows its own wall preview. Clicking sends the usual wall-span build command
from each hub. Release Shift to return to a single span.

This also works for matching hubs connected by one recorded wall span to those
front fortress hubs. It does not follow a second span. Recorded links retain
eligibility across gaps, consistent with the wall rebuilding system. Fortress
expansion occupants inherit membership through their foundation pad.

Both hubs must be completed, alive, owned by the same player and use the same
template. The partner must match the reflected position within one world unit.
Rotated fortresses use their own local forward axis. No nearby unrelated hub
is selected by proximity alone. Missing partners leave ordinary placement intact.

The two previews each use the construction planner, including terrain, snapping,
existing pieces, collision and build costs. Combined insufficient funds mark the
preview illegal. A paired click requires both plans to be legal and affordable;
otherwise it keeps placement active and reports the refusal. Each command still
passes the ordinary authoritative construction checks when executed.

This is an OpenBFME client placement convenience, not a claim about retail Shift
behavior. It adds no simulation command or wire-format fields.

Validation: `openbfme_wall_symmetry_tests` covers front/rear and rotated pairs,
missing/enemy/unfinished/unrelated partners, one-span versus two-span connections,
producer membership, preview toggling, command sources/destinations and combined
funds. Its synthetic coordinates are the expected result for this requested aid.
