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

The remaining territory UI is S-5301: register the `LivingWorldMap` native
component, render the mounted map and region effect geometry in the authored APT
placeholder, connect hover to the territory description, and implement start
region picks using the scenario restrictions. The existing View3D device is the
rendering integration reference. Turns, armies and battles remain S-5302; the
lobby still refuses strategic game start explicitly.
