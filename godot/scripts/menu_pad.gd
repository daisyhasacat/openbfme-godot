## Lane OPTS-6: the game controller in the menus (NOT RETAIL: an OpenBFME improvement; RotWK's menus take the mouse only). Godot's joypad events become
## the keys the menus' keyboard navigation reads (GameClient/GUI/MenuNavigator.h), sent through Godot's input as key events, so the controller and the
## keyboard take one path:
##   D-pad, left stick          the arrows (a held direction repeats: first after 0.4 s, then every 0.12 s)
##   A / B                      Enter / Esc
##   LB / RB                    PageUp / PageDown (the tabs)
##   triggers                   skirmish focus sections; Left / Right elsewhere
##   right stick (x)             Left / Right (sliders, choices)
##   Start                      Esc (also over a game without a menu: the in-game menu opens, as Esc opens it)
## Only while a menu has the input (AptMenuPlayer.menu_has_input), Start always. take_log() lists the keys sent ("down KEY_UP" ...), for the tests.
extends Node

const DEADZONE := 0.5
const REPEAT_FIRST := 0.4
const REPEAT_NEXT := 0.12
const BUTTONS := {
	JOY_BUTTON_DPAD_UP: KEY_UP, JOY_BUTTON_DPAD_DOWN: KEY_DOWN, JOY_BUTTON_DPAD_LEFT: KEY_LEFT, JOY_BUTTON_DPAD_RIGHT: KEY_RIGHT,
	JOY_BUTTON_A: KEY_ENTER, JOY_BUTTON_B: KEY_ESCAPE, JOY_BUTTON_LEFT_SHOULDER: KEY_PAGEUP, JOY_BUTTON_RIGHT_SHOULDER: KEY_PAGEDOWN,
	JOY_BUTTON_START: KEY_ESCAPE,
}
## axis -> [key below -DEADZONE, key above +DEADZONE] (a trigger only goes positive)
const AXES := {
	JOY_AXIS_LEFT_X: [KEY_LEFT, KEY_RIGHT], JOY_AXIS_LEFT_Y: [KEY_UP, KEY_DOWN], JOY_AXIS_RIGHT_X: [KEY_LEFT, KEY_RIGHT],
	JOY_AXIS_TRIGGER_LEFT: [KEY_NONE, KEY_LEFT], JOY_AXIS_TRIGGER_RIGHT: [KEY_NONE, KEY_RIGHT],
}
const REPEATING := [KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT]

var start_lobby: Callable # returns true when Start was handled by the skirmish lobby
var _start_consumed := false
var _shell: Node
var _section_trigger := {}
var _section_captured := {}
var _axis_key := {}   # axis -> the key it holds now (KEY_NONE: none)
var _held := {}       # key -> the time (s) of its next repeat
var _log := PackedStringArray()


func _init(shell: Node) -> void:
	_shell = shell
	name = "MenuPad"
	process_mode = Node.PROCESS_MODE_ALWAYS


func take_log() -> PackedStringArray:
	var out := _log
	_log = PackedStringArray()
	return out


func _active() -> bool:
	return _shell != null and _shell.menu_has_input()


func _input(event: InputEvent) -> void:
	if event is InputEventJoypadButton:
		var b := event as InputEventJoypadButton
		if b.button_index == JOY_BUTTON_START:
			if b.pressed and start_lobby.is_valid() and start_lobby.call():
				_start_consumed = true
				get_viewport().set_input_as_handled()
				return
			if not b.pressed and _start_consumed:
				_start_consumed = false
				get_viewport().set_input_as_handled()
				return
		if not BUTTONS.has(b.button_index):
			return
		if not b.pressed or _active() or b.button_index == JOY_BUTTON_START:
			_send(BUTTONS[b.button_index], b.pressed, false)
			get_viewport().set_input_as_handled()
	elif event is InputEventJoypadMotion:
		var m := event as InputEventJoypadMotion
		if m.axis in [JOY_AXIS_TRIGGER_LEFT, JOY_AXIS_TRIGGER_RIGHT]:
			var pressed := m.axis_value >= DEADZONE
			var had: bool = _section_trigger.get(m.axis, false)
			_section_trigger[m.axis] = pressed
			if pressed and not had and _active() and _shell.has_method("menu_controller_section"):
				_section_captured[m.axis] = _shell.menu_controller_section(-1 if m.axis == JOY_AXIS_TRIGGER_LEFT else 1)
			if _section_captured.get(m.axis, false):
				if not pressed:
					_section_captured.erase(m.axis)
				get_viewport().set_input_as_handled()
				return
		if not AXES.has(m.axis):
			return
		var keys: Array = AXES[m.axis]
		var want: int = KEY_NONE
		if m.axis_value <= -DEADZONE:
			want = keys[0]
		elif m.axis_value >= DEADZONE:
			want = keys[1]
		var had: int = _axis_key.get(m.axis, KEY_NONE)
		if want == had:
			return
		if had != KEY_NONE:
			_send(had, false, false)
		_axis_key[m.axis] = want
		if want != KEY_NONE and _active():
			_send(want, true, false)
		get_viewport().set_input_as_handled()


func _process(_delta: float) -> void:
	var now := Time.get_ticks_msec() / 1000.0
	for key in _held.keys():
		if now >= float(_held[key]):
			if not _active():
				_held.erase(key)
				continue
			_send(key, true, true)


func _send(key: int, pressed: bool, echo: bool) -> void:
	var ev := InputEventKey.new()
	ev.keycode = key as Key
	ev.physical_keycode = key as Key
	ev.pressed = pressed
	ev.echo = echo
	ev.set_meta("menu_controller", true)
	_log.append("%s %s%s" % ["down" if pressed else "up", OS.get_keycode_string(key), " (repeat)" if echo else ""])
	if pressed and REPEATING.has(key):
		_held[key] = Time.get_ticks_msec() / 1000.0 + (REPEAT_NEXT if echo else REPEAT_FIRST)
	elif not pressed:
		_held.erase(key)
	Input.parse_input_event(ev)
