## Headless test of lane SHELLFIX-1 (the owner's v0.4.9 reports: the BFME2 screens in the RotWK style, the settings of the online home, ACCEPT coming back
## late): the game with --shellfix1-check (scripts/shellfix1_check.gd) in each main menu style, then --auto --shellfix1-game in each (the quit menu's
## Options over a live game). With Node.js on the PATH the checks also sign in to a local test lobby (services/online/lobby, its own data folder and
## port; never a public lobby) and drive the online home's OPTIONS; without it that part prints a SKIP line.
##
##   godot --headless --path godot --script res://tests/shellfix1_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

var _failures := 0
var _lobby_pid := -1


func _initialize() -> void:
	var code: int = _run()
	if _lobby_pid > 0:
		OS.kill(_lobby_pid)
	print("SHELLFIX1 TEST %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## a local lobby (services/online/lobby/src/server.mjs) on a free port with a data folder of the test's own; "" without Node.js
func _start_lobby(dir: String) -> String:
	var out: Array = []
	if OS.execute("node", PackedStringArray(["--version"]), out, true) != 0:
		print("SKIP: the online home part (no Node.js on the PATH for the local test lobby)")
		return ""
	var port := 20000 + OS.get_process_id() % 20000
	var data := dir.path_join("lobby-data")
	DirAccess.make_dir_recursive_absolute(data)
	var key := "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
	for kv in [["LOBBY_BIND", "127.0.0.1:%d" % port], ["LOBBY_DATA", data], ["TICKET_KEYS", "1:" + key], ["SESSION_KEY", "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"],
			["RELAYS", "local=127.0.0.1:9"], ["MOTD", "SHELLFIX-1 test lobby"]]:
		OS.set_environment(kv[0], kv[1])
	var server := ProjectSettings.globalize_path("res://").path_join("../services/online/lobby/src/server.mjs").simplify_path()
	_lobby_pid = OS.create_process("node", PackedStringArray([server]))
	_check(_lobby_pid > 0, "the local test lobby started (%s, port %d)" % [server, port])
	OS.delay_msec(1500)
	return "http://127.0.0.1:%d" % port if _lobby_pid > 0 else ""


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game run (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	var dir := OS.get_user_data_dir().path_join("shellfix1-test-%d" % OS.get_process_id())
	DirAccess.make_dir_recursive_absolute(dir)
	var lobby := _start_lobby(dir)
	OS.set_environment("OPENBFME_LOBBY", lobby)
	var file := dir.path_join("shellfix1-options.ini")
	for style in ["rotwk", "bfme2"]:
		for args in [["--shellfix1-check"], ["--auto", "--advance=2", "--shellfix1-game"]]:
			var f := FileAccess.open(file, FileAccess.WRITE)
			f.store_string("OpenBFMEMainMenuStyle = %s\n" % style)
			f.close()
			_game_run(args + ["--options-file=" + file], "%s style" % style, not lobby.is_empty() and args[0] == "--shellfix1-check")
	DirAccess.remove_absolute(file)
	return 0 if _failures == 0 else 1


func _game_run(args: Array, what: String, online: bool) -> void:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	var passes := 0
	var online_passes := 0
	for line in text.split("\n"):
		if line.begins_with("SHELLFIX1 "):
			print("    ", line)
			if line.begins_with("SHELLFIX1 RESULT"):
				result = line
			elif line.begins_with("SHELLFIX1 PASS"):
				passes += 1
				if line.contains("online home after the switch"):
					online_passes += 1
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error") or line.begins_with("GAME FAILED"):
			print("    ", line)
			_failures += 1
	_check(code == 0 and result == "SHELLFIX1 RESULT: 0 failed" and passes > 0, "%s (%s): exit %d, %s, %d checks passed, %.0f s" % [what, " ".join(args), code,
		result if not result.is_empty() else "NO RESULT", passes, (Time.get_ticks_msec() - t0) / 1000.0])
	if online:
		_check(online_passes > 0, "%s: the online part ran to the online home after the style switch" % what)
