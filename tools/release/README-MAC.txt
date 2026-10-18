This is our first Mac build: an unstable preview. Expect bugs.

OpenBFME v0.4.6 Mac Unstable Preview (Apple Silicon)

Apple Silicon Macs only (M1 or newer). There are no plans at this time to release for Intel Macs, because we have no Intel Mac hardware to test builds on, and because of stability issues on that platform.

Requires macOS 13 or later. Older supported macOS versions have not been
tested on this build machine. This preview contains the game and the OpenBFME Launcher. It has no
automatic updates and includes no retail game files. Keep both apps together.

GAME FILES
Supply your own complete, clean English BFME2 1.06 and Rise of the
Witch-king (RotWK) 2.01 installations. Keep them in separate folders,
outside OpenBFME.app. Do not merge the two installs or flatten their
subfolders. A copied Windows install or the corresponding complete
folders inside a CrossOver/Wine bottle can be used. Example layout:

  /Volumes/Games/BFME2/       <- BFME2 installation root
      *.big, game.dat, and the original subfolders (including apt/)
  /Volumes/Games/RotWK/       <- RotWK installation root
      *.big, game.dat, and the original subfolders (including apt/)

These are full installations, not just game.dat or a few archives. The
game checks the files; a missing, modified or wrong-version file is an
error. No Windows executable is run by this Mac app.

Open OpenBFME Launcher.app first. It detects remembered game folders and
the environment settings below. If none are set, click Play and use the
game's setup screen to Browse to each installation root and confirm.
Select the folder containing that game's
.big files, not the parent Games folder, an individual file, or the other
game's folder. Validation may take a minute.

The game's remembered settings are in:
  ~/Library/Application Support/Godot/app_userdata/OpenBFME/install-paths.cfg
To choose again, quit OpenBFME and remove that settings file. Alternatively,
create it as plain text, with absolute paths and no quotes around the values:
  ROTWK_INSTALL=/Volumes/Games/RotWK
  BFME2_INSTALL=/Volumes/Games/BFME2

For one Terminal launch, environment variables override the saved paths:
  ROTWK_INSTALL="$HOME/Games/RotWK" BFME2_INSTALL="$HOME/Games/BFME2" \
    "/path/to/OpenBFME.app/Contents/MacOS/OpenBFME"
Replace /path/to with the actual extracted folder. Terminal variables do
not automatically apply to an app opened later from Finder.

FIRST OPEN / GATEKEEPER
1. Verify the ZIP against its accompanying SHA256 file (Terminal:
   shasum -a 256 openbfme-v0.4.6-macos-arm64-unstable-preview.zip).
2. Extract the ZIP, then double-click OpenBFME Launcher.app in Finder.
3. If macOS blocks this unverified app, dismiss the alert. Open Apple menu
   > System Settings > Privacy & Security. Under Security, find the
   OpenBFME warning and click Open Anyway. Confirm Open and authenticate
   with an administrator account or Touch ID if asked.
4. On macOS versions that allow it, Control-click/right-click the blocked app
   > Open > Open is an alternative. Recent macOS may require step 3.
5. Repeat for OpenBFME.app if prompted. On later launches open the launcher
   and click Play (or open OpenBFME.app directly).

This app is not Developer ID signed or notarized. It has only a local
ad-hoc signature required for Apple Silicon execution; that is not Apple
approval and does not bypass Gatekeeper. Do not disable Gatekeeper
globally. Do not override an explicit malware or damaged-app warning;
verify the checksum, download again, and report the problem instead.

MAC PREVIEW UPDATES
Mac preview: updates are manual for now. Play starts only the game bundled
beside this launcher. The Mac package has no signed payload manifest and
is not signature-verified by the launcher. The published SHA256 is for
download-integrity checking, not an official release signature.
The launcher verifies the official Windows/Linux release manifest only to
check whether its version is newer. It links to the releases page; it never
downloads or installs a Windows/Linux update on this Mac. Download a new
complete Mac preview manually and keep its two apps together.

KNOWN ISSUES
- Unstable preview: rendering, menus, campaign and multiplayer may have bugs.
- Replays may not load.
- Options > Accept has previously left menu navigation unresponsive. If
  this happens, quit and relaunch the app; report the steps and version.
- Audio is unverified on our test Mac: CoreAudio was unavailable and Godot
  used its dummy audio driver. This does not establish audio failure on
  physical Macs.
- Retina fidelity and older macOS hardware remain unverified.
- Metal uses linear mipmap filtering for repeating textures to stay within
  the sampler limit; clamped textures retain anisotropic filtering.
- This is a separate preview, not a claim of complete v0.4.6 Mac support.

BUG REPORTS
https://github.com/Open-BFME/openbfme-godot/issues
Include the exact version, Mac model/chip, macOS version, steps to
reproduce, and the session log from:
  ~/Library/Application Support/Godot/app_userdata/OpenBFME/logs/
Review logs for private information before uploading. Never upload your
retail game files, game.dat, extracted assets, or full installation folders.

FILES
OpenBFME.app  native Apple Silicon game and engine
OpenBFME Launcher.app  preview launcher (keep beside OpenBFME.app)
README-MAC.txt  this guide
VERSION  exact source commit and build version
LICENSE and NOTICE  license and third-party notices
The ZIP checksum is distributed next to the download.
