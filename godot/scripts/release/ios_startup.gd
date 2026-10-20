extends CanvasLayer
## Draw before parsing/instantiating the game and retain feedback during data mounting.
var status: Label
var progress: ProgressBar
var began := Time.get_ticks_msec()

func _ready() -> void:
	layer = 80
	add_to_group("ios_startup")
	var background := ColorRect.new()
	background.color = Color("10191e")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var content := VBoxContainer.new()
	content.custom_minimum_size.x = 320
	content.add_theme_constant_override("separation", 20)
	center.add_child(content)
	status = Label.new()
	status.text = "Loading OpenBFME…"
	status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status.add_theme_font_size_override("font_size", 28)
	content.add_child(status)
	progress = ProgressBar.new()
	progress.custom_minimum_size = Vector2(320, 18)
	progress.show_percentage = false
	progress.max_value = 1.0
	progress.indeterminate = true
	content.add_child(progress)
	print("STARTUP iOS loading screen ready at %d ms" % Time.get_ticks_msec())
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
	get_tree().root.add_child(scene.instantiate())

func stage(text: String) -> void:
	status.text = text
	# Archive verification currently has no incremental progress callback.
	progress.indeterminate = true
	progress.value = 0.0
	print("STARTUP %s at %d ms" % [text, Time.get_ticks_msec()])

func finished() -> void:
	print("STARTUP initial loading complete at %d ms" % Time.get_ticks_msec())
	queue_free()
