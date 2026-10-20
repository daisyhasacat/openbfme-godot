## Exported-pack/loader check: no retail installs or display server required.
extends SceneTree

func _initialize() -> void:
	for native_class in ["RetailFileSystem", "GameWorld", "InGameHudNode"]:
		if not ClassDB.class_exists(native_class):
			push_error("APPIMAGE BOOT FAIL: missing native class " + native_class)
			quit(1)
			return
	var world: Node = ClassDB.instantiate("GameWorld")
	world.free()
	print("APPIMAGE BOOT PASS")
	quit(0)
