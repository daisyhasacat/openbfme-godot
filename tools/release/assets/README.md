# Application icon

The game project and macOS exports use the upstream OpenBFME ring artwork,
including the corrected transparent exterior from the macOS icon fix.
`tools/release/app_icon.py` generates `godot/assets/openbfme-icon.png` for the
project/editor and the ICNS files for macOS exports.

The older Second Age artwork remains in the repository but is no longer selected:

- `godot/assets/second-age.icns`: copied unchanged from the local Second Age app's `Contents/Resources/AppIcon.icns`.
- `godot/assets/second-age.png`: the corresponding original launcher artwork from the local `aio-lotr-launcher/frontend/assets/second-age.png`.

These files are application branding, not retail game data. The macOS exporter copies the ICNS into the app's Resources directory.
