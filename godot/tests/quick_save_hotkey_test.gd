extends SceneTree
var calls := 0
var enabled := true
var fail := false
var loads := 0
class Gameplay extends Node:
	var received := 0
	func _input(event: InputEvent) -> void:
		if event is InputEventKey and event.keycode == KEY_S: received += 1
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var gameplay := Gameplay.new(); root.add_child(gameplay)
	var hotkey = load("res://scripts/quick_save_hotkey.gd").new()
	hotkey.available = func(): return enabled
	hotkey.save = func():
		calls += 1
		return {"ok": not fail, "error": "Network game"}
	hotkey.load_available = func(): return true
	hotkey.quick_load = func():
		loads += 1
		return {"ok": true}
	root.add_child(hotkey)
	var key := InputEventKey.new(); key.keycode = KEY_S; key.pressed = true; key.ctrl_pressed = true; key.shift_pressed = true
	root.push_input(key)
	await process_frame
	assert(calls == 1 and gameplay.received == 0)
	assert(hotkey._message.text == "Quick Save saved.")
	key.echo = true; root.push_input(key)
	assert(calls == 1 and gameplay.received == 0)
	key.echo = false; key.pressed = false; key.ctrl_pressed = false; key.shift_pressed = false; root.push_input(key)
	assert(not hotkey._held and gameplay.received == 0)
	key.pressed = true; root.push_input(key)
	assert(calls == 1 and gameplay.received == 1) # Plain S remains Stop.
	key.ctrl_pressed = true; key.shift_pressed = true
	enabled = false; root.push_input(key); assert(calls == 1)
	enabled = true
	var entry := LineEdit.new(); root.add_child(entry); entry.grab_focus()
	root.push_input(key); assert(calls == 1)
	entry.free()
	fail = true; root.push_input(key)
	await process_frame
	assert(calls == 2 and hotkey._message.text == "Quick Save failed: Network game")
	hotkey._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	assert(not hotkey._held)
	fail = false
	key.keycode = KEY_F5; key.ctrl_pressed = false; key.shift_pressed = false
	root.push_input(key)
	await process_frame
	assert(calls == 3)
	key.pressed = false; root.push_input(key)
	# F9 remains available from menus, where saving is disabled.
	enabled = false
	key.keycode = KEY_F9; key.pressed = true; root.push_input(key)
	await process_frame
	assert(loads == 1 and calls == 3)
	assert(hotkey._message.text == "Quick Load loaded.")
	key.echo = true; root.push_input(key)
	assert(loads == 1)
	key.echo = false; key.pressed = false; root.push_input(key)
	assert(not hotkey._held)
	hotkey.free(); gameplay.free()
	print("QUICK SAVE HOTKEY PASS")
	quit()
