## Editor entrypoint: godot --headless --path godot --script res://tests/smoke_test.gd
## Exported application: OpenBFME --headless -- --smoke-test
extends SceneTree


func _initialize() -> void:
	# DEVPORT-6: the gate imports its checkout only while .godot/extension_list.cfg is missing (the CI's verify script), so a resource added since
	# the slot's first import (the OpenBFME seal, POWEREDBY-1; PLATFORMICON-1's icons before it) had no imported texture and the menus
	# failed to boot. The smoke test is the gate's first Godot run: it imports the project as a fresh clone does, for every gate after it.
	if OS.has_feature("editor"):
		var output: Array = []
		var imported := false
		for _attempt in 2:   # a first import may abort (known Godot quirk with the extension)
			if OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--import"], output, true) == 0:
				imported = true
				break
		if not imported:
			printerr("SMOKE: godot --import failed twice: ", "".join(output).right(2000))
	var code: int = await preload("res://tests/smoke_checks.gd").new().run(self)
	quit(code)
