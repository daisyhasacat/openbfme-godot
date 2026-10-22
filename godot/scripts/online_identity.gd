## OpenBFME. GPL-3.0.
extends RefCounted

const INSTALL_FILE := "user://install_id.txt"


static func install_id(path: String = INSTALL_FILE) -> Dictionary:
	if FileAccess.file_exists(path):
		return _read_install(path)
	var bytes := Crypto.new().generate_random_bytes(16)
	if bytes.size() != 16:
		return {"error": "cannot generate an install id"}
	# DEVINPR-1 (review of WP-C): written to a file of its own, then renamed into place, so a crash, a full disk or a second game starting at the same
	# moment never leaves a half-written id behind; the id used is the one the file holds after the rename (the same in both games of a race)
	var temp := "%s.%s.tmp" % [path, Crypto.new().generate_random_bytes(8).hex_encode()]
	var file := FileAccess.open(temp, FileAccess.WRITE)
	if file == null:
		return {"error": "cannot save the install id in %s" % ProjectSettings.globalize_path(path)}
	file.store_string(bytes.hex_encode() + "\n")
	file.flush()
	var failed := file.get_error() != OK
	file.close()
	if failed or DirAccess.rename_absolute(temp, path) != OK:
		DirAccess.remove_absolute(temp)
		# the other game of a race renamed its id in first (a rename does not replace a file on every system): that one is the install's
		if not failed and FileAccess.file_exists(path):
			return _read_install(path)
		return {"error": "cannot save the install id in %s" % ProjectSettings.globalize_path(path)}
	return _read_install(path)


static func _read_install(path: String) -> Dictionary:
	var where := ProjectSettings.globalize_path(path)
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"error": "cannot read the saved install id in %s" % where}
	var id := file.get_as_text().strip_edges()
	if file.get_error() != OK or RegEx.create_from_string("^[0-9a-f]{32}$").search(id) == null:
		return {"error": "the saved install id in %s is damaged: delete that file and press Online again (a new id is made)" % where}
	return {"install": id}


static func device_hash(machine_id: String) -> String:
	if machine_id.is_empty():
		return ""
	var bytes := "openbfme-device-v1".to_utf8_buffer()
	bytes.append(0)
	bytes.append_array(machine_id.to_utf8_buffer())
	var hash := HashingContext.new()
	if hash.start(HashingContext.HASH_SHA256) != OK or hash.update(bytes) != OK:
		return ""
	return hash.finish().hex_encode()


static func identity() -> Dictionary:
	var result := install_id()
	if result.has("error"):
		return result
	if not ClassDB.class_exists("ReleaseInfo"):
		return {"error": "cannot read the client build version: extension unavailable"}
	var info: RefCounted = ClassDB.instantiate("ReleaseInfo")
	result["client"] = {"platform": OS.get_name().to_lower(), "version": info.version()}
	var device := device_hash(OS.get_unique_id())
	if not device.is_empty():
		result["device"] = device
	return result
