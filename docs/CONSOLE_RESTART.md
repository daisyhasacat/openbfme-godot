# Console restart and building reconstruction

The in-game console's **Restart game** tool records buildings for every occupied
player start before closing the app. It uses `recordBuildings` and the existing
JSON building-layout storage, including castle dependencies, expansion plots,
wall links, upgrades and building levels. Snapshot failure cancels the restart
and reports the error in the console.

On startup the saved map, seed and player slots load first. A one-shot handoff
then runs the layouts through `replayBuildings`, one owner at a time, resolving
owners by start position. Uber mode is restored before reconstruction if it was
enabled at restart, including its fog-of-war removal. Existing starting buildings are reused. Reconstruction
uses the normal fast building replay path, so it can take a few seconds rather
than appearing all at once. Each owner receives temporary construction funds;
their recorded money is restored when their layout finishes. Replay failures
are printed instead of being silently skipped.

The snapshot uses the reserved layout name `console-restart-buildings` and
`console-restart-buildings-slot-N` under `user://building_layouts/`. These files
remain available for inspection or manual replay. The startup handoff is consumed
before loading so a failed load cannot cause a restart loop.

This applies to the console tool in local skirmishes. Campaigns, network games
and replay viewing retain their return-to-menu behavior. The ordinary pause
menu's Restart still starts the original match over. This preserves buildings,
not units, ongoing battles, construction progress or the complete simulation.

Checks: `console_restart_test.gd`, `restart_buildings_test.gd`,
`building_layout_test.gd`, and installed-data `restart_buildings_native_test.gd`.
