## For now Lua stays out of network games (docs/BFME_LUA.md, Limits): a host and a joiner play 40 logic frames of a LAN game, each with an entry
## script on its Lua path that registers an engine handler and a fast update callback. The scripts load in the menus (they print there), but in
## the network game no handler or callback runs, and the game ends without a desync. A local skirmish with the same script is the control: there
## its callback and handler do run.
##
##   godot --headless --path godot --script res://tests/lua_network_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("LUA NETWORK %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("LUA NETWORK SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var dir := OS.get_user_data_dir().path_join("lua-network-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var script := FileAccess.open(dir.path_join("mp_probe.lua"), FileAccess.WRITE)
	script.store_string("""print('LUA-MP loaded')
bfme.events.on('matchStarted', 'mp', function() print('LUA-MP matchStarted') end)
bfme.events.on('constructionComplete', 'mp', function() print('LUA-MP event') end)
bfme.events.onUpdate('mp', function() print('LUA-MP update ' .. tostring(bfme.match.getStatus().ready)) end, 50)
""")
	script.close()
	OS.set_environment("OPENBFME_LUA_PATH", dir) # the two game processes inherit it
	var godot_path := OS.get_executable_path()
	var project := ProjectSettings.globalize_path("res://")
	var port := 33000 + (Time.get_ticks_msec() % 2000)
	var common := ["--headless", "--path", project]
	var options := dir.path_join("Options.ini") # an empty options file of its own: the player's options (Uber mode ...) stay out of the test
	var empty := FileAccess.open(options, FileAccess.WRITE)
	empty.close()
	var host_args: Array = common + ["--log-file", "%s/host.log" % dir, "--", "--options-file=" + options, "--net-host=%d" % port, "--seed=9",
		"--net-slots=human:FactionMen:0:0;human:FactionMordor:1:1", "--net-script", "--net-frames=40"]
	var join_args: Array = common + ["--log-file", "%s/join.log" % dir, "--", "--options-file=" + options, "--net-join=127.0.0.1:%d" % port, "--net-name=Joiner", "--net-script",
		"--net-frames=40"]
	for who in ["host", "join"]:
		DirAccess.remove_absolute("%s/%s.log" % [dir, who])
	var pids := { "host": OS.create_process(godot_path, PackedStringArray(host_args)) }
	OS.delay_msec(3000)
	pids["join"] = OS.create_process(godot_path, PackedStringArray(join_args))
	var codes := {}
	var deadline := Time.get_ticks_msec() + 600000
	for who in ["host", "join"]:
		var pid: int = pids[who]
		while pid > 0 and OS.is_process_running(pid) and Time.get_ticks_msec() < deadline:
			OS.delay_msec(200)
		codes[who] = str(OS.get_process_exit_code(pid)) if pid > 0 and not OS.is_process_running(pid) else ""
		if pid > 0 and OS.is_process_running(pid):
			OS.kill(pid)
	for who in ["host", "join"]:
		var log := FileAccess.get_file_as_string("%s/%s.log" % [dir, who])
		_check(codes[who] == "0", "%s: the LAN game ran 40 frames without a desync (exit '%s')" % [who, codes[who]])
		_check(log.contains("LUA-MP loaded"), "%s: the entry script loaded in the menus" % who)
		_check(log.contains("GAME NET status"), "%s: the network game played" % who)
		_check(not log.contains("LUA-MP update true"), "%s: no update callback ran during the network game" % who)
		_check(not log.contains("LUA-MP matchStarted") and not log.contains("LUA-MP event"), "%s: no engine handler ran in the network game" % who)
	# the control: the same script in a local skirmish runs its update callback and its handlers
	var local_args: Array = common + ["--log-file", "%s/local.log" % dir, "--", "--options-file=" + options, "--auto", "--advance=6", "--no-intro",
		"--map=maps/map mp fall back 4p/map mp fall back 4p.map", "--faction=FactionMen"]
	DirAccess.remove_absolute("%s/local.log" % dir)
	var local_pid := OS.create_process(godot_path, PackedStringArray(local_args))
	var local_deadline := Time.get_ticks_msec() + 300000
	while OS.is_process_running(local_pid) and Time.get_ticks_msec() < local_deadline:
		OS.delay_msec(200)
	if OS.is_process_running(local_pid):
		OS.kill(local_pid)
	var local_log := FileAccess.get_file_as_string("%s/local.log" % dir)
	_check(local_log.contains("LUA-MP update true") and local_log.contains("LUA-MP matchStarted"), "local: the same script's update callback and handler run in a local game")
	DirAccess.remove_absolute(dir.path_join("mp_probe.lua"))
	return 0 if _failures == 0 else 1
