## Exercise cold menu transitions and retain isolated stalls in timing counters.
extends SceneTree
func _initialize() -> void: call_deferred("run")
func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("MENU TIMING SKIP: retail installations required")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini":"user://menu-timing-test.ini"}).ok)
	for menu in ["MainMenu.apt", "Options.apt", "Skirmish.apt"]:
		assert(shell.shell_push(menu))
		shell.reset_timing()
		var reset: Dictionary = shell.get_stats()
		for stage in ["steps", "list", "canvas", "submit"]:
			assert(reset["peak_" + stage + "_us"] == 0)
		for i in 80:
			shell.tick(1.0 / 30.0)
			shell.render(true)
		var stats: Dictionary = shell.get_stats()
		assert(stats.frames == 80 and stats.rebuilds == 80)
		for stage in ["steps", "list", "canvas", "submit"]:
			# tolerance: a measured maximum must bound the arithmetic mean; no machine-specific time limit.
			assert(stats["peak_" + stage + "_us"] >= stats[stage + "_us"])
		print("MENU TIMING ", menu, " ", stats)
	shell.free()
	world.free()
	print("MENU TIMING PASS")
	quit()
