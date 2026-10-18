## CASTER-1: read-only broadcast controls. Presentation state is never a player command.
extends CanvasLayer

signal seek_requested(frame: int)
signal go_live_requested

class Timeline extends HSlider:
	var events: Array = []
	var indexed_frame := 1

	func _draw() -> void:
		if max_value <= min_value:
			return
		var width := size.x - 16.0
		var indexed := clampf((indexed_frame - min_value) / (max_value - min_value), 0, 1)
		draw_line(Vector2(8, size.y - 3), Vector2(8 + width * indexed, size.y - 3), Color("648c9b"), 3)
		for event in events:
			var x := 8 + width * (float(event.frame) - min_value) / (max_value - min_value)
			var tint := Color("da955b")
			if event.kind == 1:
				tint = Color("df6464") if event.hero else Color("c48ca4")
			elif event.kind == 2:
				tint = Color("91a8ee")
			elif event.kind == 3:
				tint = Color("d7bd7c")
			draw_line(Vector2(x, 3), Vector2(x, 13), tint, 2)

const GOLD := Color("d7bd7c")
const INK := Color("10191ef0")

var world: Node
var shell: Node
var panel: PanelContainer
var player_row: HBoxContainer
var status_label: Label
var mode_label: Label
var team_button: CheckButton
var all_button: Button
var player_buttons: Dictionary = {}
var players: Array = []
var observed := -1
var team := false
var _elapsed := 0.0
var _theme := Theme.new()
var transport: PanelContainer
var play_button: Button
var step_button: Button
var speed_button: OptionButton
var clock_label: Label
var timeline: Timeline
var marker_button: OptionButton
var replay_note: Label
var go_live_button: Button
var history: Dictionary = {}
var indexed_frame := 1
var transport_error := ""
var _event_frame := 0
var _scrubbing := false
var hud: Node
var director = preload("res://scripts/caster_director.gd").new()
var director_button: Button
var director_label: Label
var director_popup: PopupPanel
var director_options: Dictionary = {}
var _recent_events: Array = []
var overlays: Control
var pip: Control
var pip_button: Button
var suspended := false
var _palantir_level := -1
const SPEEDS := [0.25, 0.5, 1.0, 2.0, 4.0, 8.0]


func configure(game_world: Node, game_shell: Node, camera_hud: Node = null, source_camera: Camera3D = null) -> void:
	world = game_world
	shell = game_shell
	hud = camera_hud
	layer = 105
	panel = PanelContainer.new()
	panel.name = "CasterVision"
	panel.theme = _theme
	var background := StyleBoxFlat.new()
	background.bg_color = INK
	background.border_color = GOLD.darkened(0.45)
	background.set_border_width_all(1)
	background.content_margin_left = 18
	background.content_margin_right = 18
	background.content_margin_top = 10
	background.content_margin_bottom = 10
	panel.add_theme_stylebox_override("panel", background)
	add_child(panel)
	var column := VBoxContainer.new()
	panel.add_child(column)
	var heading := HBoxContainer.new()
	column.add_child(heading)
	mode_label = Label.new()
	mode_label.text = "REPLAY / OBSERVER"
	mode_label.add_theme_color_override("font_color", GOLD)
	heading.add_child(mode_label)
	status_label = Label.new()
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	heading.add_child(status_label)
	player_row = HBoxContainer.new()
	column.add_child(player_row)
	all_button = _button("0  All vision")
	all_button.pressed.connect(select_all)
	player_row.add_child(all_button)
	team_button = CheckButton.new()
	team_button.text = "Team vision [T]"
	team_button.tooltip_text = "Union of this player's and their allies' vision; no enemy information."
	team_button.toggled.connect(_set_team)
	_build_director(column)
	_build_transport(background)
	overlays = load("res://scripts/caster_overlays.gd").new()
	add_child(overlays)
	overlays.configure(shell, _theme)
	overlays.clean_changed.connect(_apply_clean)
	if source_camera != null and hud != null:
		pip = load("res://scripts/caster_pip.gd").new()
		add_child(pip)
		pip.theme = _theme
		pip.configure(world, hud, source_camera)
		overlays.pip = pip
		pip.enabled_changed.connect(func(value):
			pip_button.set_pressed_no_signal(value)
			_layout())
		pip.swapped.connect(func():
			if director.enabled:
				director.take_over())
	else:
		pip_button.disabled = true
	if shell != null:
		_palantir_level = shell.shell_top_level()
	get_viewport().size_changed.connect(_layout)
	refresh()
	_layout()


