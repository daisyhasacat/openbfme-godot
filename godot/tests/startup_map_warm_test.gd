## The main menu must not wait for map-label cache warming.
extends SceneTree

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	create_timer(90).timeout.connect(func(): quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini":"user://startup-map-warm-test.ini"}).ok)
	var report: Dictionary = shell.get_shell_report()
	assert(report.map_labels_warmed == 0)
	# tolerance: the mounted retail/mod catalog must contain maps; its size is data-dependent.
	assert(report.map_labels_total > 0)
	assert(shell.shell_push("MainMenu.apt"))
	await process_frame
	await process_frame
	report = shell.get_shell_report()
	# tolerance: two frames warm at most two batches of eight, regardless of machine speed.
	assert(report.map_labels_warmed <= 16)
	assert(shell.shell_push("Skirmish.apt")) # immediate entry still resolves labels synchronously
	var before: int = shell.get_shell_report().map_labels_warmed
	await process_frame
	await process_frame
	assert(shell.get_shell_report().map_labels_warmed == before)
	shell.free()
	world.free()
	print("STARTUP MAP WARM PASS")
	quit()
