# Linux AppImage CI

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
regardless of the caller's working directory. Updater handover is disabled because
the mounted image is read-only. Download a newer CI artifact to update; these
artifacts are not published or signed release-channel packages.

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
