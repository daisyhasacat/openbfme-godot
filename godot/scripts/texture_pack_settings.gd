## Optional texture packs are mounted at startup; keep their enabled list intact
## when switching back to the original game's textures.
extends RefCounted

const PATH := "user://mods.cfg"

static func read(path := PATH) -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(path)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		return {"ok": false, "error": error_string(error)}
	return {"ok": true, "enabled": bool(config.get_value("mods", "hd_textures", true))}

static func save_enabled(enabled: bool, path := PATH) -> Error:
	var config := ConfigFile.new()
	var error := config.load(path)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		return error
	config.set_value("mods", "hd_textures", enabled)
	return config.save(path)
