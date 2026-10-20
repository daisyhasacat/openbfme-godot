## Lane MODS-1: the in-game mod manager (the "Mods" link under the version in the menus, Release.open_mod_manager()).
##
## A mod is a RotWK 2.01 "-mod" folder (loose data\ini, art, maps ... and *.big inside it) or a single .big file. They live in
## user://mods (or anywhere, added with "Add mod folder..."); user://mods.cfg holds the order and which are on:
##   [mods] order = [absolute game roots, load order], enabled = [the enabled ones, in that order],
##   textures_only = [mods mounted as texture packs: only their art\compiledtextures, art\textures and art\terrain files]
## The engine mounts the enabled ones at startup over the verified game files (RetailFileSystem, GodotRetailFileSystem.cpp), so a change
## takes effect after a restart. A downloaded mod folder often has its game files one level down (RJ RotWK: "RJ RotWK 1 06 3 3" beside
## the screenshots); the game root is the folder itself when it holds data / art / maps / *.big, else its one subfolder that does.
extends CanvasLayer

const CONFIG := "user://mods.cfg"
const MODS_DIR := "user://mods"
## the engine's own optional packs (extracted beside the mods; switched in Options, not here)
const BUILT_IN := ["rotwk_202_maps.big", "wall_towers.big"]
const GAME_DIRS := ["data", "art", "maps", "apt", "libraries"]
const UI_SCALE := 1.5

var active_mods: Array = []   ## the mount report's "mods" (what this session runs with)
var _order: Array[String] = []
var _enabled: Dictionary = {}
var _textures_only: Dictionary = {}
var _list: VBoxContainer
var _status: Label
var _dialog: FileDialog
var _changed := false
var _game_picker: OptionButton
var _game := "bfme2"
var _bfme1_path := ""
var _aotr_path := ""


func _ready() -> void:
	layer = 120
	process_mode = Node.PROCESS_MODE_ALWAYS
	_game = Release.selected_game()
	_bfme1_path = Release.bfme1_install()
	_aotr_path = Release.aotr_install()
	_load()
	_discover()
	_build()
	_refresh()


## the game root of a mod folder (see the header); "" when it holds no game files
static func game_root(path: String) -> String:
	if path.get_extension().to_lower() == "big":
		return path if FileAccess.file_exists(path) else ""
	if _holds_game_files(path):
		return path
	var found := ""
	for sub in DirAccess.get_directories_at(path):
		var p := path.path_join(sub)
		if _holds_game_files(p):
			if not found.is_empty():
				return "" # two candidates: ambiguous, the player adds the right one
			found = p
	return found


static func _holds_game_files(path: String) -> bool:
	var dir := DirAccess.open(path)
	if dir == null:
		return false
	for d in dir.get_directories():
		if d.to_lower() in GAME_DIRS:
			return true
	for f in dir.get_files():
		if f.get_extension().to_lower() == "big":
			return true
	return false


func _load() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(CONFIG) != OK:
		return
	for p in cfg.get_value("mods", "order", []):
		if not _order.has(String(p)):
			_order.append(String(p))
	for p in cfg.get_value("mods", "enabled", []):
		_enabled[String(p)] = true
		if not _order.has(String(p)):
			_order.append(String(p))
	for p in cfg.get_value("mods", "textures_only", []):
		_textures_only[String(p)] = true


func _save() -> void:
	var cfg := ConfigFile.new()
	var enabled: Array = []
	for p in _order:
		if _enabled.get(p, false):
			enabled.append(p)
	cfg.set_value("mods", "order", Array(_order))
	cfg.set_value("mods", "enabled", enabled)
	var textures: Array = []
	for p in _order:
		if _textures_only.get(p, false):
			textures.append(p)
	cfg.set_value("mods", "textures_only", textures)
	var err := cfg.save(CONFIG)
	_status.text = "Saved. Restart OpenBFME to apply." if err == OK else "Could not save %s: %s" % [CONFIG, error_string(err)]
	print("MODS saved: %d enabled of %d (%s)" % [enabled.size(), _order.size(), error_string(err)])


func _discover() -> void:
	var root := ProjectSettings.globalize_path(MODS_DIR)
	DirAccess.make_dir_recursive_absolute(root)
	for d in DirAccess.get_directories_at(root):
		_add_path(root.path_join(d), false)
	for f in DirAccess.get_files_at(root):
		if f.get_extension().to_lower() == "big" and not BUILT_IN.has(f.to_lower()):
			_add_path(root.path_join(f), false)


## adds a mod (its game root) at the end of the order; false when the path holds no game files
func _add_path(path: String, enable: bool) -> bool:
	var r := game_root(path)
	if r.is_empty():
		return false
	if not _order.has(r):
		_order.append(r)
	if enable:
		_enabled[r] = true
	return true


func _display_name(path: String) -> String:
	var name := path.get_file()
	var parent := path.get_base_dir()
	# a game root inside a download folder: the download's name is the one the player knows
	if parent.get_base_dir() == ProjectSettings.globalize_path(MODS_DIR) and not FileAccess.file_exists(path):
		return parent.get_file() + "  (" + name + ")"
	return name


