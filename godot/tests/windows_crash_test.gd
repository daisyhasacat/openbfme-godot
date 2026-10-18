## Destructive native test with no retail data. Run as a subprocess, not in the editor:
## Godot --headless --path godot --script res://tests/windows_crash_test.gd -- --crash-probe=main
## Use --crash-probe=worker for the std::thread case. Inspect the resulting local dump with the matching PDB.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	await process_frame
	if OS.get_name() != "Windows" or not OS.is_debug_build():
		printerr("Windows debug Godot required")
		quit(2)
		return
	var info: RefCounted = ClassDB.instantiate("ReleaseInfo")
	print("DUMP_DIRECTORY=", ProjectSettings.globalize_path("user://logs/minidumps"))
	if OS.get_cmdline_user_args().has("--crash-probe=main"):
		info.crash_test()
	elif OS.get_cmdline_user_args().has("--crash-probe=worker"):
		info.crash_test_worker()
	else:
		printerr("Choose --crash-probe=main or --crash-probe=worker")
	quit(3) # neither a crash nor failure to load the extension is success
