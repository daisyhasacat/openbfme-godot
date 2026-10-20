# Player sync

Open **Options → Sync → Player sync**. Enter the sync server URL and a username,
then press **Sync now**. Use the same username on each device. Settings persist
locally. Once configured with **Sync now**, changes upload automatically while
playing or browsing menus. The watcher checks every two seconds and waits for
two matching observations before uploading. New saves, profile edits, created
heroes and hero statistics are included. Offline failures retry after 30 seconds.

Automatic sync uploads local changes only; it never replaces files that the game
may have loaded in memory. Use **Sync now** in the main menu to download changes
from another device or resolve a conflict, then restart to reload profiles and
heroes. Conflicts remain untouched by automatic uploads and are reported in the
session log. Only one manual or automatic transfer runs at a time.

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

Profiles (`Skirmish.ini`, and `bfme1-Skirmish.ini` in BFME1 mode) hold every
profile in one file, so they are merged rather than replaced: every profile from
both versions is kept, and where both have the same setting the side that changed
since the last sync wins (this device when both did). This device's current
profile stays selected. Automatic sync uploads the merge without rewriting the
local file the game has loaded; **Sync now** also writes it locally (restart to
see the other device's profiles). A stale or empty profile list therefore cannot
remove profiles, and a profile deleted on one device returns from the others.

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
original. Files are limited to 128 MiB each. Sync runs while the app is open; closing the
app immediately after saving can leave the upload pending until the next run. Interrupted syncs can be retried;
successful files have their sync state saved individually.

Validation:

```sh
python3 -m unittest discover -s tools/player_sync -v
# Run only in an isolated Godot project named OpenBFMELanBulkTest:
OPENBFME_SYNC_TEST_URL=http://127.0.0.1:8766 godot --headless --path TEST_PROJECT --script res://tests/player_sync_test.gd
```
