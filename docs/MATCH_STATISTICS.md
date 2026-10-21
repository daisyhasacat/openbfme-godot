# Client-reported online match statistics

Online clients now send a detailed snapshot when they reach the score screen.
This exports the existing `ScoreKeeper` and victory/disconnect results; it does
not run a server simulation, alter scoring rules, or change retail replay bytes.
Older clients do not report these counters. A crash before the score screen
cannot supply that client's final snapshot, although another participant can
report the shared game state it observed. Historical relay logs do not contain
ready-made object statistics and are not backfilled by this change.

The report includes every occupied slot's result (win, loss, disconnected or
unfinished), actual faction, final score, units and buildings built/lost/destroyed,
heroes built/lost, fortresses built, resources earned/spent, spending split by
units/buildings/heroes, allied tribute, skill/power points, surviving unit/building
counts, time to first hero, player session length and elimination bonus. Object
template names retain a separate built/lost/destroyed/horde count, including mod
names. Each player also has up to 121 samples of the existing resource, score,
unit/building population and skill-point timeline.

## Provenance and limitations

`Common/ScoreKeeper.h` documents the RotWK sources, BFME donors, and disputed
binary provenance (S-001). Counter adders RW 0x79F0E1 / 0x79F303 / 0x79F486, the
per-template maps and the score-screen contract already exist in the port.
`ScoreScreenData::build` now copies those maps and classifies templates using
KindOf STRUCTURE/HERO/HORDE; classification is `unknown` when no template exists.
It never invents a faction or a building catalog. This is OpenBFME report
metadata, not a recovered retail network-statistics protocol.

The existing S-1061 scoring coverage stop still applies: some scoring callers,
veterancy counters, AI timeline money and retail ordering remain unported or
unverified. This feature exposes the counters the game currently computes,
without claiming complete retail statistic parity. Initial armies, summoned
objects, construction starts, replacements, cancellations, and destroyed objects
follow the existing ScoreKeeper's rules, rather than a new count of build clicks
or completed buildings. Session time is the player's existing defeat/end frame
(or the current frame) divided by five; it can differ between players. Timelines
are sampled, not a new per-frame server recording. No gameplay simulation state
is changed by copying the report; a native test asserts the state hash is intact.

## Transport and trust

`POST /v1/games/<16-hex-game-id>/statistics` requires the original participant's
lobby bearer session. The server captures slot/account ownership when START is
issued, independently of client-supplied names. Guests who did not play and
spectators cannot submit. There is one immutable report per human account/slot.
An identical retry returns `stored: true, unchanged: true`; a different body
returns 409 and leaves the original intact. The full roster, types, counters,
strings, sample order, bounds and schema are checked. Bodies are limited to
512 KiB, 512 template types per player, eight players, seven simulated days and
121 samples per player. Oversized or invalid reports are explicitly refused.

Every participant may report the whole scoreboard. For a player's counters the
selection is their own report, then the original host's, then another peer's.
Results with a decisive win/loss may come from later peers when an early leaver
only saw an unfinished state. Contradictory decisive results mark the match
`disputed`; both originals remain available to administrators. Disputed matches
count in the disputed total and do not add wins, losses or detailed totals.
`corroborated` means two or more clients reported consistent decisive outcomes
for each human slot; it does not certify identical counters or prevent collusion.
One unopposed report can count as a `reported` result. All summaries explicitly
say `server_verified: false`. Disconnects and unfinished results are separate
from losses. No Elo/ranking points or cheat-proof results are assigned.

Reports do not close the UDP game or mark it complete. Relay-log upload remains
the independent completion signal. Match-report receipts and counters can be
available before the final log arrives, and survive the live-room expiry.

## Persistence and privacy

The existing **Share my game results for statistics** setting controls uploads.
A client saves a report before sending it, with its original origin and bearer
session, into `user://online_statistics_outbox.json`. On POSIX the file is mode
0600. Windows uses the user profile's normal ACL. It is not on the player-sync
allowlist. Pending reports retry temporary failures with bounded backoff and are
reloaded on restart. Redirects are never followed. Authentication failures,
conflicts and invalid bodies stay private for recovery without a retry loop.
At most 32 reports are queued; a full queue preserves old data and explicitly
refuses new entries. Disabling sharing cancels sending and clears queued reports
and their credentials. It does not erase reports already stored on the server.

The server stores no bearer tokens in statistics. Node stores `match-statistics:`
rosters and `match-report:` manifests/parts in `lobby/meta/`; the Worker uses
Durable Object metadata with the same keys. Report bodies are chunked below
individual storage-value limits; the manifest is written last and missing
committed parts fail startup explicitly. Keep this metadata with the full data
backup; the existing replay-blob export endpoint does not include it. Aggregates
are derived once per match/report identity and never incremented on retries.

Stats belong to the lobby account ID, so renaming preserves them and making a
new guest identity starts a new history. Guest identities are still not
password-registered accounts. Public completed-game summaries omit account IDs,
report bodies and per-type/timeline detail; private matches remain admin-only.

## Viewing statistics

At `https://admin.secondage.dev/online/`, use the existing administrator passkey:

- **User management → Statistics** shows wins/losses, unresolved/disputed games,
  accumulated counters, recent games and per-template counts.
- **Room chat & controls → select a game** shows per-player counters and type
  breakdowns, sampled timelines and the original reports for comparison.
- **Completed games** shows receipt status and reported outcomes.

Authenticated players may read their own aggregate with
`GET /v1/players/me/statistics`. The existing passkey gateway signs
`GET /v1/admin/users/<encoded-account-id>/statistics` and the game-detail request
on the backend. No browser receives the signing key. Public management routes
remain blocked at nginx.

## Validation

```sh
node --test services/online/lobby/test/*.test.mjs services/online/test/*.test.mjs
python3 tools/net/test_match_statistics.py --godot /path/to/godot
ROTWK_INSTALL=/path/to/RotWK BFME2_INSTALL=/path/to/BFME2 \
  engine/build/openbfme_tests --test-case='end2 retail: the spend by kind*'
ROTWK_INSTALL=/path/to/RotWK BFME2_INSTALL=/path/to/BFME2 \
  godot --headless --path godot --script res://tests/match_statistics_export_test.gd
```

Server tests cover immutable/idempotent submissions, authentication and revoked
sessions, conflicting results, incomplete/disconnect outcomes, own-counter
precedence, persistence after room expiry/restart, partial-write retry, private
history, large HTTP bodies and Worker chunk restoration. The Godot test covers
durable retry, redirects, opt-out and file permissions. The native/retail export
tests cover unchanged simulation state and score-screen equality. UI testing
uses disposable local authentication and real exported counters; it does not
bypass production passkeys.
