# Upstream integration, 2026-10-26

Merged Open-BFME/openbfme-godot `f45ea04a57` into the fork's water branch.
The fork's authored water, Lua authority, peace time, fortress construction,
local extended starts, tutorial state reporting and single-player cross-build
save policy are retained alongside upstream's target acquisition, scripting,
extended network maps, Cheat Mode and bounded recording readers.

Conflicts were reconciled at interface boundaries rather than accepting mixed
implementations. TargetFinder keeps the fork's team-run index and applies
upstream's player-layer mask without changing the original candidate order.
The dedicated upstream objectives screen serves the existing tutorial API;
opening/closing still reports the fork's single-player script state. Upstream
render-thread palette uploads and asset-pack texture lookup remain enabled.

The combined setup has additional fields. LANAPI protocol 5 and LAN START
versions 8 (legacy capacity) / 9 (extended capacity) reject older peers instead
of interpreting different layouts as identical. Replay format 12 includes
upstream build provenance and Cheat Mode plus fork peace time, Lua authority,
fortress construction and handicaps. Historical upstream formats 5..11 and
fork formats 5..9 are read by complete-layout validation. Version numbers
7..9 overlap between branches; each candidate starts from an empty result,
validates the full record, and cannot retain partial metadata from a failed
attempt. Synthetic regression fixtures cover both lineages and a combined
16-player setup. Existing fork-only 32-start maps remain local; network
capacity remains the explicitly tagged upstream 9..16-start contract.

Validation is scoped to the integration: full native/Godot build, native
water/network/replay/lobby/rules/save/target/script checks, changed game-script
loading, and Metal water normal/displacement regression. MP3 fixtures on this
machine require pinned lame 3.100; installed lame 4.0 cannot validate their
golden data. Those audio cases are not claimed as passing.

The merged stop register also uses the existing `closed` / `code` vocabulary
for S-260 / S-6010, removes the duplicated S-6101 row, and regenerates the
README count. The SCRIPT-4 threshold fixture explicitly clears initial rank
science grants before testing zero purchase points; runtime rank behavior is
unchanged.
