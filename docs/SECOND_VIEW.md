# Second game view

During desktop gameplay, click **Pop out view** near the upper left to open a
resizable native window. Move it to another monitor if desired. It renders the
same live world with the same player vision. It has the native minimap,
Palantir/hero buttons, and projected building command buttons, without the main
window's adaptive HUD panels.

The camera starts to the right of the main view. Normal selection, orders,
building commands, keyboard shortcuts, minimap clicks, scrolling, and zoom work
in the focused window with the same mouse and camera preferences. Each window
keeps its own selection, command buttons, and camera position. Switching focus
restores that window's selected group before orders are sent. Rotating the
focused view also rotates the other view to preserve shared transparent-object
ordering. Escape opens the main game menu and focuses its window. Clicking the button again
focuses the existing window. Closing it leaves the game running. Leaving gameplay
or replacing the world closes it automatically.

While open, offscreen pose culling is disabled so units visible only in the
additional camera animate correctly alongside the caster picture-in-picture.
This can increase rendering and animation work. The previous setting is restored
when the window closes. Both input surfaces send commands to the same player
and simulation. Only the focused surface handles game input; switching focus
cancels held scrolling and drag selection. Scripted input restrictions apply to
both surfaces. Closing the second view preserves the main HUD's event handlers.

Run the lifecycle and camera-isolation check with:

```sh
godot --headless --path godot --script res://tests/second_view_test.gd
# With ROTWK_INSTALL and BFME2_INSTALL set:
godot --headless --path godot --script res://tests/second_view_native_test.gd
```

The checks cover world sharing, camera isolation, native keyboard dispatch,
independent selections and selected-group restoration, scripted input restriction, focus switching, cleanup on
menu/world transitions, and culling restoration. Native
window placement and visual rendering require a desktop gameplay check.