func _apply_clean(clean: bool) -> void:
	panel.visible = not clean
	if hud != null:
		hud.visible = not clean
		hud.set_broadcast_clean(clean)
	if shell != null and _palantir_level >= 0 and shell.has_level(_palantir_level):
		shell.set_level_visible(_palantir_level, not clean)
	_refresh_transport()


func _build_director(column: VBoxContainer) -> void:
	var row := HBoxContainer.new()
	column.add_child(row)
	director_button = _button("Director [D]")
	director_button.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	director_button.text_overrun_behavior = TextServer.OVERRUN_NO_TRIMMING
	director_button.disabled = hud == null
	director_button.toggled.connect(_set_director)
	row.add_child(director_button)
	director_label = Label.new()
	director_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(director_label)
	var tune := _button("Tune director")
	tune.toggle_mode = false
	tune.size_flags_horizontal = Control.SIZE_SHRINK_END
	tune.text_overrun_behavior = TextServer.OVERRUN_NO_TRIMMING
	row.add_child(tune)
	pip_button = _button("PiP [F7]")
	pip_button.size_flags_horizontal = Control.SIZE_SHRINK_END
	pip_button.text_overrun_behavior = TextServer.OVERRUN_NO_TRIMMING
	pip_button.tooltip_text = "Pin a second location. Shift+F7 pins the current view. Shares vision and camera angle."
	pip_button.toggled.connect(func(value):
		if pip != null:
			pip.set_enabled(value))
	row.add_child(pip_button)
	director_popup = PopupPanel.new()
	director_popup.theme = _theme
	add_child(director_popup)
	director_popup.about_to_popup.connect(func():
		if hud != null:
			hud.set_input_enabled(false))
	director_popup.popup_hide.connect(func():
		if hud != null:
			hud.set_input_enabled(true))
	var body := PanelContainer.new()
	var background := StyleBoxFlat.new()
	background.bg_color = INK
	background.border_color = GOLD
	background.set_border_width_all(1)
	background.content_margin_left = 16
	background.content_margin_right = 16
	background.content_margin_top = 16
	background.content_margin_bottom = 16
	body.add_theme_stylebox_override("panel", background)
	director_popup.add_child(body)
	var settings := VBoxContainer.new()
	body.add_child(settings)
	var title := Label.new()
	title.text = "Director · event priorities (0 disables)"
	title.add_theme_color_override("font_color", GOLD)
	settings.add_child(title)
	for key in director.CATEGORIES:
		var spin := _director_number(settings, director.CATEGORIES[key], 0, 10, 1, director.priorities[key])
		spin.value_changed.connect(func(value): director.priorities[key] = int(value))
		director_options["priority:" + key] = spin
	for option in [["dwell_seconds", "Minimum dwell (seconds)", 1, 60, 1],
		["return_seconds", "Return after idle (seconds)", 1, 120, 1],
		["move_seconds", "Ease duration (seconds)", 0.2, 10, 0.1],
		["far_cut_distance", "Cut only beyond (world units)", 500, 10000, 100],
		["camera_height", "Camera height (native limits apply)", 100, 1500, 25]]:
		var spin := _director_number(settings, option[1], option[2], option[3], option[4], director.get(option[0]))
		spin.value_changed.connect(func(value): director.set(option[0], value))
		director_options[option[0]] = spin
	var note := Label.new()
	note.text = "Actual-event feed; all vision recommended.\nManual pan, zoom or minimap click takes over immediately."
	settings.add_child(note)
	tune.pressed.connect(func(): director_popup.popup_centered(Vector2i(650 * _theme.default_font_size / 20.0, 0)))


