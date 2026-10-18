# Adaptive in-game HUD

The **HUD** button at the upper left, or **F10**, cycles between Classic, Modern,
and Hidden. Modern is the default on iOS. The preference is saved locally in
`user://adaptive-hud.cfg`. Hidden leaves only the restore button.

Modern keeps the minimap at bottom left and the selection's commands visible at
bottom right. Construction has its own panel above the commands: builder orders
such as Stop and Extinguish, plus building upgrades and hero recruitment, stay
in Commands. Native submenu and Back buttons navigate the selected building's
command ranges, including fortress menus. Fortress construction keeps the normal
radial commands alongside its separate builder menu. A button exposed in both the Palantir
arc and side bar appears once, identified by its native command-button name.

Powers, Heroes, Builders, Map controls and System open from the upper-right
shortcuts. These auxiliary panels leave commands and construction visible.
Portrait and landscape layouts use safe-area insets on iOS. Tiles retain native
icons, prices, queue counts, availability and progress indicators.

- **Powers:** native powers and a purchase panel with localized names, icons,
  availability and point costs. Leaving the purchase panel closes the native store
  and submits its pending purchases, as in the original interface.
- **Heroes / Builders:** select an owned object directly.
- **Placement:** reposition, lock, rotate, build, or cancel using touch buttons.
- **System:** army selection, deselect, console, HUD mode, and access to the
  existing pause/options screens. Those screens retain their current implementation.
- **Map:** a resizable terrain minimap with fog, visible unit dots, and the camera
  footprint. Tap/drag to move the camera; enable Orders to send the current selection.

For controllers, hold the existing palette trigger (RT, or Tab in keyboard
controller mode). D-pad left/right selects a tile, up/down changes focus between Commands, Construction and the auxiliary sections, A
activates, B cancels commands/queues, and Y closes the panel. In Map, the D-pad moves
its crosshair, A looks, B issues an order, and Y exits. World/camera controller
bindings continue to use the existing controller bridge. The replacement suppresses
the old controller palette while active.

Native menu graphics and the old radar hit area are disabled in Modern/Hidden;
selection, simulation and orders remain live. Overlay rectangles capture only their
own input, including touches dragged outside a panel, rather than blocking a large
rectangle across the battlefield. Radar orders use the same native command path as
the classic radar; the new UI does not query hidden enemy positions.

Validation: `godot/tests/adaptive_hud_test.gd` exercises the replacement with a mock
HUD, including controller activation/cancellation, power-store cleanup, map orders,
mode restoration, and phone portrait/landscape plus desktop layout bounds. It needs
no retail data. It was run in an isolated Godot 4.6 project; production targets the
repository's Godot 4.7 build. Native adapters were syntax-checked against that
build's headers. Full retail gameplay, visual screenshots and physical iOS/controller
hardware testing remain pending.

`godot/tests/adaptive_fortress_menu_test.gd` tests the installed RotWK fortress
through the replacement UI: Upgrades, an upgrade choice, Heroes and Back, with
fortress construction enabled and disabled.

## Sizing and live reload

All sizing is in `godot/data/adaptive_hud_layout.json`. Defaults request an
880-pixel square minimap (four times the previous side length), a 1040-unit-tall
command panel (four times the previous maximum height), and five command columns.
Construction remains above commands. Sizes fit within the viewport on smaller
screens; there, fitting takes precedence over the requested dimensions.

When running from source, edit that JSON file. Exported games create an editable
`user://adaptive-hud-layout.json` on first HUD initialization. On macOS this is
`~/Library/Application Support/Godot/app_userdata/OpenBFME/adaptive-hud-layout.json`.
The HUD button tooltip shows the active file's absolute path.

Saving the file reloads it during the match (checked every second by default).
**Shift+F10** or **System → Reload HUD layout** reloads immediately. Panel sizes,
column counts, minimap sizes, tile dimensions, font/icon sizes, padding, spacing,
corners, scaling and reload interval are configurable. Partial JSON objects use
shipped defaults for omitted keys. Invalid JSON or invalid values report an error
and retain the last valid layout. Reloading preserves selection, open submenus,
placement and the power store.
