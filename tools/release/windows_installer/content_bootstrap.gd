extends Node

# Added only to the private Windows export, before the normal Release autoload.
func _enter_tree() -> void:
	if Engine.is_editor_hint(): return
	var problem := configure(OS.get_executable_path().get_base_dir(), OS.get_user_data_dir())
	if not problem.is_empty():
		push_error(problem)
		OS.alert(problem, "OpenBFME content setup failed")
		get_tree().quit(1)

static func copy_tree(source: String, destination: String) -> String:
	var error := DirAccess.make_dir_recursive_absolute(destination)
	if error != OK: return "Cannot create map directory: " + error_string(error)
	for name in DirAccess.get_files_at(source):
		error = DirAccess.copy_absolute(source.path_join(name), destination.path_join(name))
		if error != OK: return "Cannot install map " + name + ": " + error_string(error)
	for name in DirAccess.get_directories_at(source):
		var problem := copy_tree(source.path_join(name), destination.path_join(name))
		if not problem.is_empty(): return problem
	return ""

static func configure(root: String, settings: String) -> String:
	var profile_path := root.path_join("content-profile.json")
	if not FileAccess.file_exists(profile_path): return "Missing or invalid installed content profile."
	var profile = JSON.parse_string(FileAccess.get_file_as_string(profile_path))
	if not profile is Dictionary: return "Missing or invalid installed content profile."
	var installs: Array = profile.get("installs", ["rotwk", "bfme2"])
	if not "bfme2" in installs: return "Missing BFME2 content selection."
	for game in installs:
		var folder := root.path_join("content/" + game)
		if not DirAccess.dir_exists_absolute(folder): return "Missing installed game content: " + game
		OS.set_environment(game.to_upper() + "_INSTALL", folder)
	if not "rotwk" in installs: OS.set_environment("ROTWK_INSTALL", "")
	var error := DirAccess.make_dir_recursive_absolute(settings)
	if error != OK: return "Cannot create settings directory: " + error_string(error)
	var stamp := settings.path_join("bundle-root.txt")
	if FileAccess.file_exists(stamp) and FileAccess.get_file_as_string(stamp) == root: return ""
	var enabled: Array[String] = []
	var textures: Array[String] = []
	for mod in profile.mods:
		var path := root.path_join(str(mod.path))
		if not FileAccess.file_exists(path) and not DirAccess.dir_exists_absolute(path): return "Missing installed mod: " + str(mod.path)
		enabled.append(path)
		if mod.textures_only: textures.append(path)
	var config := ConfigFile.new()
	config.set_value("mods", "order", enabled)
	config.set_value("mods", "enabled", enabled)
	config.set_value("mods", "textures_only", textures)
	config.set_value("mods", "hd_textures", profile.hd_textures)
	error = config.save(settings.path_join("mods.cfg"))
	if error != OK: return "Cannot save installed mod paths: " + error_string(error)
	config = ConfigFile.new()
	config.set_value("game", "selected", profile.game)
	error = config.save(settings.path_join("game-selection.cfg"))
	if error != OK: return "Cannot save selected game: " + error_string(error)
	# Persist roots too, for loose movies/audio and tools that use InstallSetup.
	var paths := FileAccess.open(settings.path_join("install-paths.cfg"), FileAccess.WRITE)
	if paths == null: return "Cannot save installed game paths."
	var rotwk := root.path_join("content/rotwk") if "rotwk" in installs else ""
	paths.store_string("ROTWK_INSTALL=" + rotwk + "\nBFME2_INSTALL=" + root.path_join("content/bfme2") + "\n")
	paths.close()
	if FileAccess.file_exists(root.path_join("profile/wall_balance.cfg")):
		error = DirAccess.copy_absolute(root.path_join("profile/wall_balance.cfg"), settings.path_join("wall_balance.cfg"))
		if error != OK: return "Cannot save installed wall settings: " + error_string(error)
	for folder in ["installed-maps", "map-library", "map_edits"]:
		if DirAccess.dir_exists_absolute(root.path_join("content/" + folder)):
			var problem := copy_tree(root.path_join("content/" + folder), settings.path_join(folder))
			if not problem.is_empty(): return problem
	var output := FileAccess.open(stamp, FileAccess.WRITE)
	if output == null: return "Cannot save content configuration marker."
	output.store_string(root)
	output.close()
	return ""