func _director_number(parent: VBoxContainer, text: String, low: float, high: float, step: float, value: float) -> SpinBox:
	var row := HBoxContainer.new()
	parent.add_child(row)
	var label := Label.new()
	label.text = text
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(label)
	var spin := SpinBox.new()
	spin.min_value = low
	spin.max_value = high
	spin.step = step
	spin.value = value
	row.add_child(spin)
	return spin


func _set_director(on: bool) -> void:
	director.set_enabled(on and hud != null)
	director_button.set_pressed_no_signal(director.enabled)


func restore_director(preferences: Dictionary) -> void:
	director.restore_preferences(preferences)
	director.take_over()
	director_button.set_pressed_no_signal(director.enabled)
	for key in director_options:
		var value: float = director.priorities[key.trim_prefix("priority:")] if key.begins_with("priority:") else director.get(key)
		director_options[key].set_value_no_signal(value)


func _director_tick(delta: float) -> void:
	if hud == null or not visible:
		return
	var camera: Dictionary = hud.get_camera()
	var pose: Dictionary = director.tick(_recent_events, world.get_presented_frame(), delta, camera)
	if not pose.is_empty():
		hud.camera_look_at(pose.position)
		hud.camera_set_height(pose.height)
	director.remember_pose(hud.get_camera())
	if not director.enabled:
		director_label.text = "Free camera"
	elif director.manual_remaining > 0:
		director_label.text = "Manual camera · director returns in %.1fs idle" % director.manual_remaining
	elif director.choice.is_empty():
		director_label.text = "Director · waiting for a recent event"
	else:
		director_label.text = "Director · %s · %s" % [event_name(director.choice), director.choice.name]


func _build_transport(background: StyleBoxFlat) -> void:
	transport = PanelContainer.new()
	transport.name = "ReplayTransport"
	transport.theme = _theme
	transport.add_theme_stylebox_override("panel", background.duplicate())
	add_child(transport)
	var column := VBoxContainer.new()
	transport.add_child(column)
	var row := HBoxContainer.new()
	column.add_child(row)
	play_button = _button("Pause [Space]")
	play_button.toggle_mode = false
	play_button.pressed.connect(_toggle_play)
	row.add_child(play_button)
	step_button = _button("Step [.]")
	step_button.toggle_mode = false
	step_button.pressed.connect(_step_frame)
	row.add_child(step_button)
	speed_button = OptionButton.new()
	speed_button.focus_mode = Control.FOCUS_NONE
	for speed in SPEEDS:
		speed_button.add_item("%s×" % speed)
	speed_button.item_selected.connect(_speed_changed)
	row.add_child(speed_button)
	clock_label = Label.new()
	clock_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	clock_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	row.add_child(clock_label)
	marker_button = OptionButton.new()
	marker_button.focus_mode = Control.FOCUS_NONE
	marker_button.fit_to_longest_item = false
	marker_button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	marker_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	marker_button.item_selected.connect(_jump_marker)
	row.add_child(marker_button)
	timeline = Timeline.new()
	timeline.min_value = 1
	timeline.step = 1
	timeline.focus_mode = Control.FOCUS_NONE
	timeline.custom_minimum_size.y = 28
	timeline.drag_started.connect(func(): _scrubbing = true)
	timeline.drag_ended.connect(_seek_released)
	column.add_child(timeline)
	var note_row := HBoxContainer.new()
	column.add_child(note_row)
	replay_note = Label.new()
	replay_note.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_note.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	replay_note.add_theme_color_override("font_color", Color("aebbc2"))
	note_row.add_child(replay_note)
	go_live_button = _button("Go live [G]")
	go_live_button.toggle_mode = false
	go_live_button.size_flags_horizontal = Control.SIZE_SHRINK_END
	go_live_button.text_overrun_behavior = TextServer.OVERRUN_NO_TRIMMING
	go_live_button.tooltip_text = "Catch up to the received edge, not the players' undelayed game."
	go_live_button.pressed.connect(_go_live)
	note_row.add_child(go_live_button)


