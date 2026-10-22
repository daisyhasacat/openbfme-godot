## OpenBFME Quick Save. Kept after the game under the root so the HUD cannot
## enqueue this chord as a gameplay key before it is consumed.
extends CanvasLayer
var available: Callable
var save: Callable
var load_available: Callable
var quick_load: Callable
var _busy := false
var _held := false
var _held_key := 0
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
	if not event is InputEventKey: return
	if _held and event.keycode == _held_key:
		get_viewport().set_input_as_handled()
		if not event.pressed: _held = false
		return
	if not event.pressed or event.echo or event.alt_pressed or event.meta_pressed: return
	var plain: bool = not event.ctrl_pressed and not event.shift_pressed
	var loading: bool = plain and event.keycode == KEY_F9
	var saving: bool = (plain and event.keycode == KEY_F5) or (event.keycode == KEY_S and event.ctrl_pressed and event.shift_pressed)
	if not loading and not saving: return
	if _busy:
		get_viewport().set_input_as_handled()
		return
	var allowed := load_available if loading else available
	if not allowed.is_valid() or not allowed.call(): return
	var focus := get_viewport().gui_get_focus_owner()
	if event.keycode == KEY_S and (focus is LineEdit or focus is TextEdit): return
	_held = true
	_held_key = event.keycode
	get_viewport().set_input_as_handled()
	_busy = true
	_run.call_deferred(loading)
func _run(loading: bool) -> void:
	var action := quick_load if loading else save
	var result: Dictionary = await action.call()
	_busy = false
	var title := "Quick Load" if loading else "Quick Save"
	_message.text = title + (" loaded." if loading else " saved.") if result.get("ok", false) else title + " failed: " + str(result.get("error", "Unknown error"))
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
	load_available = Callable()
	quick_load = Callable()
