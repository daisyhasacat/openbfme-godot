# Strategy-driven native skirmish AI

Source AI rules are now the default. Enable **Enhanced computer AI** in OpenBFME options (`OpenBFMEEnhancedAI = yes`) for the policies below. The choice is independent of Uber performance mode, is carried in game setup and replay headers, and participates in the peer identity and settings hash. Campaign saves keep source AI rules. Missing donor families remain explicitly reported; source mode is not a full retail parity claim.

This branch implements an enhanced OpenBFME policy, explicitly requested by the
user. It builds on the existing wall-balance, siege-production, breach-gathering
and paid-upgrade changes. It does not claim to reproduce the retail AI or make
multiplayer/replay compatibility claims.

## Research translated into behavior

Design reference: **BFME2 Competitive Playbook - HeavenGames Edition**, 53 pages,
created in the research chat. Its strategic principles are translated into engine
data and ordinary commands; no retail faction names, build-order strings or
archive contents are embedded in simulation code.

| Playbook principle | Implementation |
| --- | --- |
| Scout the composition, then buy the missing counter | `AIStrategy::assess` counts only visible, detected enemy battalions; `productionNeed` requests pikes against cavalry/monsters, ranged units against infantry/flyers, swords against pikes, and limited cavalry against unprotected archers. |
| Protect ranged damage | Two ranged battalions create demand for at least one pike; local archers prioritize reachable combat units over structures, while pikes prefer mounted/monster threats. |
| Do not feed cavalry into pikes | A close pike triggers a normal pathfinding move away instead of an attack order. |
| Preserve expensive survivors | Heroes, cavalry and flyers at 30% health or below retreat when threatened; a timed recovery reservation removes them from tactics and recruitment until 75% health or 15 seconds. This does not grant healing. |
| Survival before optional technology | A visible home threat suspends optional upgrade savings. A requested counter can be trained even while saving for upgrades. Request completion bookkeeping continues during savings. |
| Siege needs protected ownership | Existing breach engines and escorts retain their reservations and orders. Recovering units cannot be recruited as new escorts. |

The role quotas are bounded (one mobile response, up to four pike/ranged counters)
and count living units plus unfinished production requests. Once satisfied, the
existing army composition, hero, siege and economy systems continue normally.
Counter production uses existing ArmyDefinition candidates, real factories,
normal paid queues, command points and prerequisites. Missing producers reuse the
existing legal construction request path. There are no free units or resource
injections. Visible allied units and contained horde members are not counted as
hostile battalions.

## Integration and deterministic state

`AIStrategy.cpp` contains the new policy. Production calls it from
`AIBaseBuilder.cpp`; the tactical pass runs once per simulation second after
fortification reservations and before team recruitment. Decisions iterate stable
object order and break targeting ties by object ID. No new RNG is used. Recovery
ownership is included in the brain state hash, and all commands use existing AI
interfaces. Disabled tactical/team systems, script-controlled players and peace
time retain their existing authority.

The policy uses KindOf roles rather than a fixed numerical damage-counter table.
It therefore does not yet distinguish all mounted hero armor variations, perform
cost-normalized duel simulation, or infer every mod's untagged role. Those are
limits of the implemented policy, not silently invented combat statistics.

## Strategy sources

- [HeavenGames Men guide - Blatant7](https://bfme2.heavengames.com/strategy/Overviews/106_Men/)
- [HeavenGames Elves guide - Blatant7](https://bfme2.heavengames.com/strategy/Overviews/106_Elves/)
- [HeavenGames Isengard guide - Blatant7](https://bfme2.heavengames.com/strategy/Overviews/106_isengard/)
- [HeavenGames Mordor guide - Catabre](https://bfme2.heavengames.com/strategy/Overviews/106_Mordor/)
- [HeavenGames hero-use guide - Blatant7](https://bfme2.heavengames.com/strategy/Other_Guides/General_hero_guide/)
- GameReplays-formatted hero and cavalry guides preserved in
  [ValheruGR/BFME2](https://github.com/ValheruGR/BFME2/tree/master/1.09v3/Patch%201.09v3%20Notes/guides).

## Still outside this implementation

Faction-specific fixed openings, active creep/scout missions, tunnel transport
planning, deliberate cavalry body-block formations, per-hero ability combinations,
weapon-mode optimization, and a full protected-siege movement formation remain
future work. Existing economy and special-power systems still run. This branch
adds adaptive production and local combat decisions rather than replacing every
subsystem with an unvalidated monolith.

## Validation

Synthetic regression coverage is in `engine/tests/test_ai_strategy.cpp`. Run it
alongside fortification, skirmish production, tactics, visibility and combat tests.
Live games require the user's archives. Integration on main preserves the existing
wall/siege and reinforcement-wave changes.
