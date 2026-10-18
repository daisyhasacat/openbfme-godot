## Rendered proof using the command-recorded encounter from caster_director_test.gd.
extends "res://tests/caster_transport_walk.gd"


func seconds(value: float) -> void:
	await host.get_tree().create_timer(value).timeout


func type_number(spin: SpinBox, popup: PopupPanel, text: String) -> void:
	var field := spin.get_line_edit()
	await click(Vector2(popup.position) + field.get_global_rect().get_center())
	var event := InputEventKey.new()
	event.keycode = KEY_A
	event.ctrl_pressed = true
	event.pressed = true
	Input.parse_input_event(event)
	await frames(1)
	for character in text:
		event = InputEventKey.new()
		event.unicode = character.unicode_at(0)
		event.keycode = character.unicode_at(0)
		event.pressed = true
		Input.parse_input_event(event)
		await frames(1)
	await key(KEY_ENTER)


func run(game: Node, output: String) -> int:
	host = game
	directory = output
	check(DirAccess.make_dir_recursive_absolute(output) == OK, "director capture directory")
	var file := FileAccess.open(OS.get_user_data_dir().path_join("caster-director.json"), FileAccess.READ)
	check(file != null, "scripted replay metadata exists")
	if file == null:
		return failures
	var metadata: Dictionary = JSON.parse_string(file.get_as_text())
	file.close()
	var hero_ids: Array[int] = []
	for id in metadata.heroes:
		hero_ids.append(int(id))
	await frames(6)
	await key(KEY_SPACE)
	check(host._world.is_paused(), "pause before scripted encounter")
	await host._caster_seek(int(metadata.frame))
	await finish_seek()
	check(host._world.replay_status().mismatches == 0, "scripted encounter seek matches CRCs")
	var midpoint := Vector2(metadata.x, metadata.y)
	host._hud.camera_look_at(midpoint + Vector2(500, 350))
	var motion := InputEventMouseMotion.new()
	motion.position = host.get_viewport().get_visible_rect().size * 0.5
	motion.global_position = motion.position
	Input.parse_input_event(motion)
	await frames()
	var initial: Vector2 = host._hud.get_camera().position
	var hash_before: int = host._world.get_state_hash()
	await key(KEY_D)
	check(host._caster.director.enabled, "D enables director")
	await seconds(0.35)
	var choice: Dictionary = host._caster.director.choice
	print("CASTER DIRECTOR CHOICE ", choice, " EXPECTED ", metadata)
	check(not choice.is_empty(), "director selects an actual event")
	if choice.is_empty():
		return failures
	check(int(choice.object) in hero_ids and choice.position.distance_to(midpoint) < 400, "director chooses the scripted fight")
	var moving: Vector2 = host._hud.get_camera().position
	check(moving.distance_to(initial) > 1 and moving.distance_to(choice.position) > 1, "nearby camera move has an intermediate pose")
	await seconds(2.2)
	check(host._hud.get_camera().position.distance_to(choice.position) < 1, "ease arrives at the encounter")
	await capture("director-following")
	var tune: Button = host._caster.director_button.get_parent().get_child(2)
	await click(tune.get_global_rect().get_center())
	check(host._caster.director_popup.visible, "director tuning opens")
	await type_number(host._caster.director_options.return_seconds, host._caster.director_popup, "1")
	check(host._caster.director.return_seconds == 1.0, "idle-return tuning reaches the policy")
	await capture("director-tuning")
	await key(KEY_ESCAPE)
	check(not host._caster.director_popup.visible and not host._quit_open, "Escape closes tuning without opening quit")
	await key(KEY_RIGHT)
	check(host._caster.director.manual_remaining > 0, "native camera input takes over immediately")
	await capture("director-manual")
	await seconds(3.5)
	check(host._caster.director.manual_remaining == 0 and not host._caster.director.choice.is_empty(), "director returns after configured idle interval")
	check(host._hud.get_camera().position.distance_to(choice.position) < 1, "director eases back to the fight")
	check(host._world.get_state_hash() == hash_before, "camera and tuning never mutate paused simulation")
	await capture("director-returned")
	await host._caster_seek(int(metadata.frame) - 1)
	await finish_seek()
	check(host._caster.director.enabled and host._caster.director.return_seconds == 1.0, "rewind preserves director preferences")
	check(host._world.replay_status().mismatches == 0, "rewind with director still matches CRCs")
	check(host._caster.panel.get_global_rect().end.x <= host.get_viewport().get_visible_rect().size.x - 8, "director controls stay inside safe layout")
	print("CASTER DIRECTOR UI RESULT failures=", failures)
	return failures
