# Private Windows installer built on macOS

Double-click `tools/release/Build Windows Installer.command`, or run:

```sh
python3 tools/release/windows_from_mac.py --check
python3 tools/release/windows_from_mac.py --output build/windows-installer-example
```

The workflow targets Windows 10/11 **x64**, building on Apple Silicon or Intel
macOS with the pinned universal macOS llvm-mingw UCRT toolchain. It uses the
existing Windows CMake toolchain and simulation floating-point flags. LuaJIT's
host generators run on the Mac; its static runtime is compiled for Windows.

Prerequisites on this Mac: Python 3, CMake, Ninja, curl, tar, NSIS (`makensis`),
Godot's stable editor, and an already-built native Mac GDExtension in
`godot/bin` for script imports. Godot defaults to its application-bundle binary;
use `--godot` to override. The Windows template version matches that editor.
Compiler downloads are pinned by SHA-256; official templates are verified
against their release SHA512-SUMS. The initial run needs network access.

## Content selection

The snapshot is taken when the command starts. By default it reads the current
Mac OpenBFME profile and remembers the installation folders from
`install-paths.cfg`; explicit `--rotwk` / `--bfme2` paths or their environment
variables override those folders. It includes:

- Entire BFME2 and RotWK install trees, including their loose assets.
- Enabled mods, in their configured order, and their texture-only flags.
  Texture-only packs disabled by the HD-texture switch are excluded.
- Installed custom maps, downloaded map libraries and map edits.
- The local launcher's RotWK map library, when using the default Mac profile.
- Saved wall rules and the selected BFME2 or RotWK game mode.
- Project assets and enabled bundled packs included in the Godot export.
  Files renamed `.disabled` are excluded; disabled mods are not enabled again.

Override the profile with `--userdata` or an additional RotWK map library with
`--map-library`. Selecting BFME1 or AOTR is currently rejected explicitly;
this workflow packages BFME2/RotWK, including their active mods.

Links are copied as content, not references to the Mac. Cyclic links, missing
active mods, unsupported Windows filenames and case-insensitive collisions fail
the package. Input files changing during archive creation also fail it. Source
installs and settings are not modified. Retail files stay in ignored local build
outputs; neither they nor the resulting installer belong in Git or public
release uploads.

## Installer and content paths

The command creates a new output directory containing:

- `OpenBFME-Setup.exe`
- `OpenBFME-content.tar`
- `README.txt`, including the content archive's SHA-256

Keep both installer files together when copying them to Windows. The archive is
a sidecar because the content can exceed the NSIS single-executable limit.
The per-user installer verifies its complete SHA-256, requires an empty target
directory, and extracts it with the 64-bit Windows `tar.exe`. It creates desktop
and Start-menu shortcuts directly to the installed **OpenBFME.exe**.
Allow space for the archive, installed content and user map-library copies.

The private export adds a content bootstrap before the normal Release autoload.
At startup the executable resolves `content/bfme2` and `content/rotwk` relative
to itself, supplies both environment and remembered paths, and initializes the
mod paths and maps from the snapshot. No Mac installation paths enter the
content profile. This also works when starting the executable directly.

Settings/saves use `%APPDATA%/OpenBFME Windows Bundle`, separate from ordinary
OpenBFME installations. Initialization runs on first launch and when the
installation location changes; subsequent launches preserve player settings
and mod selections. Uninstalling is manual for this private bundle: remove its
installation directory and shortcuts. Its separate settings/saves are retained.

Builds are incremental under `build/windows-from-mac`. The clickable command
uses a timestamped output directory; the Python command refuses to overwrite an
existing output. `--jobs` controls build concurrency. To repackage a completed
Windows export without recompiling:

```sh
python3 tools/release/windows_from_mac.py \
  --exported build/windows-from-mac/export \
  --output build/windows-installer-repacked
```

Repackaging takes a fresh content snapshot. Changes to exported scripts or the
bootstrap require a normal build/export first. `content-manifest.json` inside
the archive records installed file sizes and SHA-256 hashes; private PDBs are
excluded and retained beside the build DLL for debugging.

## Validation

```sh
python3 -m unittest tools/release/test_windows_from_mac.py
/Applications/Godot.app/Contents/MacOS/Godot --headless --path godot \
  --script "$PWD/tools/release/windows_installer/test_bootstrap.gd"
```

