## Map transfer (engine GameNetwork/MapPackage.h) on this player's installed maps: every installed map is named by its contents
## (maps/openbfme_map_<digest>/...), its package can be read, and installing that package through another file system (a second player) under a
## scratch root gives the same map name, the same start positions and the same package again. Needs retail files and at least one installed map.
extends SceneTree

const ROOT := "user://map_transfer_test"


func _initialize() -> void:
	call_deferred("_run")


func _fail(why: String) -> void:
	printerr("MAP TRANSFER FAIL: ", why)
	quit(1)


func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	if not mounted.ok:
		_fail(str(mounted))
		return
	var maps: Array = fs.installed_maps()
	if maps.is_empty():
		print("MAP TRANSFER SKIP: no installed map")
		quit(77)
		return
	var other = ClassDB.instantiate("RetailFileSystem") # the receiving player
	if not other.mount_retail().ok:
		_fail("second mount")
		return
	_clean()
	for m: Dictionary in maps:
		var key := str(m.key)
		if load("res://scripts/online_maps.gd").key_digest(key).is_empty():
			_fail("installed map %s is not named by its contents: %s" % [m.path, key])
			return
		var pkg: PackedByteArray = fs.map_package(key)
		if pkg.is_empty():
			_fail("no package for " + key)
			return
		var got: Dictionary = other.install_map_package(pkg, key, ROOT)
		if not got.get("ok", false):
			_fail("install of %s: %s" % [key, got.get("error", "")])
			return
		if str(got.key) != key or int(got.players) != int(m.players):
			_fail("the received map differs: %s / %s players vs %s / %s" % [got.key, got.players, key, m.players])
			return
		if other.map_package(key) != pkg:
			_fail("the received map's package is not the sent one: " + key)
			return
		var tampered := pkg.duplicate()
		tampered[tampered.size() - 1] ^= 1
		if other.install_map_package(tampered, key, ROOT).get("ok", false):
			_fail("a package that does not match its key was installed")
			return
		print("MAP TRANSFER ok: %s (%d KB, %s)" % [m.name, pkg.size() / 1024, key])
	_clean()
	print("MAP TRANSFER PASS: %d maps" % maps.size())
	quit(0)


func _clean() -> void:
	_remove(ProjectSettings.globalize_path(ROOT))


func _remove(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for d in DirAccess.get_directories_at(dir):
		_remove(dir.path_join(d))
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	DirAccess.remove_absolute(dir)
