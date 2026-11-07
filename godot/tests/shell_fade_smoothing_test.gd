## White fades must update between script ticks without spending another 3D render.
extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	ProjectSettings.set_setting("openbfme/rendering/menu_3d_max_fps", 1)
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	var scene
	var previous: Dictionary = {}
	var previous_updates := 0
	var found_smooth_sample := false
	var deadline := Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		var shell = game.get("_shell")
		if shell == null:
			continue
		if scene == null:
			for instance in shell.find_children("*", "W3DInstancer", true, false):
				if not instance.get_shell_scene_state().is_empty():
					scene = instance
					break
		if scene == null:
			continue
		var current: Dictionary = scene.get_shell_scene_state()
		var updates: int = shell.get_report().view3d_render_updates
		if not previous.is_empty() and current.effect_ticks == previous.effect_ticks and updates == previous_updates:
			if current.fade_render_add != previous.fade_render_add:
				assert(current.fade_add == previous.fade_add, "script fade changed between ticks")
				found_smooth_sample = true
				break
		previous = current
		previous_updates = updates
	assert(found_smooth_sample, "fade was held at script or 3D render cadence")
	print("SHELL FADE SMOOTHING PASS (1 Hz decoration budget)")
	game.queue_free()
	await process_frame
	quit(0)