The tests compile a real NSIS installer using synthetic content, check mod
selection and map inclusion, reject unsafe names/cycles, verify linked content
is self-contained, and execute bootstrap path/configuration behavior in Godot.
A native Windows install/launch is a separate acceptance check; compiling a PE
on the Mac does not establish that it runs successfully on Windows or stays in
cross-platform simulation lockstep.

Local verification on October 24, 2026 completed the Mac-hosted cross-build and
Godot Windows export. The executable and DLL were identified as PE32+ x86-64;
the DLL/PDB pair and DLL package audit passed. Eight workflow tests, eleven PDB
regression tests, and the Godot bootstrap test passed. The private installer
archive's 6,615 file hashes were verified against its embedded manifest, including
the installed maps and launcher map-library snapshot. Windows install/launch
remains unverified on a native Windows machine.

## Upload to DigitalOcean Spaces

The double-click command now uploads each completed build to
`maclauncher/openbfme/windows-installers/<UTC timestamp>/`. Supply credentials
in the process environment as `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY`
(optional `AWS_SESSION_TOKEN`), or use `AWS_PROFILE`. Secrets are never passed
on command lines or written into the installer. A Finder-launched command may
not inherit exports from a Terminal; launch the command from that Terminal when
using shell environment credentials.

Optional destination environment variables:

- `OPENBFME_CDN_BUCKET` (default `maclauncher`)
- `OPENBFME_CDN_ENDPOINT` (default `https://sfo3.digitaloceanspaces.com`)
- `OPENBFME_CDN_URL` (default `https://maclauncher.sfo3.digitaloceanspaces.com`)
- `OPENBFME_CDN_RELEASE` (default UTC timestamp; must be a new release folder)

The Python build command uploads with `--upload`. Upload an already-built
installer without rebuilding:

```sh
python3 tools/release/publish_windows_installer.py build/windows-private-installer
```

Uploads keep the bucket's default access; no public ACL is applied automatically.
Use `--public` on the publisher or `--cdn-public` on the build command to make
all included assets publicly downloadable. Large sidecars use AWS CLI multipart
uploads. The uploader checks the README's payload checksum, refuses existing
release folders, verifies remote sizes and SHA-256 metadata, then publishes
`manifest.json` last. A local `cdn-upload.json` records the destination and
hashes. Failed uploads are retained for inspection and do not publish a
completion marker. It never changes the bucket policy or deletes other objects.

After a successful upload, the publisher verifies the remote completion manifest,
then deletes the three unchanged local installer artifacts (the content TAR,
setup EXE and README). It retains `cdn-upload.json`, unrelated files, and the
incremental build/export cache. Upload or verification failures retain local
artifacts. Use the standalone publisher's `--keep-local` option when a local
installer copy is needed. Local files changed after uploading are retained.

The Mac double-click workflow now uses `--exclude-movies --exclude-rotwk`:
it packages BFME2 only and selects `bfme2-106` at launch. RotWK installation
files, the launcher's RotWK map library and RotWK-specific bundled packs are
omitted. Loose movie files/directories are excluded; BIG archives are preserved
byte-for-byte. The bootstrap accepts the explicit `installs` inventory and
clears the RotWK environment override when only BFME2 is included. The Python
workflow accepts the same flags; omit them to build the full content bundle.

## Portable ZIP using existing installations

For a ZIP with just the Windows runtime and no installer or game assets:

```sh
python3 tools/release/windows_existing_zip.py --output build/windows-portable/OpenBFME-Windows.zip
```

Extract the whole `OpenBFME` folder and run `OpenBFME.exe`. This export omits
the private bundled-content bootstrap and uses the standard OpenBFME profile
and first-run discovery: Windows registry entries and common install locations,
followed by confirmation or manual selection of BFME2 1.06 and RotWK 2.01 folders.
It does not require a `content` folder beside the executable. It excludes the
optional local RotWK map/tower packs and contains only the EXE, PCK, DLL and
instructions. ZIP integrity is checked before completing the output.

First-run setup accepts BFME2 1.06 without RotWK. Leave the optional RotWK field
empty to validate and mount BFME2 alone; setup remembers the BFME2 folder and
selects `bfme2-106` for later launches. If a RotWK folder is selected, both game
folders are validated and the RotWK mount remains unchanged. A bad explicitly
selected RotWK installation is reported rather than silently ignored.