static func frame_time(frame: int) -> String:
	var seconds := maxi(0, frame - 1) / 5
	return "%02d:%02d" % [seconds / 60, seconds % 60]


static func event_name(event: Dictionary) -> String:
	match int(event.kind):
		0: return "Base under attack" if event.base else "Fight"
		1: return "Hero killed" if event.hero else "Building lost"
		2: return "Power used"
		3: return "Base ownership changed" if event.base else "Building ownership changed"
	return "Event"


func _refresh_transport() -> void:
	var state: Dictionary = world.caster_transport_status()
	_refresh_events(int(state.get("frame", world.get_presented_frame())))
	transport.visible = state.get("active", false) and not overlays.clean
	if not transport.visible:
		return
	var live: bool = state.get("live", false)
	var failed: bool = state.mismatches > 0 or not str(state.get("failure", "")).is_empty() or not transport_error.is_empty()
	play_button.text = "Play [Space]" if world.is_paused() else "Pause [Space]"
	play_button.disabled = failed or state.finished
	step_button.disabled = failed or state.frame >= state.final_frame
	go_live_button.visible = live
	go_live_button.disabled = failed
	speed_button.select(SPEEDS.find(world.get_time_scale()))
	timeline.max_value = maxi(1, state.final_frame)
	if not _scrubbing:
		timeline.set_value_no_signal(state.frame)
	clock_label.text = "%s / %s · frame %d" % [frame_time(state.frame), frame_time(state.final_frame), state.frame]
	timeline.indexed_frame = indexed_frame
	timeline.queue_redraw()
	replay_note.text = "Indexed through %s · orange: fights · red: deaths · blue: powers · gold: captures · rewind rebuilds from start" % frame_time(indexed_frame)
	if live:
		var behind: int = maxi(0, int(state.final_frame) - int(state.frame))
		var status := "RECEIVED EDGE" if behind == 0 else "%.1fs behind received edge" % (behind / 5.0)
		if state.finished:
			status = "END OF RECEIVED GAME"
		elif world.is_paused():
			status = "PAUSED · " + status
		clock_label.text = "%s / %s received" % [frame_time(state.frame), frame_time(state.final_frame)]
		replay_note.text = "Public delay %ds · %s · CRCs checked %d · rewind rebuilds from start" % [int(state.delay), status, int(state.hashes_compared)]
	if not str(state.get("failure", "")).is_empty():
		replay_note.text = "STOPPED: " + str(state.failure)
	elif state.mismatches > 0:
		replay_note.text = "STOPPED: recorded hash mismatch at frame %d" % state.first_mismatch_frame
	elif not live and (not state.complete or state.lost > 0):
		replay_note.text = "Incomplete recording: deterministic seeking unavailable"
	timeline.editable = state.get("seekable", false) and not failed
	if not transport_error.is_empty():
		replay_note.text = transport_error
		play_button.disabled = true
		step_button.disabled = true
	replay_note.tooltip_text = replay_note.text
	_layout()


func _refresh_events(frame: int) -> void:
	var changed := false
	for event in world.caster_events(maxi(0, _event_frame - 25)):
		if (event.kind == 1 and not event.hero and not event.structure) or (event.kind == 3 and not event.structure):
			continue
		var key := "%d:%d:%d" % [event.frame, event.kind, event.object]
		changed = changed or not history.has(key)
		history[key] = event
	_event_frame = frame
	indexed_frame = maxi(indexed_frame, frame)
	_recent_events = history.values().filter(func(event): return event.frame <= frame and event.frame >= frame - director.lookback_frames)
	if changed or marker_button.item_count == 0:
		timeline.events = history.values()
		timeline.events.sort_custom(func(a, b): return a.frame < b.frame)
		marker_button.clear()
		marker_button.add_item("Jump to event (%d)" % history.size())
		for event in timeline.events:
			marker_button.add_item("%s · %s · %s" % [frame_time(event.frame), event_name(event), event.name])
			marker_button.set_item_metadata(marker_button.item_count - 1, int(event.frame))


