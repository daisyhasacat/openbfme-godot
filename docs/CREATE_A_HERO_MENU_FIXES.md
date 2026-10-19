# Create-a-Hero menu corrections

Donor references are relative to Open-BFME-2:

- `Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Apt/AptMyHero.cpp`
  refreshes `UpdateHeroBaseAttributes` after edits. The shell now invokes the
  movie update from its edited-hero notification, including saved-hero changes.
- `Code/GameEngine/Source/GameClient/GUI/AptPlayer.cpp`, `FindRenderImage`,
  tries image records and then the mapped-image collection. The device now
  follows both lookups for `_imageMap`; this restores VerticalScrollBar's
  button and thumb images without changing the movie or its input behavior.
- `Code/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp`,
  `setModelState`, calls `adjustAnimation` after changing model state. Our split
  model/animation tables previously retained a shared idle state across a new
  skeleton or ModelAnimationPrefix. This left an Elf or Wizard preview playing
  the Captain's idle clip until another animation was picked. A binding change
  now forces animation selection to resolve the new prefix immediately.
- `AptMyHeroRefreshCamera.cpp` supplies ViewInfo's pitch, zoom, floor, distance,
  shift and horizontal field of view. The preview now keeps horizontal FOV,
  reads the mounted map's camera offset for zoom distance, and keeps Floor
  separate from Zoom. The Godot eye placement and close-up pitch conversion
  remain explicitly inferred under S-1405; this does not establish retail
  pixel parity. Preview placement still needs comparison with a retail capture.

The hero-list event correction uses the existing GadgetListBox contract:
GLM_SELECTED data1 is the gadget, data2 is the selected row; the callback's
window is the receiving owner. The regression changes the selected row through
the real gadget and checks the displayed record.

Validation: native Create-a-Hero and draw-runtime regressions; scripted builder
flow across classes, appearance, powers and save; windowed screenshots of the
full-body preview, Powers view and restored scrollbar artwork. Retail bytes and
screenshots are not committed.
