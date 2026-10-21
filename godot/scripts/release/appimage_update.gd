extends CanvasLayer
## Explicit, main-menu-only AppImage update. Downloads stay beside the active image.
const MANIFEST_URL := "https://secondage.dev/downloads/openbfme/linux-x86_64/latest.json"
const DOWNLOAD_ROOT := "https://secondage.dev/downloads/openbfme/linux-x86_64/"
const Handover := preload("res://scripts/release/appimage_handover.gd")
var can_update := false
var _status: Label
var _start: Button
var _back: Button
var _http: HTTPRequest
var _busy := false
var _image := ""
var _lock := ""
var _candidate := ""

static func select_asset(data: Variant) -> Dictionary:
	if not data is Dictionary or data.get("schema") != 1:
		return {}
	var sha := str(data.get("sha256", ""))
	var url := str(data.get("url", ""))
	if sha.length() != 64 or not sha.is_valid_hex_number(false): return {}
	if url != DOWNLOAD_ROOT + sha + ".AppImage": return {}
	var size := int(data.get("size", 0))
	if size < 11 or size > 2147483648: return {}
	return {"sha256": sha, "url": url, "size": size, "version": str(data.get("version", "unknown"))}

static func image_error(path: String) -> String:
	if path.is_empty() or not path.is_absolute_path() or not FileAccess.file_exists(path):
		return "Run the Linux AppImage to update it."
	var dir := DirAccess.open(path.get_base_dir())
	if dir == null or dir.is_link(path.get_file()):
		return "The AppImage must be a regular file in a writable folder."
	return ""

func _ready() -> void:
	set_meta("openbfme_native_menu", true)
	layer = 140
	var shade := ColorRect.new()
	shade.color = Color(0.025, 0.035, 0.05, 0.98)
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(shade)
	var box := VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.offset_left = 40; box.offset_right = -40; box.offset_top = 40; box.offset_bottom = -40
	box.add_theme_constant_override("separation", 24)
	add_child(box)
	var title := Label.new(); title.text = "AppImage updates"; title.add_theme_font_size_override("font_size", 28); box.add_child(title)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.text = "Download the latest Steam Deck / Linux build from Second Age, then restart. Your Steam shortcut and game files stay in place. The previous AppImage is kept as a backup."
	box.add_child(_status)
	_start = Button.new(); _start.text = "Update and restart"; _start.custom_minimum_size.y = 64; box.add_child(_start)
	_start.pressed.connect(_update)
	_back = Button.new(); _back.text = "Back"; _back.custom_minimum_size.y = 64; box.add_child(_back)
	_back.pressed.connect(_close)
	_http = HTTPRequest.new(); _http.use_threads = true; _http.timeout = 300; add_child(_http)
	_image = OS.get_environment("APPIMAGE") if OS.get_name() == "Linux" else ""
	var why := image_error(_image) if can_update else "Return to the main menu and leave the lobby before updating."
	_start.disabled = not why.is_empty()
	if not why.is_empty(): _status.text = why
	(_back if _start.disabled else _start).grab_focus()

func _input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_close()

func _process(_delta: float) -> void:
	if _busy and not _http.download_file.is_empty():
		_status.text = "Downloading update… %.1f MiB" % (_http.get_downloaded_bytes() / 1048576.0)

func _close() -> void:
	_http.cancel_request()
	_cleanup()
	queue_free()

func _cleanup() -> void:
	if _lock.is_empty(): return
	for name in ["new.AppImage", "finish.sh"]:
		DirAccess.remove_absolute(_lock.path_join(name))
	DirAccess.remove_absolute(_lock)
	_lock = ""

func _fail(message: String) -> void:
	_busy = false
	_http.download_file = ""
	_cleanup()
	_status.text = message
	_start.disabled = false
	_back.text = "Back"
	_start.grab_focus()

func _update() -> void:
	if _busy: return
	_busy = true; _start.disabled = true; _back.text = "Cancel"
	_status.text = "Checking for the latest AppImage…"
	_http.body_size_limit = 65536
	var err := _http.request(MANIFEST_URL + "?t=" + str(int(Time.get_unix_time_from_system())))
	if err != OK: _fail("Could not check for updates: " + error_string(err)); return
	var reply: Array = await _http.request_completed
	if reply[0] != HTTPRequest.RESULT_SUCCESS or reply[1] != 200:
		_fail("Update check failed (HTTP %s). Try again later." % reply[1]); return
	var asset := select_asset(JSON.parse_string((reply[3] as PackedByteArray).get_string_from_utf8()))
	if asset.is_empty(): _fail("The update manifest is invalid. The installed app was not changed."); return
	if FileAccess.get_sha256(_image) == asset.sha256:
		_fail("You already have the latest AppImage."); return
	_lock = _image + ".update-lock"
	if DirAccess.make_dir_absolute(_lock) != OK:
		_lock = ""
		_fail("Cannot prepare the update. Check that the AppImage folder is writable and no other update is running."); return
	_candidate = _lock.path_join("new.AppImage")
	_http.body_size_limit = asset.size
	_http.download_file = _candidate
	err = _http.request(asset.url)
	if err != OK: _fail("Download could not start: " + error_string(err)); return
	reply = await _http.request_completed
	_http.download_file = ""
	if reply[0] != HTTPRequest.RESULT_SUCCESS or reply[1] != 200:
		_fail("Download failed (HTTP %s). The installed app was not changed." % reply[1]); return
	var file := FileAccess.open(_candidate, FileAccess.READ)
	if file == null: _fail("Cannot read the downloaded update."); return
	var size := file.get_length()
	var header := file.get_buffer(11)
	file.close()
	if size != asset.size or header.slice(0, 4) != PackedByteArray([127, 69, 76, 70]) or header.slice(8, 11) != PackedByteArray([65, 73, 2]) or FileAccess.get_sha256(_candidate) != asset.sha256:
		_fail("The downloaded AppImage failed verification. The installed app was not changed."); return
	var helper := _lock.path_join("finish.sh")
	file = FileAccess.open(helper, FileAccess.WRITE)
	if file == null: _fail("Cannot prepare the restart helper."); return
	file.store_string(Handover.SCRIPT); file.close()
	var args := PackedStringArray([helper, _image, _candidate, str(OS.get_process_id()), asset.sha256, _lock])
	args.append_array(OS.get_cmdline_args())
	if not OS.get_cmdline_user_args().is_empty():
		args.append("--")
		args.append_array(OS.get_cmdline_user_args())
	if OS.create_process("/bin/sh", args) <= 0:
		_fail("Cannot start the update helper. The installed app was not changed."); return
	_lock = "" # The external helper owns cleanup after the game exits.
	get_tree().quit()
