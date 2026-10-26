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

This is an initial map display, not retail presentation parity (S-5304). The
camera fits the INI Center/Extent with AptPitch; exact AptCenter/AptZoom
projection, map-specific lighting, animated clouds and region effects remain
unverified. An explicitly authored FX `NormalMap=None` selects no bump mapping
and records a material note; missing real texture names still produce errors.

The remaining territory interaction is S-5301: register the native window's
input/listener path, draw region highlights, connect hover to the territory
description, and implement start region picks using scenario restrictions.
Turns, armies and battles remain S-5302; the lobby still refuses strategic game
start explicitly.

`godot/tests/territory_map_view_test.gd` opens the actual strategic lobby, checks
model loading, checks that the GPU image has textured content, verifies static
texture reuse and verifies that leaving the lobby disables the viewport. Set
`OPENBFME_TERRITORY_SCREENSHOT` to capture the lobby and its map texture.
