extends SceneTree
const Updater := preload("res://scripts/release/appimage_update.gd")
func _initialize() -> void:
	var sha := "a".repeat(64)
	var good := {"schema": 1, "sha256": sha, "url": Updater.DOWNLOAD_ROOT + sha + ".AppImage", "size": 123, "version": "test"}
	assert(Updater.select_asset(good).sha256 == sha)
	for change in [{"schema": 2}, {"sha256": "g".repeat(64)}, {"url": "https://evil.invalid/update.AppImage"}, {"size": 0}, {"size": 2147483649}]:
		var bad := good.duplicate(); bad.merge(change, true)
		assert(Updater.select_asset(bad).is_empty())
	assert(Updater.select_asset([]).is_empty())
	assert(Updater.image_error("") == "Run the Linux AppImage to update it.")
	call_deferred("_ui")
func _ui() -> void:
	var options = load("res://tests/appimage_options_probe.gd").new()
	root.add_child(options)
	options._tab = "Sync"
	options._mods_button.grab_focus()
	var right := InputEventAction.new(); right.action = "ui_right"; right.pressed = true
	options._input(right)
	assert(options._tab == "Updates")
	options._input(right)
	assert(options._tab == "Display")
	options.free()
	var updater := Updater.new()
	root.add_child(updater)
	var controller = load("res://scripts/controller_input.gd").new()
	root.add_child(controller)
	assert(controller._native_menu_focus())
	controller.free()
	assert(updater._start.disabled)
	assert(updater._status.text == "Return to the main menu and leave the lobby before updating.")
	updater._close()
	await process_frame
	print("APPIMAGE UPDATE PASS")
	quit()