func _toggle_play() -> void:
	if not transport_error.is_empty():
		return
	world.caster_playback(not world.is_paused(), world.get_time_scale())
	_refresh_transport()


func _go_live() -> void:
	var state: Dictionary = world.caster_transport_status()
	if state.get("live", false) and state.get("seekable", false) and transport_error.is_empty():
		go_live_requested.emit()


func _step_frame() -> void:
	if not transport_error.is_empty():
		return
	var result: Dictionary = world.caster_step()
	_refresh_transport()
	if result.has("error"):
		replay_note.text = result.error


func _speed_changed(index: int) -> void:
	world.caster_playback(world.is_paused(), SPEEDS[index])


func _seek_released(changed: bool) -> void:
	_scrubbing = false
	if changed:
		seek_requested.emit(int(timeline.value))


func _jump_marker(index: int) -> void:
	if index > 0:
		seek_requested.emit(int(marker_button.get_item_metadata(index)))


func _button(text: String) -> Button:
	var button := Button.new()
	button.text = text
	button.toggle_mode = true
	button.focus_mode = Control.FOCUS_NONE
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color("202d36")
	normal.set_content_margin_all(9)
	var selected: StyleBoxFlat = normal.duplicate()
	selected.bg_color = Color("48504a")
	selected.border_color = GOLD
	selected.border_width_bottom = 2
	button.add_theme_stylebox_override("normal", normal)
	button.add_theme_stylebox_override("pressed", selected)
	button.add_theme_stylebox_override("hover", selected)
	return button


func _layout() -> void:
	var size := get_viewport().get_visible_rect().size
	var factor := minf(size.x / 1920.0, size.y / 1080.0)
	panel.position = Vector2(48, 24) * factor
	panel.size = Vector2(size.x - 96 * factor, 0)
	transport.size = Vector2(size.x * 0.635, 0)
	transport.position = Vector2(size.x * 0.34, size.y - 24 * factor - transport.size.y)
	if _theme.default_font_size != roundi(20 * factor):
		_theme.default_font_size = roundi(20 * factor)
	if shell != null:
		var face: Dictionary = shell.shell_font("Albertus MT", _theme.default_font_size)
		if face.get("font") != null and _theme.default_font != face.font:
			_theme.default_font = face.font
	for button in player_buttons.values():
		button.custom_minimum_size.x = 120 * factor
	if overlays != null:
		overlays.transport_top = transport.position.y
		overlays.layout(panel.position.y + panel.size.y)


func _process(delta: float) -> void:
	if suspended:
		return
	_director_tick(delta)
	_elapsed += delta
	if _elapsed < 0.2 or world == null:
		return
	if not world.is_logic_held(): # RENDERWAIT-1: the refresh below reads the live logic (the caster status and metrics)
		return
	_elapsed = 0.0
	refresh()


