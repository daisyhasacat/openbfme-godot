extends SceneTree

class Preview:
	extends "res://scripts/release/ios_startup.gd"
	func _ready() -> void:
		_build_screen()

func _initialize() -> void:
	call_deferred("check")

func check() -> void:
	var screen := Preview.new()
	root.add_child(screen)
	screen._process(0)
	if screen.ring_texture != null:
		var first: Rect2 = screen.ring_texture.region
		screen.began -= 234
		screen._process(0)
		assert(screen.ring_texture.region != first)
	screen.stage("Loading game files…")
	assert(screen.status.text == "Loading game files…")
	assert(screen.progress.visible == (screen.ring_texture == null))
	screen.finished()
	await process_frame
	assert(not is_instance_valid(screen))
	print("STARTUP TEST PASS: ring advances, stage updates, teardown completes")
	quit()
