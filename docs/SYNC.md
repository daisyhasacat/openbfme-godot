# Player sync

Open **Options → Sync → Player sync**. Enter the sync server URL and a username,
then press **Sync now**. Use the same username on each device. Settings persist
locally. Sync explicitly from the main menu; restart afterwards to reload the
profiles and heroes that the game keeps in memory.

Run the separate server with Python 3.11 or newer (no third-party packages):

```sh
python3 tools/player_sync/serve.py --data workspace/player-sync --port 8766
```

It listens on all IPv4 interfaces (`0.0.0.0`) by default. `--bind` can restrict
it to a chosen interface. Clients
use `http://SERVER-IP:8766`. The default client address is the LAN content host,
with a separate port. This service uses usernames without passwords for a trusted
LAN; a username selects storage, not authentication. Keep the data directory on
persistent storage and back it up. Stop the service before restoring a backup.
Run one server process per data directory.

Sync includes Skirmish.ini profiles, profile RealTimeStats.ini files, saves,
created heroes and their statistics in the actual Save folder, replays,
campaign progress, map edits and favorites, controller preferences, HUD layout,
and wall preferences. Explicit distinct hero folder overrides are supported.
Each selected game has its own namespace. Installation paths, mods, game assets,
map downloads, caches, logs, online session credentials, and device graphics
settings are excluded.

Missing local files are restored; missing server files are uploaded. A file
changed only locally replaces its previous server version. When both sides
changed, the server version is downloaded and the local version is preserved
under `user://sync-conflicts/<timestamp>/`. This includes first-sync collisions.
Files are synced whole: hero counters and profile records are not numerically
merged. Copy a preserved version back to its original location if desired, then
sync again. Deletions are deliberately not propagated.

Uploads use compare-and-swap version checks to reject concurrent changes. The
server retains content-addressed previous versions under each username's
`blobs/` folder; `manifest.json` points to current versions. Client downloads
verify SHA-256 before and after writing a temporary file, then replace the
original. Files are limited to 128 MiB each. Interrupted syncs can be retried;
successful files have their sync state saved individually.

Validation:

```sh
python3 -m unittest discover -s tools/player_sync -v
# Run only in an isolated Godot project named OpenBFMELanBulkTest:
OPENBFME_SYNC_TEST_URL=http://127.0.0.1:8766 godot --headless --path TEST_PROJECT --script res://tests/player_sync_test.gd
```