func _is_active(path: String) -> bool:
	for m in active_mods:
		if String(m.get("path", "")).simplify_path() == path.simplify_path():
			return true
	return false


func _build() -> void:
	var panel := PanelContainer.new()
	panel.name = "ModManager"
	panel.scale = Vector2(UI_SCALE, UI_SCALE)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.08, 0.07, 0.05, 0.96)
	style.border_color = Color(0.72, 0.6, 0.36)
	style.set_border_width_all(1)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(12)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var viewport := get_viewport().get_visible_rect().size / UI_SCALE
	panel.custom_minimum_size = Vector2(minf(640, viewport.x - 32), minf(420, viewport.y - 32))
	panel.position = (get_viewport().get_visible_rect().size - panel.custom_minimum_size * UI_SCALE) / 2
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 8)
	panel.add_child(col)
	var title := Label.new()
	title.text = "Games and mods"
	title.add_theme_font_size_override("font_size", 20)
	title.add_theme_color_override("font_color", Color(0.95, 0.85, 0.6))
	col.add_child(title)
	var game_row := HBoxContainer.new()
	col.add_child(game_row)
	var game_label := Label.new()
	game_label.text = "Game"
	game_row.add_child(game_label)
	_game_picker = OptionButton.new()
	_game_picker.name = "GamePicker"
	_game_picker.add_item("The Rise of the Witch-king / BFME II")
	_game_picker.add_item("Battle for Middle-earth I")
	_game_picker.add_item("Age of the Ring")
	_game_picker.select(["bfme2", "bfme1", "aotr"].find(_game))
	_game_picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_game_picker.item_selected.connect(func(index: int):
		_game = ["bfme2", "bfme1", "aotr"][index]
		if _game == "aotr" and _aotr_path.is_empty():
			_status.text = "Choose the AOTR installation folder below before restarting."
		else:
			_status.text = "Restart to switch games." if _game!="bfme1" or not _bfme1_path.is_empty() else "Choose the BFME1 installation folder below before restarting."
		_refresh())
	game_row.add_child(_game_picker)
	game_row.add_child(_button("BFME1 files…", _choose_bfme1))
	game_row.add_child(_button("AOTR files…", _choose_aotr))
	var note := Label.new()
	note.text = "Switch games with Restart to apply. Age of the Ring uses its own assets over verified base files. Mods below apply to standard RotWK only; lower entries win."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.add_theme_font_size_override("font_size", 12)
	note.modulate = Color(1, 1, 1, 0.75)
	col.add_child(note)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	col.add_child(scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_list)
	_status = Label.new()
	_status.add_theme_font_size_override("font_size", 12)
	col.add_child(_status)
	var buttons := HFlowContainer.new()
	col.add_child(buttons)
	buttons.add_child(_button("Add mod folder…", _open_dialog))
	buttons.add_child(_button("Open mods folder", func() -> void: Release.open_user_folder(Release.UserFolder.MODS)))
	buttons.add_child(_button("Restart to apply", _restart))
	buttons.add_child(_button("Close", _close))
	_dialog = FileDialog.new()
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.use_native_dialog = true
	_dialog.title = "Choose a mod folder"
	_dialog.dir_selected.connect(_on_folder)
	add_child(_dialog)


