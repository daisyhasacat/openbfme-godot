# Snow weather console override

Open the in-game console with the backtick key and click **Snow weather**.
Click again to restore the map's original weather. It works on any loaded map;
loading another map or ending the game clears the override.

This is an opt-in OpenBFME presentation effect, not a port of a retail snow
accumulation shader. It does not activate Freezing Rain, Darkness, attribute
modifiers, or gameplay weather spells, and consumes no simulation RNG.

Live drawables select the mounted template's `SNOW` ModelConditionState, including
snow models and `Texture = A B` replacements. Authoritative model conditions
remain unchanged; new drawables inherit the override and subsequent animation,
damage and construction flag updates keep it. Disabling restores the most recent
authoritative conditions, including native snow on an authored snowy map.
Static props use the same model-state selector and floor weather textures. Their
stable renderer IDs, transforms, shroud, sway and tree-topple references survive
variant changes. Assets come from the mounted game or mod; missing variants use
the template's normal state matching and the procedural coating.

`snow_cover.gdshaderinc` adds world-space drift/grain and slope-dependent coverage
to lit model surfaces. Ground uses a denser snow layer on the surface albedo
before lighting, including terrain blend layers, roads and paving. Dark soil
and asphalt no longer darken the snow itself. Shared world-space drift and grain
keep the coating continuous across terrain chunks and road ribbons. Road alpha
is preserved, so soft road edges still blend into the snowy ground.
Roofs and ground receive more coverage than walls. Baked lighting/detail, material alpha, fog, shroud,
selection decals and targeting cursors remain intact. Unshaded/additive effects,
water, sky and UI are excluded. The coating can also dust authored snow variants.
It models exposure by surface slope, not roof occlusion or accumulated geometry.
World positions are reconstructed from an existing varying to preserve the
Metal/Forward+ varying budget.

The terrain builder also builds a map of authored snow replacements from the
mounted `terrain.ini`: an exact terrain type name followed by `Snow` or `Snowy`
(case insensitive; `Snow` takes priority). It does not guess between unrelated
textures. Matching texture tiles use a separate snow atlas in the original dry
atlas layout, preserving UVs, blend weights and cliff wrapping. This atlas only
affects the enabled visual override; disabling it restores the dry material.
Unmapped classes use procedural snow. Missing, corrupt or undersized explicit
replacements are reported and receive procedural coverage. The builder's `snow`
report lists `terrain_type_mapping`, `texture_mapping` (filenames), `applied`,
`procedural` and `errors`, making
the texture map inspectable for the current mounted game or mod. Additional
semantic pairings without an exact name match remain future work.

Precipitation uses the existing map snow renderer with the enhanced preset in
`godot/weather/console_snow.ini`: Buckland Snow's mounted-texture reference,
700-unit height, 10-unit spacing,
60-unit fall speed, 4-unit quads and X:60/Y:35 drift,
independent of the active map's rain settings. The texture comes from mounted
assets. The values were checked against the user-installed CrossOver custom map.
Particle count remains capped at 12,000; the renderer approximates amplitude with
angular spread rather than the original frequency-driven sway, so this is not
a claim of exact retail visual parity. Horizontal speed is constant drift,
not gravity that accelerates flakes throughout their lifetime. Restoring defaults
retains precipitation on maps that originally had it. A missing snow texture is reported as an error.

Validation: `snow presentation:*` tests authored building state selection,
new-object inheritance, restoration and unchanged logic hash. `snow shader:*`
checks inclusion/exclusion of material families. `snow_weather_test.gd` exercises
the public toggle, map changes, native snowfall restoration and (on a GPU) checks
that roofs brighten, walls stay unchanged, dark terrain/roads/paving become
snowy, authored replacements are sampled, and removing the coating reproduces
the original image exactly. `snow terrain:*` checks replacement discovery, atlas
layout preservation and invalid asset reporting with generated textures.
Set OPENBFME_SNOW_SCREENSHOT to save a map capture, or
OPENBFME_SNOW_GROUND_SCREENSHOT for the ground/road/paving GPU test strip.
