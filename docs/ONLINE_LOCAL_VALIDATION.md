# Local online validation — 2026-10-20

The macOS development client was tested against `services/online/server.mjs`, with an HTTP lobby on `127.0.0.1:8787` and a UDP relay on `127.0.0.1:7350`. Retail assets came from the configured installations; no game data or credentials are included in this report.

## Coverage

| Check | Result |
| --- | --- |
| Full lobby/server JavaScript suite | 80 passed: guest identity, room permissions, private codes, readiness, chat, long polling, ticket formats, reconnect, spectators, replay access, moderation, persistence and process failures |
| Engine online/platform/relay suite | 118 passed, 39,455 assertions; includes retail menu flows, Quick Match and tournaments against the platform test service, UDP play, disconnect/rejoin, pause and spectator paths |
| Initial Python/Godot online/platform suite | 32 passed: response limits, credential redaction, redirects, player counts, reconnect watchdog, platform contracts and result reporting |
| Post-fix Python/Godot checks | 20 passed for targeting, soak verdicts, failed-rejoin teardown and transport; the final combined replay/targeting/soak/platform/teardown run passed all 18 tests |
| Rendered online branding | Passed at 1024×768 and 2560×1440; captures inspected |
| Real two-client menu walkthrough | Both clients signed in, created/joined a room, used chat/map/rules/ready controls, checked premature-start refusal and played 150 frames with equal hashes and zero desyncs |
| Fresh-process online replay | Completed frame 150, compared 149 recorded hashes, zero mismatches and no lost commands |
| Impaired-network soak | Three 300-frame matches, 2/4/4 peers; 80 ms latency, 30 ms jitter, 30% loss, 10% duplication and 10% reordering; equal peer hashes, zero desyncs and three successful relay-log replay conversions/playbacks, each with three CRC comparisons and zero mismatches |
| Live service restart | Existing guest session remained valid; the completed game's downloaded START and relay log were byte-identical after restart; relay health recovered |
| Browser dashboard | Connected, displayed test-player presence, filtered player search, required administrator access for management, and reported no browser warnings/errors |
| Offline replay observer regression | `caster_vision_test.gd`: zero failures, including seek, step and recorded-hash checks |

Quick Match, tournaments, community statistics and result reporting use the separate platform API. Their automated tests use `tools/platform_fake`, the backend service's test copy. The local lobby/relay does not host those routes.

## Fixes found during validation

- The Godot walkthrough aimed at a button's visual centre, which can miss its transformed clickable shape. It now uses the device's existing hit-area search and refuses a covered/missing target.
- The Godot game-message bridge dropped `luaAuthoritySlot`. It now carries `lua_authority_slot` in both directions, validates its integer/range/human-slot constraints, and keeps the online game's authority through initial loading and playback.
- Network replay initialization restored offline starting-wall jobs that were absent from the recorded game. Playback now derives the network setting from the recording's human-slot mapping.
- `--replay-check` could remain stopped forever after a recorded-hash mismatch. It now exits with a failed verdict; a synthetic stopped-playback regression pins the exit behavior.
- Finished games remained in live/rejoin listings and could obtain new reconnect/spectator tickets. Finished games are now excluded and ticket requests are refused; players retain archived replay access, including after restart.
- The soak script compared BSD `wc` output as strings. Numeric comparisons now accept padded counts, with regression cases that still reject short runs, differing hashes, desyncs and replay mismatches.

## Repeat the checks

Use Node.js 18.2+, Python with pytest, the built extension/peer/tests/relay, and the configured `GODOT`, `ROTWK_INSTALL` and `BFME2_INSTALL` environment variables.

```sh
node --test services/online/lobby/test/*.test.mjs services/online/test/*.test.mjs
engine/build/openbfme_tests '--source-file=*test_online*,*test_platform*,*test_relay*'
python3 -m pytest tools/net/test_online_point.py tools/net/test_online2_soak.py
SOAK_GAMES='1 2 3' bash tools/net/online2_soak.sh engine/build workspace/online-validation-soak 300
```

Rebuild every peer and the extension before starting a run. Rebuilding during a match changes its profile identity and correctly causes a mismatch refusal.

The local server is supervised by the macOS user LaunchAgent `dev.secondage.openbfme-online`, using the existing ignored `services/online/.state/` directory. It runs at login. The client selects it through `OpenBFMEOnlineLobby` in its saved Options.ini; a backup of the original Options.ini was retained. Server state, signing keys, logs and local captures remain untracked.

## Limits

These checks cover one Mac and local sockets, plus synthetic link faults. They do not establish internet/NAT reachability, cross-OS compatibility, retail-client cross-play, long-duration production reliability or hosting at secondage.dev. Messenger/Connections and other explicitly unported retail features retain their existing reported limitations. The Linux AppImage was not rebuilt by this validation.

The standalone peer cannot convert a replay START for an installed custom map without that map in its own cache. The rendered client test used the installed Buckland Snow map; retail-map relay conversions were exercised by the soak.
