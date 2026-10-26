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
to lit model surfaces and terrain, including paving. Roofs and ground receive
more coverage than walls. Baked lighting/detail, material alpha, fog, shroud,
selection decals and targeting cursors remain intact. Unshaded/additive effects,
water, sky and UI are excluded. The coating can also dust authored snow variants.
It models exposure by surface slope, not roof occlusion or accumulated geometry.
World positions are reconstructed from an existing varying to preserve the
Metal/Forward+ varying budget.

Precipitation uses the existing map snow renderer and the mounted Weather block's
texture, size, spacing and speed; the override enables that renderer even when
the map's IsSnowing flag is off. Restoring defaults retains precipitation on
maps that originally had it. A missing snow texture is reported as an error.

Validation: `snow presentation:*` tests authored building state selection,
new-object inheritance, restoration and unchanged logic hash. `snow shader:*`
checks inclusion/exclusion of material families. `snow_weather_test.gd` exercises
the public toggle, map changes, native snowfall restoration and (on a GPU) checks
that roofs brighten, walls stay unchanged, and removing the coating reproduces
the original image exactly. Set OPENBFME_SNOW_SCREENSHOT to save a map capture.
