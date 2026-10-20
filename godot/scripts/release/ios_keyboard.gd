extends Control
## A transparent Godot editor gives native game fields iOS IME/keyboard support.
var shell: Callable
var editor: LineEdit
var focus_id := 0
var field_rect := Rect2()
var _keyboard_request_ms := -1000
func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	editor = LineEdit.new()
	# Own keyboard presentation explicitly; LineEdit focus/caret changes must
	# not reset UIKit's editor while it is opening or composing text.
	editor.virtual_keyboard_enabled = false
	editor.modulate.a = 0.0
	editor.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(editor)
	editor.text_changed.connect(func(text: String) -> void:
		var live = shell.call()
		if live != null and focus_id != 0: live.edit_native_text(text, false))
	editor.text_submitted.connect(func(text: String) -> void:
		var live = shell.call()
		if live != null and focus_id != 0: live.edit_native_text(text, true)
		DisplayServer.virtual_keyboard_hide())
func _process(_delta: float) -> void:
	var live = shell.call()
	var field: Dictionary = live.native_text_input() if live != null else {}
	if not field.get("focused", false):
		if focus_id != 0:
			editor.release_focus()
			DisplayServer.virtual_keyboard_hide()
		focus_id = 0
		return
	field_rect = field.rect
	editor.position = field_rect.position
	editor.size = field_rect.size
	if focus_id != field.id:
		focus_id = field.id
		editor.text = field.text
		editor.caret_column = editor.text.length()
		editor.grab_focus()
		_request_keyboard(true)
	elif editor.text != field.text and not editor.has_focus():
		editor.text = field.text
func _input(event: InputEvent) -> void:
	if event is InputEventScreenTouch and event.pressed and focus_id != 0 and field_rect.has_point(event.position) and not DisplayServer.has_hardware_keyboard():
		editor.grab_focus()
		_request_keyboard(false)

func _request_keyboard(new_field: bool) -> void:
	# Height may remain zero during UIKit's opening animation. Do not send a
	# second responder request during that interval, or while already visible.
	var now := Time.get_ticks_msec()
	if not new_field and (DisplayServer.virtual_keyboard_get_height() > 0 or now - _keyboard_request_ms < 500): return
	_keyboard_request_ms = now
	_show_keyboard()

func _show_keyboard() -> void:
	if DisplayServer.has_hardware_keyboard(): return
	DisplayServer.virtual_keyboard_show(editor.text, field_rect, DisplayServer.KEYBOARD_TYPE_DEFAULT, -1, editor.caret_column)
