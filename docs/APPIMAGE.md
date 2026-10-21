# Linux AppImage builds and updates

Bundled UI PNG assets under `godot/assets/` are inline Git binaries, exempt from the
repository's general LFS rules. Without that exception, an LFS-enabled runner
can clean the committed PNG bytes into pointers and report a dirty checkout,
causing strict packaging to stop. The workflow checks the tracked diff before
and after compilation so any future checkout mutation is named in the log;
release provenance checks remain enabled.

The **Linux AppImage** workflow (`.github/workflows/appimage.yml`) builds an
x86_64 AppImage on pull requests, pushes to `main` or `ci/**`, version tags, and
manual **Run workflow** requests. It needs no credentials or retail game files.

Download **OpenBFME-linux-x86_64-AppImage** from the run's **Artifacts** section.
Extract the artifact ZIP, then run:

```sh
sha256sum -c SHA256SUMS.txt
chmod +x OpenBFME-*-x86_64.AppImage
./OpenBFME-*-x86_64.AppImage
```

The player supplies their own supported game installs on first launch. If FUSE
is unavailable, start it with `APPIMAGE_EXTRACT_AND_RUN=1`. Graphics drivers and
system audio/display libraries remain host dependencies, as with the normal
Godot Linux package. The extension uses the project's pinned Godot Linux SDK and
its existing glibc compatibility checks rather than the runner's native compiler.

The workflow downloads checksum-pinned Godot 4.7.2, its official export templates,
appimagetool 1.9.1, and the 20251108 type-2 runtime. The explicit runtime prevents
appimagetool from fetching an unpinned runtime during packaging. Pins are in
`tools/release/appimage.py`.

`package.sh --no-launcher` first exports committed resources, verifies native
library provenance, audits the game-only payload, and checks its reproducibility.
The AppDir wraps that payload with an `AppRun`, desktop entry, and project icon.
`AppRun` locates the pack relative to itself and preserves command-line arguments
regardless of the caller's working directory. Launcher handover is disabled because the mounted image is read-only.
Settings → Updates uses the outer `APPIMAGE` path to download and replace the
image instead. CI artifacts remain available independently of the Second Age
update channel. These builds are not signed launcher release-channel packages.

Before upload, the workflow boots the actual AppImage in extraction mode from a
different directory, using `appimage_boot_test.gd` to check native extension
loading without retail files. This checks packaging and startup, not gameplay.
Artifacts include the AppImage, SHA-256 checksum, and boot log, retained 14 days.

Local staging/helper checks:

```sh
python3 -m pytest -q tools/release/test_appimage.py
```

The AppDir format and packaging command follow the upstream
[AppImage manual](https://docs.appimage.org/packaging-guide/manual.html) and
[appimagetool documentation](https://github.com/AppImage/appimagetool#usage).

## Steam Deck update channel

From the main menu choose **Settings → Updates → AppImage updates… → Update and
restart**. The app reads
`https://secondage.dev/downloads/openbfme/linux-x86_64/latest.json`, downloads its
immutable SHA-256-named AppImage, and verifies size, type-2 AppImage header and
SHA-256 before quitting. Downloads use HTTPS and trust Second Age's hosting; this
is not a separately signed release manifest. No GitHub account is required.

The external helper waits for the game to exit, verifies the hash again, saves
`<active AppImage>.previous`, atomically replaces the original filename, and
launches it with the original arguments. This preserves the Steam shortcut. It
clears old AppImage mount environment variables while retaining the Steam session.
An immediate failed launch restores and starts the previous file; later crashes
require restoring `.previous` manually. Save data and retail installations are
untouched. The image's directory must be writable and have room for both download
and backup. Cancellation or a download/verification failure leaves the active
image unchanged. Restart failures are logged in `<active AppImage>.update.log`.
After a power loss, remove a stale `.update-lock` directory only when no update is
running. Updates are disabled during games and lobbies.

## Build from a Mac through the Linux server

The pinned compiler SDK and AppImage tools are Linux x86_64 executables. macOS
cannot execute them natively. Use a clean checkout of the desired commit on an
x86_64 Linux builder, then invoke it from the Mac over SSH. For Second Age's small
server use one build job and low CPU/I/O priority; game assets are not needed:

```sh
ssh hailey@165.22.13.12 'cd ~/openbfme-appimage/source && JOBS=1 nice -n 15 ionice -c 3 tools/release/build_appimage.sh'
```

`build_appimage.sh` downloads checksum-pinned tools, builds the portable native
library, runs existing package provenance/audit/reproducibility checks, and boots
the real AppImage headlessly without retail data. Its output directory must be
fresh; override `OPENBFME_APPIMAGE_OUT` for a subsequent build. After reviewing
the successful boot log, publish only the image and manifest into the existing
nginx static site (no lobby/service restart):

```sh
sudo python3 tools/release/publish_appimage.py --image /absolute/path/OpenBFME-VERSION-x86_64.AppImage \
  --directory /var/www/secondage/site/downloads/openbfme/linux-x86_64 \
  --version VERSION --commit COMMIT
```

Publication copies a hash-named image before atomically switching `latest.json`.
Older immutable images remain available for clients already downloading them.
An existing AppImage without the Updates tab needs one manual download of a
build containing this updater. Add that file to Steam as a non-Steam game once.

Validation:

```sh
python3 tools/release/test_appimage_handover.py
python3 tools/release/test_publish_appimage.py
godot --headless --path godot --script res://tests/appimage_update_test.gd
```
