## Real online spectator proof: the lobby signs 120s delay, two peers feed the relay.
extends "res://tests/caster_overlay_walk.gd"


func transport() -> Dictionary:
	return host._world.caster_transport_status()


func run(game: Node, output: String) -> int:
	host = game
	directory = output
	var receipts: Array[Dictionary] = []
	host.caster_seek_finished.connect(func(receipt): receipts.append(receipt))
	check(DirAccess.make_dir_recursive_absolute(output) == OK, "live capture directory")
	var deadline := Time.get_ticks_msec() + 150000
	while transport().get("frame", 1) < 150 and Time.get_ticks_msec() < deadline:
		await frames(2)
	var initial := transport()
	check(initial.get("live", false) and initial.get("frame", 1) >= 150, "real relay spectator caught up beyond frame 150")
	if not initial.get("live", false) or initial.get("frame", 1) < 150:
		return failures
	check(initial.delay == 120, "signed default public delay is two minutes")
	await key(KEY_SPACE)
	check(host._world.is_paused(), "Space pauses live simulation")
	var reference := transport()
	var reference_hash: int = host._world.get_state_hash()
	var reference_frame: int = reference.frame
	var source: Dictionary = host._world.net_status().spectate
	check(reference.frame <= reference.final_frame, "playhead is within received edge")
	check(host._caster.timeline.max_value == reference.final_frame, "timeline is bounded by received data")
	for id in host._world.get_object_ids():
		var object: Dictionary = host._world.get_object(id)
		if str(object.get("template", "")).ends_with("FortressCitadel"):
			host._hud.camera_look_at(Vector2(object.x, object.y))
			break
	await frames(10)
	bounds()
	var controls: Rect2 = host._caster.transport.get_global_rect()
	check(host.get_viewport().get_visible_rect().encloses(controls), "live transport fits viewport")
	check(host._caster.go_live_button.size.x >= 100, "Go live label has visible button width")
	for overlay in host._caster.overlays.panels.values():
		if overlay.visible:
			check(not controls.intersects(overlay.get_global_rect()), "live controls do not overlap broadcast overlays")
	await capture("live-paused")
	var outside: Dictionary = host._world.caster_seek_step(int(transport().final_frame) + 1, 20)
	check(not outside.ok and transport().frame == reference_frame, "native seek refuses beyond received edge without advancing")
	await key(KEY_1)
	await key(KEY_F7)
	var pin: Vector3 = host._caster.pip.target
	host._caster.seek_requested.emit(reference_frame - 100)
	await capture("live-rewind-progress")
	await finish_seek()
	check(transport().frame == reference_frame - 100 and host._world.is_paused(), "backward seek rebuilds to exact frame while paused")
	check(host._caster.pip.enabled and host._caster.pip.target == pin and host._caster.observed >= 0, "live rewind restores PiP and POV")
	var rewound: Dictionary = host._world.net_status().spectate
	check(rewound.index == source.index and rewound.delay == source.delay and rewound.served >= source.served, "rewind retains spectator identity, delay and received frontier")
	await frames(8)
	await capture("live-behind")
	await key(KEY_PERIOD)
	check(transport().frame == reference_frame - 99 and host._world.is_paused(), "live step advances exactly one frame")
	await host._caster_seek(reference_frame)
	await finish_seek()
	check(transport().frame == reference_frame and host._world.get_state_hash() == reference_hash, "rewind then fast-forward exactly matches uninterrupted state")
	check(transport().hashes_compared > 0 and transport().mismatches == 0, "received relay CRCs match after reconstruction")
	await key(KEY_G)
	await finish_seek()
	var first_edge: Dictionary = receipts.back().status
	check(not host._world.is_paused() and first_edge.frame == first_edge.final_frame, "Go live reaches the received edge at completion")
	await frames(8)
	await capture("live-edge")
	host._world.caster_playback(true, 1.0)
	await host._caster_seek(reference_frame)
	await finish_seek()
	var edge_before: int = transport().final_frame
	deadline = Time.get_ticks_msec() + 180000
	while transport().final_frame <= edge_before + 25 and Time.get_ticks_msec() < deadline:
		await frames(3)
	check(transport().final_frame > edge_before + 25, "received log continues to grow while spectator remains paused")
	check(transport().frame == reference_frame and host._world.get_state_hash() == reference_hash, "receiving data does not advance or mutate paused simulation")
	await capture("live-paused-receiving")
	await key(KEY_G)
	await finish_seek()
	check(not host._world.is_paused(), "Go live resumes playback")
	var at_edge: Dictionary = receipts.back().status
	check(at_edge.frame == at_edge.final_frame, "Go live completes at the current received edge while source advances")
	check(at_edge.delay == 120 and at_edge.mismatches == 0, "Go live preserves delay and CRC agreement")
	host._world.caster_playback(true, 1.0)
	await frames(8)
	await capture("live-following")
	await key(KEY_F9)
	check(host._caster.overlays.clean and not host._caster.transport.visible and host._caster.pip.visible, "live clean mode retains chosen overlay and PiP")
	await capture("live-clean")
	await key(KEY_G)
	await finish_seek()
	check(not host._world.is_paused() and transport().delay == 120, "Go live hotkey also works in clean mode")
	host._world.caster_playback(true, 1.0)
	await host._caster_seek(reference_frame)
	await finish_seek()
	check(host._world.get_state_hash() == reference_hash and transport().mismatches == 0, "old received frames remain reproducible after following new data")
	print("CASTER LIVE UI RESULT failures=", failures, " transport=", JSON.stringify(transport()))
	return failures
