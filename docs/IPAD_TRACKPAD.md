# iPad trackpad input

The native iPad bridge sends trackpad input through the same Godot mouse and
gesture events as macOS. Direct finger touches retain the touch controls.
The reference is Godot's macOS `godot_content_view.mm`: `mouseDown`, `mouseUp`,
`processMouseEvent`, `scrollWheel`, `processPanEvent`, and `magnifyWithEvent`.

Existing Xcode exports must run the current `sync_ios_export.py` before building.
Their source Info.plist must enable `UIApplicationSupportsIndirectInputEvents`;
without it UIKit cannot distinguish pointer clicks from direct touches.

Device acceptance requires comparing these actions with the macOS build:

| Action | Expected game input |
| --- | --- |
| Move pointer | Mouse motion at the pointer position |
| Primary click / enabled tap to click | Left press and release |
| Click and drag | Left remains held throughout movement |
| Two-finger secondary click | Right press and release |
| Control-primary click | Right press and release, even if Control is released first |
| Press Control after a primary drag begins | Drag stays left until release |
| Double primary click | Only the second press has the double-click flag |
| Triple or secondary double click | No double-click flag, matching Godot macOS |
| Two-finger scroll | Pan gesture with macOS precise-scroll scale and direction |
| Pinch | Incremental magnify gesture |
| Shift / Option / Command with pointer input | Matching modifier flags |
| Cancel a click | Release held buttons without leaving a stuck drag |

Unit tests cover button transitions and click classification; they do not certify
UIKit delivery or physical trackpad behavior. Run this matrix on an iPad with a
Magic Keyboard before claiming device parity. System pointer speed, tap-to-click,
secondary-click settings, and gestures reserved by iPadOS remain OS controlled.
