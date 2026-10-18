## Rendered 1080p/1440p broadcast proof using caster-overlay.replay.
extends "res://tests/caster_director_walk.gd"


func bounds() -> void:
	var viewport: Rect2 = host.get_viewport().get_visible_rect()
	var visible_panels: Array = []
	for panel in host._caster.overlays.panels.values():
		if panel.visible:
			var rect: Rect2 = panel.get_global_rect()
			check(viewport.encloses(rect), "overlay inside viewport: " + str(rect))
			for other in visible_panels:
				check(not rect.intersects(other), "overlay panels do not overlap")
			visible_panels.append(rect)


func run(game: Node, output: String) -> int:
	host = game
	directory = output
	check(DirAccess.make_dir_recursive_absolute(output) == OK, "overlay capture directory")
	await frames(6)
	await key(KEY_SPACE)
	check(host._world.is_paused(), "pause before overlay checks")
	if not host._world.is_paused():
		return failures
	await host._caster_seek(30)
	await finish_seek()
	for code in [KEY_F2, KEY_F3, KEY_F4, KEY_F5]:
		await key(code)
	await frames(20)
	check(host._caster.overlays.labels.production.text.contains("RohanEomer"), "real production visible")
	check(host._caster.overlays.labels.production.text.contains("Upgrade_MenFortressBanners"), "real upgrade visible")
	bounds()
	await capture("overlay-production")
	await host._caster_seek(305)
	await finish_seek()
	host._hud.camera_look_at(Vector2(1086, 1270))
	await frames(20)
	var before: int = host._world.get_state_hash()
	check(host._caster.overlays.labels.heroes.text.contains("HP"), "living hero health visible")
	await capture("overlay-panels")
	for index in [1, 2]:
		var selector: OptionButton = host._caster.overlays.graph_selector
		await click(selector.get_global_rect().get_center())
		await popup_item(selector.get_popup(), index)
		check(selector.selected == index, "graph tab selected")
		await capture("overlay-graph-" + str(index))
	await key(KEY_F9)
	check(host._caster.overlays.clean and not host._caster.panel.visible and not host._caster.transport.visible and not host._hud.visible, "clean hides controls and native HUD")
	await frames(20)
	bounds()
	await capture("overlay-clean")
	var camera_before: Vector2 = host._hud.get_camera().position
	var press := InputEventKey.new()
	press.keycode = KEY_RIGHT
	press.pressed = true
	Input.parse_input_event(press)
	await seconds(0.4)
	press = InputEventKey.new()
	press.keycode = KEY_RIGHT
	Input.parse_input_event(press)
	await frames()
	check(host._hud.get_camera().position.distance_to(camera_before) > 1, "native camera still moves in clean mode")
	await key(KEY_F2)
	check(not host._caster.overlays.panels.production.visible, "overlay hotkeys remain live in clean mode")
	check(host._world.get_state_hash() == before, "overlays, graphs and clean camera do not change simulation")
	for code in [KEY_F3, KEY_F4, KEY_F5]:
		await key(code)
	await capture("overlay-broadcast")
	await host._caster_seek(300)
	await finish_seek()
	check(host._caster.overlays.clean and not host._caster.overlays.enabled.production and host._caster.overlays.graph_selector.selected == 2, "rewind preserves clean and chosen overlays")
	check(host._caster.overlays.graph_history.has(305), "rewind preserves visited future history without showing future stats")
	check(host._caster.overlays.metrics.frame == 300, "displayed metrics come from rewound frame")
	await key(KEY_ESCAPE)
	check(host._quit_open and not host._caster.visible and host._hud.visible, "Escape restores HUD and clears caster overlays for the quit menu")
	await seconds(4.0)
	var quit_state: Dictionary = host._shell.quit_menu({"kind": "state"})
	check(host._shell.level_drawn(quit_state.level), "quit movie actually renders")
	await capture("overlay-quit-restored")
	await key(KEY_ESCAPE)
	check(not host._quit_open and host._world.is_paused(), "closing quit preserves the prior paused state")
	await host._caster_seek(host._world.replay_status().final_frame)
	await finish_seek()
	check(host._caster.overlays.labels.events.text.contains("Hero killed"), "ticker uses an actual recorded hero death")
	check(host._world.replay_status().mismatches == 0, "all seek CRCs match")
	await capture("overlay-death-ticker")
	print("CASTER OVERLAY UI RESULT failures=", failures)
	return failures
