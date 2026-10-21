## Exported application: OpenBFME --headless -- --appimage-boot-test
## Editor entrypoint: godot --headless --path godot --script res://tests/appimage_boot_test.gd
extends SceneTree

func _initialize() -> void:
	quit(run())

static func run() -> int:
	if DisplayServer.get_name() != "headless":
		push_error("APPIMAGE BOOT FAIL: --appimage-boot-test requires --headless")
		return 1
	for native_class in ["RetailFileSystem", "GameWorld", "InGameHudNode"]:
		if not ClassDB.class_exists(native_class):
			push_error("APPIMAGE BOOT FAIL: missing native class " + native_class)
			return 1
	var world: Node = ClassDB.instantiate("GameWorld")
	if world == null:
		push_error("APPIMAGE BOOT FAIL: cannot instantiate GameWorld")
		return 1
	world.free()
	print("APPIMAGE BOOT PASS")
	return 0
