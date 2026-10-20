## Test keyboard-request ownership without invoking a platform soft keyboard.
extends SceneTree
class Bridge extends "res://scripts/release/ios_keyboard.gd":
	var opens := 0
	func _show_keyboard() -> void: opens += 1
class Shell extends RefCounted:
	var field := {"focused":true,"id":1,"text":"Player","rect":Rect2(10,10,200,40)}
	var edits: Array = []
	func native_text_input() -> Dictionary: return field
	func edit_native_text(text: String, submit: bool) -> void: edits.append([text,submit])
func _initialize() -> void: call_deferred("run")
func run() -> void:
	var shell := Shell.new()
	var bridge := Bridge.new()
	bridge.shell = func(): return shell
	root.add_child(bridge)
	bridge.set_process(false)
	bridge._process(0)
	assert(not bridge.editor.virtual_keyboard_enabled)
	assert(bridge.opens == 1)
	assert(bridge.editor.text == "Player")
	bridge._request_keyboard(false)
	assert(bridge.opens == 1)
	for i in 20: bridge._process(0)
	assert(bridge.opens == 1)
	shell.field.id = 2
	shell.field.text = "Another"
	bridge._process(0)
	assert(bridge.opens == 2)
	assert(bridge.editor.text == "Another")
	bridge.editor.text_changed.emit("Edited")
	assert(shell.edits == [["Edited",false]])
	shell.field.focused = false
	bridge._process(0)
	assert(bridge.focus_id == 0)
	bridge.free()
	print("IOS KEYBOARD PASS")
	quit()