func refresh() -> void:
	var state: Dictionary = world.caster_status()
	visible = state.get("active", false) and not suspended
	if not visible:
		return
	var new_players: Array = state.players
	if players != new_players:
		players = new_players
		for button in player_buttons.values():
			player_row.remove_child(button)
			button.queue_free()
		player_buttons.clear()
		if team_button.get_parent() != null:
			player_row.remove_child(team_button)
		for p in players:
			var button := _button("%d  %s" % [int(p.slot) + 1, str(p.name)])
			button.tooltip_text = "%s — %s" % [p.name, str(p.faction).trim_prefix("Faction")]
			button.add_theme_color_override("font_color", Color.hex((int(p.color) << 8) | 255).lightened(0.4))
			button.pressed.connect(select_player.bind(int(p.slot)))
			player_row.add_child(button)
			player_buttons[int(p.slot)] = button
		player_row.add_child(team_button)
		_layout()
	observed = int(state.observed.index)
	team = bool(state.team)
	all_button.set_pressed_no_signal(observed < 0)
	team_button.set_pressed_no_signal(team)
	team_button.disabled = observed < 0
	for p in players:
		player_buttons[int(p.slot)].set_pressed_no_signal(int(p.index) == observed)
	mode_label.text = "LIVE SPECTATOR" if state.live else "REPLAY / OBSERVER"
	var vision := "All vision" if observed < 0 else str(state.observed.display_name) + (" + allies" if team else " POV")
	status_label.text = vision + ("  ·  Director — no recorded camera" if director.enabled else "  ·  Free camera — no recorded camera")
	if pip != null:
		pip.label.text = "PINNED VIEW · " + vision
	_refresh_transport()
	overlays.update_data(world.caster_metrics(overlays.next_sample), players, history.values())


func select_all() -> void:
	if world.caster_observe(-1):
		refresh()


func select_player(slot: int) -> void:
	for p in players:
		if int(p.slot) == slot:
			if world.caster_observe(int(p.index), team):
				refresh()
			return


func _set_team(enabled: bool) -> void:
	if observed >= 0 and world.caster_observe(observed, enabled):
		refresh()


func set_suspended(value: bool) -> void:
	suspended = value
	if pip != null:
		pip.set_suspended(value)
	if hud != null:
		hud.set_input_enabled(true)
	refresh()


func _input(event: InputEvent) -> void:
	if is_processing() and handle_input(event):
		get_viewport().set_input_as_handled()


func handle_input(event: InputEvent) -> bool:
	if suspended:
		return false
	if director_popup.visible:
		if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
			director_popup.hide()
			return true
		return false
	var over_pip: bool = pip != null and event is InputEventMouse and pip.over_panel(event.position)
	if hud != null and event is InputEventMouse:
		var over: bool = over_pip or overlays.over_panel(event.position) or (panel.visible and panel.get_global_rect().has_point(event.position)) or (transport.visible and transport.get_global_rect().has_point(event.position))
		hud.set_input_enabled(not over)
	elif hud != null and event is InputEventKey:
		hud.set_input_enabled(true)
	if visible and director.enabled and not over_pip:
		if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
			director.take_over()
		elif event is InputEventKey and event.pressed and event.keycode in [KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN]:
			director.take_over()
	if not visible or not event is InputEventKey or not event.pressed or event.echo or event.ctrl_pressed or event.alt_pressed or event.meta_pressed:
		return false
	if event.keycode >= KEY_F1 and event.keycode <= KEY_F6:
		var key: String = overlays.TITLES.keys()[event.keycode - KEY_F1]
		overlays.set_panel(key, not overlays.enabled[key])
	elif event.keycode == KEY_F7 and pip != null:
		if event.shift_pressed:
			pip.pin_here()
			pip.set_enabled(true)
		else:
			pip.set_enabled(not pip.enabled)
	elif event.keycode == KEY_F9:
		overlays.set_clean(not overlays.clean)
	elif event.keycode == KEY_ESCAPE and overlays.clean:
		overlays.set_clean(false)
		return false
	elif event.keycode == KEY_0:
		select_all()
	elif event.keycode >= KEY_1 and event.keycode <= KEY_8:
		select_player(event.keycode - KEY_1)
	elif event.keycode == KEY_T:
		_set_team(not team)
	elif event.keycode == KEY_D and hud != null:
		_set_director(not director.enabled)
	elif event.keycode == KEY_G:
		_go_live()
	elif event.keycode == KEY_SPACE and (transport.visible or overlays.clean):
		_toggle_play()
	elif event.keycode == KEY_PERIOD and (transport.visible or overlays.clean):
		_step_frame()
	else:
		return false
	return true