func _button(text: String, action: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(action)
	return b


func _refresh() -> void:
	for c in _list.get_children():
		c.queue_free()
	if _order.is_empty():
		var empty := Label.new()
		empty.text = "No mods yet. Put a mod folder in the mods folder, or use Add mod folder…"
		_list.add_child(empty)
	for i in _order.size():
		var path: String = _order[i]
		var row := HBoxContainer.new()
		var check := CheckBox.new()
		check.button_pressed = _enabled.get(path, false)
		check.disabled = _game!="bfme2"
		check.text = _display_name(path)
		check.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		check.tooltip_text = path
		check.toggled.connect(func(on: bool) -> void:
			_enabled[path] = on
			_changed = true
			_save())
		row.add_child(check)
		var textures := CheckBox.new()
		textures.text = "Textures only"
		textures.button_pressed = _textures_only.get(path, false)
		textures.disabled = _game!="bfme2"
		textures.tooltip_text = "Use this mod as a texture pack: only its texture files override the game's"
		textures.toggled.connect(func(on: bool) -> void:
			_textures_only[path] = on
			_changed = true
			_save())
		row.add_child(textures)
		var state := Label.new()
		var exists := DirAccess.dir_exists_absolute(path) or FileAccess.file_exists(path)
		state.text = "missing" if not exists else ("active" if _is_active(path) else "")
		state.add_theme_font_size_override("font_size", 11)
		state.modulate = Color(1, 0.5, 0.4) if not exists else Color(0.6, 1, 0.6)
		row.add_child(state)
		var up := _button("▲", func() -> void: _move(i, -1))
		up.disabled = i == 0
		row.add_child(up)
		var down := _button("▼", func() -> void: _move(i, 1))
		down.disabled = i == _order.size() - 1
		row.add_child(down)
		row.add_child(_button("Remove", func() -> void: _remove(i)))
		_list.add_child(row)


func _move(i: int, d: int) -> void:
	var j := i + d
	if j < 0 or j >= _order.size():
		return
	var t: String = _order[i]
	_order[i] = _order[j]
	_order[j] = t
	_changed = true
	_save()
	_refresh()


## forgets the mod (its files stay where they are)
func _remove(i: int) -> void:
	_enabled.erase(_order[i])
	_textures_only.erase(_order[i])
	_order.remove_at(i)
	_changed = true
	_save()
	_refresh()


func _open_dialog() -> void:
	_dialog.popup_centered_ratio(0.6)


func _on_folder(dir: String) -> void:
	if _add_path(dir, true):
		_changed = true
		_save()
		_refresh()
	else:
		_status.text = "No game files (data, art, maps or .big) in " + dir


static func restart_arguments(args: PackedStringArray, game_name: String, bfme1_path := "", aotr_path := "") -> PackedStringArray:
	var result := PackedStringArray()
	for arg in args:
		if arg.begins_with("--game="): continue
		if not bfme1_path.is_empty() and arg.begins_with("--bfme1-install="): continue
		if not aotr_path.is_empty() and arg.begins_with("--aotr-install="): continue
		result.append(arg)
	if not result.has("--"): result.append("--")
	result.append("--game="+game_name)
	if not bfme1_path.is_empty(): result.append("--bfme1-install="+bfme1_path)
	if not aotr_path.is_empty(): result.append("--aotr-install="+aotr_path)
	return result


static func current_restart_arguments(game_name: String, bfme1_path := "", aotr_path := "") -> PackedStringArray:
	var args := OS.get_cmdline_args()
	args.append("--")
	args.append_array(OS.get_cmdline_user_args())
	return restart_arguments(args,game_name,bfme1_path,aotr_path)


func _choose_bfme1() -> void:
	var chooser := FileDialog.new()
	chooser.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	chooser.access = FileDialog.ACCESS_FILESYSTEM
	chooser.use_native_dialog = true
	chooser.title = "Choose the BFME1 installation folder"
	chooser.canceled.connect(chooser.queue_free)
	chooser.dir_selected.connect(func(path: String):
		chooser.queue_free()
		var has_big := false
		for file in DirAccess.get_files_at(path):
			if file.to_lower().ends_with(".big"): has_big=true
		if not has_big:
			_status.text="That folder contains no BFME1 BIG archives."
			return
		var file := FileAccess.open("user://install-paths.cfg",FileAccess.READ)
		var lines := PackedStringArray()
		if file!=null:
			for line in file.get_as_text().split("\n"):
				if not line.begins_with("BFME1_INSTALL="):lines.append(line)
			file.close()
		lines.append("BFME1_INSTALL="+path)
		file=FileAccess.open("user://install-paths.cfg",FileAccess.WRITE)
		if file==null: _status.text="Could not save BFME1 folder.";return
		file.store_string("\n".join(lines));file.close()
		_bfme1_path = path
		_status.text="BFME1 folder saved. Restart to switch games.")
	add_child(chooser)
	chooser.popup_centered_ratio(0.6)


func _choose_aotr() -> void:
	var chooser := FileDialog.new()
	chooser.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	chooser.access = FileDialog.ACCESS_FILESYSTEM
	chooser.use_native_dialog = true
	chooser.title = "Choose Age of the Ring (aotr, AotR, or drive_c folder)"
	chooser.canceled.connect(chooser.queue_free)
	chooser.dir_selected.connect(func(path: String):
		chooser.queue_free()
		var folder: String = Release.aotr_folder(path)
		if folder.is_empty():
			_status.text = "Age of the Ring files were not found in that folder."
			return
		_aotr_path = folder
		var cfg := ConfigFile.new()
		cfg.load("user://game-selection.cfg")
		cfg.set_value("aotr", "install", folder)
		_status.text = "AOTR folder saved. Restart to switch games." if cfg.save("user://game-selection.cfg") == OK else "Could not save AOTR folder.")
	add_child(chooser)
	chooser.popup_centered_ratio(0.6)

func _restart() -> void:
	if _game == "aotr" and _aotr_path.is_empty():
		_status.text = "Choose the AOTR installation folder first."
		return
	if _game=="bfme1" and _bfme1_path.is_empty():
		_status.text="Choose the BFME1 installation folder first."
		return
	_save()
	var cfg := ConfigFile.new()
	cfg.load("user://game-selection.cfg")
	cfg.set_value("game","selected",_game)
	if cfg.save("user://game-selection.cfg")!=OK:
		_status.text="Could not save the game selection."
		return
	# Godot separates engine arguments and arguments after `--`. Preserve both:
	# get_cmdline_args() alone drops player options such as --max-fps and paths.
	OS.set_restart_on_exit(true, current_restart_arguments(_game,_bfme1_path,_aotr_path))
	get_tree().quit()


func _close() -> void:
	queue_free()
