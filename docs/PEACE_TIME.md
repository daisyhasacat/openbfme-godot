# Peace time

Peace Time is an OpenBFME game rule, separate from the original game's ten rule values. Choose **Off**, **5**, **10**, **15**, or **20 minutes** in the skirmish Rules tab or the native map browser's OpenBFME extensions. Off is the default. The host selects the rule for a network game; clients receive the same value.

The interval starts at simulation frame zero and ends exactly at the selected minute (5 logic frames per second). Pausing game time pauses this interval. During it, the skirmish AI keeps updating its base expansion, economy, dozers and unit production. Its tactical brain, combat-team dispatch and automatic ability casts wait until expiry. Computer-owned units and towers also refuse player-owned targets, including other computers, so incidental enemy contact cannot start an attack. Human orders and neutral targets/resources retain their normal behavior. This is an AI restraint, not invulnerability for human players.

The duration travels through the lobby, new-game setup and replay header and contributes to the simulation state hash when enabled. OpenBFME replay format 7 includes it; format 5 and 6 headers remain readable with peace time Off. Save/resume uses the recorded start data and simulation frame, so it does not restart the interval.

Synthetic coverage checks timer boundaries for every duration, paused time, setup validation and serialization, legacy headers, targeting and the AI update path. A retail-gated test checks that an AI builds and trains while peace remains active. That integration test requires the user's original game archives.
