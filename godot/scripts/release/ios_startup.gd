extends CanvasLayer
## Draw before parsing/instantiating the game and retain feedback during data mounting.
var status: Label
var progress: ProgressBar
var ring_texture: AtlasTexture
var ring_info: Dictionary = {}
var began := Time.get_ticks_msec()

func _ready() -> void:
	layer = 80
	add_to_group("ios_startup")
	_build_screen()
	print("STARTUP iOS loading screen ready at %d ms" % Time.get_ticks_msec())
	if DisplayServer.get_name() == "headless":
		await get_tree().process_frame
	else:
		await RenderingServer.frame_post_draw
	ResourceLoader.load_threaded_request("res://scenes/game.tscn")
	var loading: Array = []
	while ResourceLoader.load_threaded_get_status("res://scenes/game.tscn", loading) == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		progress.indeterminate = false
		progress.value = float(loading[0]) if not loading.is_empty() else 0.0
		await get_tree().process_frame
	progress.indeterminate = false
	progress.value = 1.0
	var scene = ResourceLoader.load_threaded_get("res://scenes/game.tscn")
	if scene == null:
		status.text = "Could not load OpenBFME."
		progress.hide()
		return
	print("STARTUP game scene loaded in %d ms" % (Time.get_ticks_msec() - began))
	var game: Node = scene.instantiate()
	get_tree().root.add_child(game)
	get_tree().current_scene = game

func _build_screen() -> void:
	var background := ColorRect.new()
	background.color = Color.BLACK
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var artwork := TextureRect.new()
	artwork.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	artwork.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	artwork.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	if ResourceLoader.exists("res://startup_art/loading.png"):
		artwork.texture = load("res://startup_art/loading.png")
	add_child(artwork)
	var content := VBoxContainer.new()
	content.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM)
	content.offset_left = -160
	content.offset_right = 160
	content.offset_top = -116
	content.offset_bottom = -24
	content.alignment = BoxContainer.ALIGNMENT_CENTER
	add_child(content)
	if ResourceLoader.exists("res://startup_art/ring_atlas.png") and FileAccess.file_exists("res://startup_art/animation.json"):
		ring_info = JSON.parse_string(FileAccess.get_file_as_string("res://startup_art/animation.json"))
		ring_texture = AtlasTexture.new()
		ring_texture.atlas = load("res://startup_art/ring_atlas.png")
		var ring := TextureRect.new()
		ring.texture = ring_texture
		ring.custom_minimum_size = Vector2(64, 64)
		ring.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		ring.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		content.add_child(ring)
	status = Label.new()
	status.text = "Starting OpenBFME…"
	status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status.add_theme_font_size_override("font_size", 20)
	content.add_child(status)
	progress = ProgressBar.new()
	progress.custom_minimum_size = Vector2(320, 8)
	progress.show_percentage = false
	progress.max_value = 1.0
	progress.indeterminate = true
	progress.visible = ring_texture == null
	content.add_child(progress)

func stage(text: String) -> void:
	status.text = text
	# Archive verification currently has no incremental progress callback.
	progress.indeterminate = true
	progress.value = 0.0
	print("STARTUP %s at %d ms" % [text, Time.get_ticks_msec()])

func finished() -> void:
	print("STARTUP initial loading complete at %d ms" % Time.get_ticks_msec())
	queue_free()

func _process(_delta: float) -> void:
	if ring_texture == null:
		return
	var frame := int((Time.get_ticks_msec() - began) * float(ring_info.fps) / 1000.0) % int(ring_info.frames)
	var columns := int(ring_info.columns)
	ring_texture.region = Rect2((frame % columns) * int(ring_info.width), (frame / columns) * int(ring_info.height), int(ring_info.width), int(ring_info.height))
