## OpenBFME. GPL-3.0. Regression for alpha.5's fatal prepare_replay refusal.
## Part 1 calls the real game controller with a rejected preparation; part 2 (lane DEVPORT-7) runs the real game: with Uber performance on,
## the command-line replay of a Faithful recording (the NETLAG-1 fixture) is refused on the replay list, by name, and the game stays up.
## The retail modal's dismissal is covered by test_mp2_replay_menu.cpp. Needs ROTWK_INSTALL / BFME2_INSTALL for part 2 (else SKIP, exit 77).
## godot --headless --path godot --script res://tests/replay_refusal_test.gd
extends SceneTree

class RejectedWorld:
	extends Node3D
	var reason := "Replay profile: Uber. Current profile: Faithful."
	var prepared := ""
	func prepare_replay(path: String) -> Dictionary:
		prepared = path
		return {"ok": false, "errors": [reason]}

class ReplayShell:
	extends Node2D
	var refused_name := ""
	var refused_reason := ""
	func save_load_refused(name: String, reason: String) -> bool:
		refused_name = name
		refused_reason = reason
		return true

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game_script := GDScript.new()
	game_script.source_code = """
extends "res://scripts/game.gd"
func _fail(message: String) -> void:
	_failed = message
	_state = State.FAILED
"""
	if game_script.reload() != OK:
		print("REPLAY REFUSAL FAIL: could not compile the replay controller")
		quit(1)
		return
	var game = game_script.new()
	var world := RejectedWorld.new()
	var shell := ReplayShell.new()
	game._world = world
	game._shell = shell
	game._state = game.State.MENU
	await game._begin_replay("/replays/owner.replay")
	var ok: bool = game._failed.is_empty() and game._state == game.State.MENU and not game._replay_active
	ok = ok and world.prepared == "/replays/owner.replay"
	ok = ok and shell.refused_name == "owner" and shell.refused_reason == world.reason
	print("  %s controller: menu retained, named-profile error sent to the replay list, no fatal error" % ["ok  " if ok else "FAIL"])
	game.free()
	world.free()
	shell.free()
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("REPLAY REFUSAL SKIP: ROTWK_INSTALL / BFME2_INSTALL unset (the real game flow needs them)")
		quit(77)
		return
	var why: String = await _real_flow()
	print("  %s real game: %s" % ["ok  " if why.is_empty() else "FAIL", "refused on the replay list, the game stays up" if why.is_empty() else why])
	ok = ok and why.is_empty()
	print("REPLAY REFUSAL ", "PASS" if ok else "FAIL", ": the controller and the real game keep the menu and name both profiles")
	quit(0 if ok else 1)


## "" when the real game refuses the replay on its replay list and keeps running, else what went wrong
func _real_flow() -> String:
	var project := ProjectSettings.globalize_path("res://")
	var fixture := project.path_join("../engine/tests/data/netlag1/prelane_runahead2.replay").simplify_path()
	if not FileAccess.file_exists(fixture):
		return "the Faithful replay fixture is missing: " + fixture
	var dir := OS.get_user_data_dir().path_join("replay-refusal")
	DirAccess.make_dir_recursive_absolute(dir)
	var ini := dir.path_join("uber.ini")
	FileAccess.open(ini, FileAccess.WRITE).store_string("OpenBFMEUberPerformance = yes\n")
	var log_path := dir.path_join("game.log")
	DirAccess.remove_absolute(log_path)
	var pid := OS.create_process(OS.get_executable_path(), ["--headless", "--path", project, "--log-file", log_path, "--",
		"--options-file=" + ini, "--replay=" + fixture])
	if pid <= 0:
		return "the game did not start"
	var text := ""
	var refused_at := -1
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 300000:
		await create_timer(0.5).timeout
		text = FileAccess.get_file_as_string(log_path)
		if text.contains("GAME FAIL") or not OS.is_process_running(pid):
			break
		if refused_at < 0 and text.contains("GAME replay refused: "):
			refused_at = Time.get_ticks_msec()
		if refused_at >= 0 and Time.get_ticks_msec() - refused_at > 5000:
			break
	var alive := OS.is_process_running(pid)
	if alive:
		OS.kill(pid)
	if refused_at < 0:
		return "no refusal within 300 s (alive %s); log tail: %s" % [alive, text.right(1500)]
	if text.contains("GAME FAIL") or not alive:
		return "the game failed or quit after the refusal; log tail: " + text.right(1500)
	if not text.contains("Replay profile: Faithful. Current profile: Uber."):
		return "the refusal does not name both profiles; log tail: " + text.right(1500)
	if not text.contains("GAME replay menu: "):
		return "the replay list was not opened for the refusal; log tail: " + text.right(1500)
	return ""
