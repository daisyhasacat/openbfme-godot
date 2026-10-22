extends SceneTree
const Shadows := preload("res://scripts/shadow_profile.gd")
const Options := preload("res://scripts/game_options.gd")
const Pipelines := preload("res://scripts/pipeline_metrics.gd")

func _initialize() -> void:
	var light := DirectionalLight3D.new()
	assert(Shadows.apply(light, "auto", 3, "iOS") == {"requested": "auto", "effective": "balanced", "enabled": true,
		"cascades": 2, "blend_splits": false, "max_distance": 3000.0})
	assert(light.directional_shadow_mode == DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS)
	assert(not light.directional_shadow_blend_splits)
	assert(light.shadow_normal_bias == 1.0)
	assert(Shadows.apply(light, "auto", 3, "macOS").effective == "high")
	assert(light.directional_shadow_mode == DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS)
	assert(light.directional_shadow_blend_splits)
	assert(Shadows.apply(light, "high", 3, "iOS").effective == "high")
	assert(Shadows.apply(light, "balanced", 3, "macOS").effective == "balanced")
	assert(not Shadows.apply(light, "high", 0, "iOS").enabled)
	assert(not light.shadow_enabled)
	light.free()
	var problems := []
	assert(Options.value_of({}, "OpenBFMEShadowProfile", problems) == "auto")
	assert(Options.value_of({"OpenBFMEShadowProfile": "invalid"}, "OpenBFMEShadowProfile", problems) == "auto")
	assert(problems.size() == 1)
	assert(Pipelines.snapshot().keys() == ["canvas", "mesh", "surface", "draw", "specialization"])
	if DisplayServer.get_name() == "headless": assert(not Pipelines.available())
	print("RENDER BUDGET PASS")
	quit()
