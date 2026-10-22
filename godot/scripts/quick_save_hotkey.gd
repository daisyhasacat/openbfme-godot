## OpenBFME Quick Save. Kept after the game under the root so the HUD cannot
## enqueue this chord as a gameplay key before it is consumed.
extends CanvasLayer
var available: Callable
var save: Callable
var _held := false
var _until := 0
var _message: Label
func _ready() -> void:
	layer = 120
	_message = Label.new()
	_message.position = Vector2(24, 64)
	_message.add_theme_font_size_override("font_size", 20)
	_message.add_theme_color_override("font_outline_color", Color.BLACK)
	_message.add_theme_constant_override("outline_size", 5)
	_message.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_message.set_meta("game_text", true)
	_message.hide()
	add_child(_message)
func _input(event: InputEvent) -> void:
	if not event is InputEventKey or event.keycode != KEY_S: return
	if _held:
		get_viewport().set_input_as_handled()
		if not event.pressed: _held = false
		return
	if not event.pressed or event.echo or not event.ctrl_pressed or not event.shift_pressed or event.alt_pressed or event.meta_pressed: return
	if not available.is_valid() or not available.call(): return
	var focus := get_viewport().gui_get_focus_owner()
	if focus is LineEdit or focus is TextEdit: return
	_held = true
	get_viewport().set_input_as_handled()
	var result: Dictionary = save.call()
	_message.text = "Quick Save saved." if result.get("ok", false) else "Quick Save failed: " + str(result.get("error", "Unknown error"))
	_message.modulate = Color.WHITE if result.get("ok", false) else Color(1, 0.6, 0.6)
	_message.show()
	_until = Time.get_ticks_msec() + 5000
func _process(_delta: float) -> void:
	if Time.get_ticks_msec() >= _until: _message.hide()
func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT: _held = false
func _exit_tree() -> void:
	available = Callable()
	save = Callable()
