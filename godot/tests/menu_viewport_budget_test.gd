## Verify the iOS decoration scheduling path on any host, using an explicit budget.
extends SceneTree
func _initialize() -> void: call_deferred("run")
func run() -> void:
	ProjectSettings.set_setting("openbfme/rendering/menu_3d_max_fps", 1)
	root.scaling_3d_scale = 0.75
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini": "user://menu-budget-test.ini", "presentation_fs": fs.create_bfme2_shell()}).ok)
	shell.auto_process = false
	assert(shell.shell_push("MainMenu.apt"))
	for i in 12:
		shell.tick(1.0 / 30.0)
		shell.render(true)
	assert(shell.shell_invoke(shell.shell_top_level(), "ShowMainMenu", PackedStringArray()).ok)
	shell.shell_show_shell_map(false)
	assert(shell.shell_fscommand("SetBackground", "fadein"))
	assert(shell.invoke(11, "ShowEndGame", PackedStringArray(["1", "", ""])).ok)
	for i in 80:
		shell.tick(1.0 / 30.0)
		shell.render(true)
		if shell.get_report().view3d_render_updates > 0:
			break
	var views := 0
	for node in shell.get_children():
		if node is SubViewport:
			views += 1
			assert(node.scaling_3d_scale == 0.75)
			assert(node.render_target_update_mode != SubViewport.UPDATE_ALWAYS)
			for model in node.get_children():
				if model.is_class("W3DInstancer"):
					assert(not model.get_auto_update())
	print("VIEWPORTS ", views)
	assert(views == 2 * int(shell.get_report().view3d_viewers), "each decoration uses paired blend viewports")
	assert(not shell.get_report().view3d_viewers == 0, "the menu contains a 3D decoration")
	var first: int = shell.get_report().view3d_render_updates
	for i in 10:
		shell.tick(0.0)
		shell.render(true)
	assert(shell.get_report().view3d_render_updates == first, "unchanged decoration respects its rendering deadline")
	assert(shell.get_report().view3d_errors.is_empty())
	shell.free()
	world.free()
	print("MENU VIEWPORT BUDGET PASS")
	quit(0)
