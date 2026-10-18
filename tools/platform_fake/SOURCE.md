# The backend's own platform service, as the game's test double

`platform_service/` and `platform_fake.py` are copies of the OpenBFME platform backend's service and its dependency-free HTTP fake (the private
repository, `services/platform/src/platform_service/` and `tools/platform_fake.py`), **source commit a46f3082** ("Preserve exact lifetime personal statistics"; first copied at 192a0fca, refreshed 2026-10-15). They are our own code; the owner allowed the copy (2026-10-15). The only change to a copied file is `platform_fake.py`'s `ROOT` (the service
package sits beside it here, not under `services/platform/src`).

`fake_server.py` is this repository's: the backend's service behind its HTTP fake, with a movable clock and a `/__test/` surface (request log,
`advance`, injected failures, `down`) that the game's tests drive. The game's client (`engine/src/GameNetwork/PlatformApiClient`) and its tests run
against this, so the wire contract cannot drift unseen: `test_platform_contract.py` pins every shape the client depends on, and the C++ tests
(`engine/tests/test_onlinehub1_*.cpp`) play whole flows against it.

To update: copy the same files from a newer backend commit, change `ROOT` back, run `python3 -m pytest tools/platform_fake` and the engine's
`*test_onlinehub1*` tests; a failing pin names what the backend changed. Record the new commit here.

Not copied: the backend's own tests (they need boto3 / moto), its deploy files and the crash-report route.
