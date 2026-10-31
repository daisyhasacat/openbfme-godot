## OpenBFME presentation presets; never changes simulation or retail detail data.
extends RefCounted

static func resolved(profile: String, platform: String = "") -> String:
	if platform.is_empty(): platform = OS.get_name()
	return ("balanced" if platform == "iOS" else "high") if profile == "auto" else profile

## After object_shadows.configure (the map's sun): the profile's cascade split only; coverage, bias and the ShadowLOD switch stay configure's.
static func apply_cascades(sun: DirectionalLight3D, profile: String, platform: String = "") -> void:
	var balanced := resolved(profile, platform) == "balanced"
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS if balanced else DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_blend_splits = not balanced

static func apply(sun: DirectionalLight3D, profile: String, lod: int, platform: String = "") -> Dictionary:
	var effective := resolved(profile, platform)
	assert(effective in ["balanced", "high"])
	sun.shadow_enabled = lod > 0
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS if effective == "balanced" else DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_blend_splits = effective == "high"
	# Keep the existing coverage: shortening it can lose shadows at wide zoom.
	sun.directional_shadow_max_distance = 3000.0
	sun.shadow_bias = 0.03
	sun.shadow_normal_bias = 1.0
	return {"requested": profile, "effective": effective, "enabled": sun.shadow_enabled,
		"cascades": 2 if effective == "balanced" else 4, "blend_splits": sun.directional_shadow_blend_splits,
		"max_distance": sun.directional_shadow_max_distance}
