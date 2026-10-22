# Object rendering and lifetime audit

The presentation instancer now omits instances outside both camera frusta from
MultiMesh draw buffers, in addition to deferring their poses. Camera visibility
is refreshed for static poses too; disabling culling restores their submission.
PiP's additional frustum keeps objects visible in either camera current.

The bound is the existing S-1730 model sphere (bind-pose pivots plus mesh extents,
with animation margin), not a proven envelope of every mod animation. Very large
animation displacements may require a larger bound. Off-screen casters are also
excluded, so their shadows are not submitted independently of their models.

Live hidden enemies/buildings already lose their render instances through
GameWorld::refresh_views and Drawable::presentedLook. Previously discovered
immobile objects remain drawn with fog lighting. Client-only scenery now uses
its presented shroud cell to exclude undiscovered objects independently of
paving opacity, and static scenery participates in camera culling.

Normal death animations retain their drawable until its lifecycle finishes.
GameWorld::remove_view then removes its instances. Removal drops the model's
live instance id, releases the pose and auxiliary vector storage, and returns
the palette block for reuse. Stable id tombstones and shared model/material
caches remain intentionally allocated. Fully sunk toppled trees now remove
their instances instead of submitting zero-scale geometry.

Zero-opacity and shrouded instances skip pose evaluation until visible again;
on-demand bone queries continue to support attachments. Simulation updates are
not culled. Shared buffer capacity, culling bookkeeping and shared rendering
passes still have costs; this change does not promise zero total frame cost or
an FPS improvement without a representative GPU capture.

Validation: build the extension, run godot/tests/perf3_cull_test.gd with mounted
retail assets, and run the shroud, fog particle, death and perf3 C++ tests.
The Godot regression verifies camera re-entry, frozen and explicit poses, PiP,
zero draw submission outside the cameras, hidden pose suppression, shroud
restore, and idempotent removal with palette release.

Verified locally: Release extension build; PERF3 CULL PASS headless and Forward+;
NEUTRAL DECORATIONS RETAIL PASS; 29 targeted C++ tests / 5,159 assertions passed.
These checks establish submission and lifecycle behavior, not an FPS benchmark.
