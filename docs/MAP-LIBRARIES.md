# Map libraries

The map browser's **Map Library** tab scans a separate local folder for each game:

- BFME1: `~/Library/Application Support/BFMELauncher/Maps/Library/bfme1`
- RotWK: `~/Library/Application Support/BFMELauncher/Maps/Library/rotwk`

Put each map in its own subfolder with its `.map` file and any accompanying
`map.ini`, `map.str`, preview images, or other assets. Subfolders are scanned
recursively. The browser creates a missing library folder when opened.

Library maps can be browsed and previewed. Select **Install map** to add one to
**Loaded maps** before playing it. BFME1 imports are stored separately in
`user://bfme1-installed-maps`; RotWK imports use `user://installed-maps`. Switching
games therefore does not add the other game's imported maps to the playable list.
