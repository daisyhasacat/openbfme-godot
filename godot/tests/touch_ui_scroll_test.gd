extends SceneTree
func _initialize() -> void: call_deferred("_run")

func _touch(position: Vector2, pressed: bool, canceled := false) -> void:
	var event := InputEventScreenTouch.new()
	event.position = position
	event.pressed = pressed
	event.canceled = canceled
	root.push_input(event, true)

func _drag(position: Vector2) -> void:
	var event := InputEventScreenDrag.new()
	event.position = position
	root.push_input(event, true)

func _run() -> void:
	root.size = Vector2i(800, 600)
	var list := ItemList.new()
	list.position = Vector2(20, 20)
	list.size = Vector2(200, 180)
	for index in 100: list.add_item("Map %d" % index)
	root.add_child(list)
	var scroll := ScrollContainer.new()
	scroll.position = Vector2(250, 20)
	scroll.size = Vector2(200, 180)
	root.add_child(scroll)
	var content := VBoxContainer.new()
	scroll.add_child(content)
	var presses := {"count": 0}
	for index in 20:
		var button := Button.new()
		button.text = "Action %d" % index
		button.custom_minimum_size = Vector2(180, 40)
		button.pressed.connect(func(): presses.count += 1)
		content.add_child(button)
	var handler = preload("res://scripts/touch_ui_scroll.gd").new()
	root.add_child(handler)
	await process_frame
	await process_frame
	var start := Vector2(60, 100)
	_touch(start, true)
	_drag(start - Vector2(0, 8))
	assert(list.get_v_scroll_bar().value == 0)
	assert(list.get_selected_items().is_empty())
	_drag(start - Vector2(0, 40))
	assert(list.get_v_scroll_bar().value == 40)
	_touch(start - Vector2(0, 40), false)
	assert(list.get_selected_items().is_empty())
	var tap := Vector2(60, 40)
	var expected := list.get_item_at_position(tap - list.position, true)
	_touch(tap, true)
	_touch(tap, false)
	assert(list.get_selected_items() == PackedInt32Array([expected]))
	var button_point: Vector2 = content.get_child(0).get_global_rect().get_center()
	_touch(button_point, true)
	_drag(button_point - Vector2(0, 30))
	_touch(button_point - Vector2(0, 30), false)
	assert(scroll.scroll_vertical == 30)
	assert(presses.count == 0)
	button_point = content.get_child(1).get_global_rect().get_center()
	_touch(button_point, true)
	_touch(button_point, false)
	assert(presses.count == 1)
	_touch(button_point, true)
	_touch(button_point, false, true)
	assert(presses.count == 1)
	var tree := Tree.new()
	tree.position = Vector2(480, 20)
	tree.size = Vector2(200, 180)
	root.add_child(tree)
	var tree_root := tree.create_item()
	for index in 100: tree.create_item(tree_root).set_text(0, "Entry %d" % index)
	await process_frame
	await process_frame
	_touch(Vector2(520, 100), true)
	_drag(Vector2(520, 60))
	_touch(Vector2(520, 60), false)
	assert(tree.get_scroll().y == 40)
	var text := RichTextLabel.new()
	text.position = Vector2(20, 250)
	text.size = Vector2(200, 180)
	for index in 100: text.append_text("Description line %d\n" % index)
	root.add_child(text)
	await process_frame
	await process_frame
	_touch(Vector2(60, 330), true)
	_drag(Vector2(60, 290))
	_touch(Vector2(60, 290), false)
	assert(text.get_v_scroll_bar().value == 40)
	# A Pencil delivered as a real mouse still follows normal list selection.
	var pen_point := Vector2(60, 70)
	var pen_index := list.get_item_at_position(pen_point - list.position, true)
	for down in [true, false]:
		var pen := InputEventMouseButton.new()
		pen.device = 1001
		pen.position = pen_point
		pen.button_index = MOUSE_BUTTON_LEFT
		pen.pressed = down
		root.push_input(pen, true)
	assert(list.get_selected_items() == PackedInt32Array([pen_index]))
	# Exercise Godot's real emulation order, rather than only push_input dispatch.
	list.deselect_all()
	var previous_emulation := Input.emulate_mouse_from_touch
	Input.emulate_mouse_from_touch = true
	for phase in [0, 1, 2]:
		var event: InputEvent
		if phase == 1:
			event = InputEventScreenDrag.new()
			event.position = Vector2(60, 60)
			event.relative = Vector2(0, -40)
		else:
			event = InputEventScreenTouch.new()
			event.position = Vector2(60, 100 if phase == 0 else 60)
			event.pressed = phase == 0
		Input.parse_input_event(event)
		Input.flush_buffered_events()
		assert(list.get_selected_items().is_empty())
	Input.emulate_mouse_from_touch = previous_emulation
	for focus_out in [Node.NOTIFICATION_APPLICATION_FOCUS_OUT, Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT]:
		list.deselect_all()
		_touch(Vector2(60, 100), true)
		handler.notification(focus_out)
		assert(handler._finger == -1 and handler._target == null and not handler._dragging)
		_touch(Vector2(60, 100), false)
		assert(list.get_selected_items().is_empty())
		_touch(Vector2(60, 100), true)
		_drag(Vector2(60, 60))
		assert(handler._dragging)
		handler.notification(focus_out)
		assert(handler._finger == -1 and handler._target == null and not handler._dragging)
		var stopped: float = list.get_v_scroll_bar().value
		_drag(Vector2(60, 30))
		_touch(Vector2(60, 30), false)
		assert(list.get_v_scroll_bar().value == stopped)
		_touch(button_point, true)
		handler.notification(focus_out)
		_touch(button_point, false)
		assert(presses.count == 1)
	print("TOUCH UI SCROLL PASS")
	quit()
