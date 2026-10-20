extends Node
## Finger scrolling for Godot lists and panels. Mouse and Pencil input stay native.
## Capture before GUI dispatch so a swipe cannot activate a row or a button.
const DRAG_DISTANCE := 12.0
var _finger := -1
var _origin := Vector2.ZERO
var _last := Vector2.ZERO
var _dragging := false
var _target: Control
var _sending_tap := false

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_WM_WINDOW_FOCUS_OUT:
		_finger = -1
		_target = null
		_dragging = false

func _input(event: InputEvent) -> void:
	if _sending_tap: return
	if event is InputEventMouse and event.device == InputEvent.DEVICE_ID_EMULATION:
		# Godot emits the emulated mouse press BEFORE its raw ScreenTouch.
		# Hit-test here too, otherwise the first press selects/activates on swipe.
		if _finger >= 0 or _scroll_parent(_control_at(get_viewport(), event.position)) != null:
			get_viewport().set_input_as_handled()
			return
	if event is InputEventScreenTouch:
		if event.pressed and _finger < 0:
			_target = _scroll_parent(_control_at(get_viewport(), event.position))
			if _target == null: return
			_finger = event.index
			_origin = event.position
			_last = event.position
			_dragging = false
		elif event.index == _finger:
			var tap: bool = not _dragging and not event.canceled and is_instance_valid(_target)
			_finger = -1
			if tap: _tap(event.position)
			_target = null
		else: return
		get_viewport().set_input_as_handled()
	elif event is InputEventScreenDrag and event.index == _finger:
		if not is_instance_valid(_target):
			get_viewport().set_input_as_handled()
			return
		if not _dragging and event.position.distance_to(_origin) >= DRAG_DISTANCE:
			_dragging = true
		if _dragging:
			_scroll(_last - event.position)
			_last = event.position
		get_viewport().set_input_as_handled()

func _control_at(node: Node, point: Vector2) -> Control:
	# Reverse child order matches ordinary overlay/UI draw order. Do not enter
	# preview viewports: their coordinate systems and cameras are independent.
	var children := node.get_children()
	children.reverse()
	for child in children:
		if child is SubViewport: continue
		if child is CanvasItem and not child.is_visible_in_tree(): continue
		if child is Control and child.clip_contents and not child.get_global_rect().has_point(point): continue
		var found := _control_at(child, point)
		if found != null: return found
	if node is Control and node.mouse_filter != Control.MOUSE_FILTER_IGNORE and node.is_visible_in_tree() and node.get_global_rect().has_point(point):
		return node
	return null

func _scroll_parent(control: Control) -> Control:
	var node: Node = control
	while node is Control:
		if node is ScrollContainer or node is ItemList or node is Tree or (node is RichTextLabel and node.scroll_active): return node
		node = node.get_parent()
	return null

func _scroll(delta: Vector2) -> void:
	var local_delta := delta / _target.get_global_transform().get_scale()
	if _target is ScrollContainer:
		_target.scroll_vertical += roundi(local_delta.y)
		_target.scroll_horizontal += roundi(local_delta.x)
	elif _target is Tree:
		# Tree exposes its current scroll but no setter; use its internal bars.
		for child in _target.get_children(true):
			if child is VScrollBar: child.value += local_delta.y
			elif child is HScrollBar: child.value += local_delta.x
	else:
		_target.get_v_scroll_bar().value += local_delta.y

func _tap(point: Vector2) -> void:
	# Reuse ordinary GUI hit testing, focus, selection and button activation.
	_sending_tap = true
	for down in [true, false]:
		var click := InputEventMouseButton.new()
		click.position = point
		click.global_position = point
		click.button_index = MOUSE_BUTTON_LEFT
		click.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		click.pressed = down
		get_viewport().push_input(click, true)
	_sending_tap = false
