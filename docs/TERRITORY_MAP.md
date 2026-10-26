# War of the Ring territory map

The strategic lobby already opens the shipped scenario list. The next dependency,
the territory data layer, now loads from the same mounted archives and
`RetailObjectWorld` as those scenarios. No extracted game assets are committed.

`LivingWorldMapStore` owns:

- `LivingWorldMapInfo`: the W3D map object, lobby camera settings, lights,
  banners, clouds, scroll settings and shadow configuration.
- `LivingWorldRegionEffects`: named region model, border/start colours and
  named effect geometry with opacity and intensity control points.
- `LivingWorldRegionCampaign`: regions, mesh sub-object names, localized
  descriptions, connections and detour points, placement positions, building
  restrictions, bonuses and concurrent territory bonuses.

The store is selected with the world's existing thread-local context owner
chain, so loading a second world does not make the first use its territory data.
Its blocks replace recording stubs; the build and simulation classification list
this as client data, not simulation logic.

Evidence is recorded beside the field tables in `LivingWorldMap.h`: the observed
RotWK tables and custom parser addresses. The target-binary provenance caveat in
PLAN rule 9 still applies. This completes data loading, not rendering parity.
Unsupported fields and unresolved constructor/overwrite/EVA details are reported
as S-5303 in the strategic lobby and documented in STOPS.md. Unknown keys fail
through the normal INI parser. Duplicate named effect/campaign blocks are refused
explicitly rather than leaving the first definition silently active.

`test_wotr1_map.cpp` checks synthetic nested grammar, camera/shadow fields,
connections, restrictions, effect timing, named lookup, unsupported fields and
duplicates. Its mounted-data test checks that region campaigns resolve their
effects and region connections. Existing WOTR lobby tests continue to exercise
the shipped scenario selection and description.

The Godot device now draws the mounted `MapObject` with `W3DInstancer` in the
authored `LivingWorldMap` placeholder, preserving APT depth, scaling and clip
colour. It uses a dedicated viewport and the normal gameplay archive source even
when the shell decorations use BFME2 assets. The static map is cached; resize or
reopening renders it again, and leaving the panel disables its viewport. Booting
a new shell releases the previous view. Model failures reach
`get_report().living_world.errors`.

The lobby now includes terrain-shaped hover and player-colour ownership
highlights. Picking tests the mounted RegionObject triangles under the exact
render camera, so irregular borders and holes follow the mesh rather than a
bounding rectangle. Boundary edges are derived from those triangles and drawn
with antialiasing. The overlay is composited after the W3D scene, preserving
visibility above transparent map effects, and clipped to the APT panel. The
large world-label subobject named by TextLayerSubObject is hidden for this view.

Hover shows the localized region name and its authored description when supplied;
otherwise the map-cache description is used if available. Leaving the map clears
hover and description without clearing a chosen start. Left click claims an
available region for the next eligible local/AI slot. Clicking a claim cycles it
to the next eligible slot; right click releases a claim the player may edit.
Remote human claims cannot be edited. OwnershipSet regions highlight and select
as a group. Disabled/disallowed starts, faction/team restrictions, closed slots,
observers and scenario changes are checked by LivingWorldTerritorySelection.
Player-colour changes update existing claims; unresolved colours use a neutral
tint without consuming simulation RNG. Scenario defaults populate occupied slots.

The ocean reuses the existing standing-water shader and the WaterShader.FX
parameters/textures carried by the mounted map mesh. It no longer renders with
the generic normalmapped approximation. The base map remains cached; hover and
selection redraw only canvas overlays. The renderer's existing standing-water
limitations (S-035) still apply.

Evidence for the interaction comes from BFME2's recovered
[AptMapPreview.cpp](https://github.com/Open-BFME/Open-BFME-2/blob/6243bfba4cc4a921892c2b545be75c6a475a6f68/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Apt/AptMapPreview.cpp),
[Rva004FD6F9.cpp](https://github.com/Open-BFME/Open-BFME-2/blob/6243bfba4cc4a921892c2b545be75c6a475a6f68/Code/GameEngine/Source/Common/Rva004FD6F9.cpp)
and [Rva004FD448.cpp](https://github.com/Open-BFME/Open-BFME-2/blob/6243bfba4cc4a921892c2b545be75c6a475a6f68/Code/GameEngine/Source/Common/Rva004FD448.cpp).
The port uses their ownership grouping, disallowed-start lookup and hover text
shape, together with the existing lobby's slot-cycling rules. Exact target
eligibility flag and faction/team restriction semantics remain S-5301.

This completes the local territory UI, not strategic game execution. Selections
are stored as names in the lobby's client model; retail region-ID persistence
and network serialization are not claimed (S-5301). Turns, armies and battles
remain S-5302 and the lobby still refuses strategic game start explicitly.
Native AptCenter/AptZoom projection, per-map light binding and effect timing are
not yet verified (S-5304); the reference framing uses an orthographic crop of
the authored Center/Extent with AptPitch. Explicit FX NormalMap=None selects no
bump mapping and records a material note; missing real texture names still error.

`test_wotr1_map.cpp` covers grouped picks, exclusions, defaults, slot cycling,
permissions and faction/team restrictions. The GPU test
`godot/tests/territory_map_view_test.gd` opens the actual strategic lobby, clicks
rendered triangles, checks selection and visible highlight changes, verifies
localized hover text/clearing, changes scenarios and checks static texture reuse
and viewport shutdown. Set `OPENBFME_TERRITORY_SCREENSHOT` to capture the lobby
and its base map texture. The test uses isolated preferences and a 4:3 viewport
for comparison with the retail reference.
