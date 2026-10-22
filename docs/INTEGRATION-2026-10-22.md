# Fork and upstream integration, 2026-10-22

Local main first merged origin/main (f5e1ab119e) in ac6edb5, then integrated
upstream/main b12bb2c85e. Local ring delivery, treasure pickup commands, Gollum
visibility, and pathfinding movement fixes are retained.

Conflict resolutions preserve local loading caches / same-session quickload,
wall settings, texture swaps, Lua tooling, local online service features and
lighting. They also incorporate upstream user-map support, grouped APT drawing,
hero statistics retry behavior, rendering key caching, online enforcement and
reporting, and the complete SUMMON-1 CreateObject port. Map classification
accepts both the map folder context and the existing preview INI-load switch.

Enhanced AI purchasing now consumes upstream's AIStatCollector reserve rather
than the obsolete build-state reserve. The strategy purchase regression funds
a purchase from the reserve alone. Online route tests preserve both account
moderation and upstream install enforcement, and both match statistics and
player-report errors use their own types. Worker tests use the SQLite-backed
storage interface required by upstream. Completed-game writes are persisted
before mutating the live game, so a failed write can be retried.

Six PNG assets from the fork were accidentally replaced by LFS pointers even
though the repository disables LFS filtering. Each pointer's SHA-256 matched
the existing PNG exactly; those bytes are restored so the assets remain usable.

Validation: native `openbfme` and `openbfme_tests` targets build; Godot headless
editor import completes without script errors. All 152 lobby tests and all 58
focused gameplay integration tests (7,420 assertions) pass. The gameplay selection
covers AI reserve spending, ring delivery, treasure, pathfinding, module inventory,
summons, user maps, hero attributes and HUD redraw. The tooling selection passes
72 Python tests; its one failure is the pre-existing AppImage updater network-policy
finding. Both that updater and the policy scanner are unchanged from pre-merge main.
