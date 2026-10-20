# Second game view

During desktop gameplay, click **Pop out view** near the upper left to open a
resizable native window. Move it to another monitor if desired. It renders the
same live world with the same player vision, without HUD or menus.

The camera starts to the right of the main view. Hold the left mouse button and
drag inside the second window to pan; use the wheel to move closer or farther
away. Its position is independent; its viewing direction follows the main
camera to preserve shared transparent-object ordering. Clicking the button again
focuses the existing window. Closing it leaves the game running. Leaving gameplay
or replacing the world closes it automatically.

While open, offscreen pose culling is disabled so units visible only in the
additional camera animate correctly alongside the caster picture-in-picture.
This can increase rendering and animation work. The previous setting is restored
when the window closes. This is a presentation feature; it starts no simulation
and sends no gameplay commands.

Run the lifecycle and camera-isolation check with:

```sh
godot --headless --path godot --script res://tests/second_view_test.gd
```

The headless check covers world sharing, camera isolation, zoom, native-window
configuration, cleanup on menu/world transitions, and culling restoration. Native
window placement and visual rendering require a desktop gameplay check.
