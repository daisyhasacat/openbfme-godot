## Installed BFME2 data: native notification movie, request delivery, dings and cleanup.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("NOTIFICATION TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	assert(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	game._world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	game._world.set_auto_advance(false)
	var shell: Node = game._shell
	assert(game._world.debug_script_action("DISPLAY_NOTIFICATION_BOX", [
		{"type":77,"string":"Instructional"}, {"type":25,"string":"SCRIPT:MBTutorial_MissionText03"}, {"type":0,"int":0}]).ok)
	game._world.advance(0.2)
	for i in 120:
		await create_timer(0.02).timeout
		var report: Dictionary = shell.get_notification_report()
		if report.get("opened", 0) == 1: break
	print("NOTIFICATION REPORT ", shell.get_notification_report())
	var report: Dictionary = shell.get_notification_report()
	assert(report.loaded and report.visible and not report.pending)
	assert(report.opened == 1)
	assert(report.sound == "Gui_DaveBox")
	assert(game._audio.is_valid_event(report.sound))
	assert(shell.script_notification("MissingNotificationType", "SCRIPT:MBTutorial_MissionText03", 0).error == "Unknown notification type: MissingNotificationType")
	assert(not game._campaign_flow._cine._note.visible)
	assert(shell.get_parent().layer == 6)
	for i in 60: await process_frame
	assert(shell.get_notification_report().opened == 1, "A persistent message must ding once")
	var native_text := false
	for row in shell.drawn_texts(false):
		if row.source == "callback" and not String(row.text).is_empty(): native_text = true
	assert(native_text, "The native notification text callback must draw the message")
	if OS.get_environment("NOTIFICATION_SHOT") != "":
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("NOTIFICATION_SHOT"))
	assert(shell.script_notification("ObjectiveCompleted", "SCRIPT:MBTutorial_MissionText03", 0.1).ok)
	for i in 120:
		await create_timer(0.02).timeout
		if shell.get_notification_report().get("opened", 0) == 2: break
	assert(shell.get_notification_report().opened == 2)
	await create_timer(1.5).timeout
	assert(not shell.get_notification_report().visible)
	game._tear_down_game("notification test")
	assert(shell.get_notification_report().loaded == false)
	game.free()
	print("NATIVE NOTIFICATION PASS")
	quit()
