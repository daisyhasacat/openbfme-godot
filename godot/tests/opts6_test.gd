## Headless test of lane OPTS-6 (menus that are exact, modal and usable with keyboard and controller): the game with --opts6-check (scripts/opts6_check.gd:
## the hit areas of every control of the settings, the bottom bar and the main menu just inside / outside their drawn shapes, the keyboard and the
## controller walks) on an Options.ini of the test's own at 1024x768, 1920x1080 and 2560x1440 (OPTS6_SIZES to choose), then --auto --opts6-game (the
## quit menu's Options over a live game: modal; AllHealthBars on, so the world has decorations to hide).
##
##   godot --headless --path godot --script res://tests/opts6_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("OPTS6 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game run (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	var dir := OS.get_user_data_dir().path_join("opts6-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var file := dir.path_join("opts6-check-options.ini")
	# every key RotWK's Save writes, in its own format and order: a real ACCEPT with no edit must leave this file's bytes alone
	# This fixture probes RotWK APT control names; local defaults may choose BFME2.
	var options := "OpenBFMEMainMenuStyle = rotwk\nAllHealthBars = yes\nAmbientVolume = 50.000000\nAudioLOD = Low\nBrightness = 50\nFirewallPortOverride = 0\nMovieVolume = 70.000000\n" \
		+ "MusicVolume = 70.000000\nSFXVolume = 70.000000\nScrollFactor = 50\nSendDelay = no\nStaticGameLOD = High\nUseEAX3 = no\nVoiceVolume = 70.000000\n"
	var sizes := OS.get_environment("OPTS6_SIZES").split(",", false) if not OS.get_environment("OPTS6_SIZES").is_empty() else PackedStringArray(["1024x768", "1920x1080", "2560x1440"])
	for size in sizes:
		var f := FileAccess.open(file, FileAccess.WRITE)
		f.store_string(options)
		f.close()
		_game_run(["--opts6-check", "--opts6-size=" + size, "--options-file=" + file], "the Options menu at " + size)
	# the checker fails closed: the inventory-only run passes as it is, and each sabotage of it fails BY NAME (a control dropped from the checker's own required
	# list, a control hidden from the page's side, a whole required button of the bottom bar, a main menu button), never silently
	_inventory_run("", "")
	_inventory_run("inventory:Retail.Brightness", "Retail.Brightness")
	_inventory_run("page:Retail.Brightness", "Retail.Brightness")
	_inventory_run("page:Retail.MusicVolume", "Retail.MusicVolume")
	_inventory_run("inventory:OpenBFMEWidescreen", "OpenBFMEWidescreen")
	_inventory_run("page:accept", "accept")
	_inventory_run("inventory:QuitMainMenu", "QuitMainMenu")
	_inventory_run("page:SoloPlayNav", "SoloPlayNav")
	var f2 := FileAccess.open(file, FileAccess.WRITE)
	f2.store_string(options)
	f2.close()
	_game_run(["--auto", "--advance=2", "--opts6-game", "--options-file=" + file], "the Options menu over a live game")
	_check(FileAccess.get_file_as_string(file) == options, "the in-game visit wrote nothing (Esc is CANCEL)")
	DirAccess.remove_absolute(file)
	return 0 if _failures == 0 else 1


func _game_run(args: Array, what: String) -> void:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	for line in text.split("\n"):
		if line.begins_with("OPTS6 "):
			print("    ", line)
			if line.begins_with("OPTS6 RESULT"):
				result = line
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line)
			_failures += 1
	_check(code == 0 and result.ends_with(" 0 failed") and not result.begins_with("OPTS6 RESULT: 0 ok"),
		"%s (%s): exit %d, %s, %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", (Time.get_ticks_msec() - t0) / 1000.0])


## the inventory-only run of the checker (OPTS6_ONLY=inventory: the main menu's, every tab's and the bottom bar's controls against the required lists, no probing)
## with `sabotage` (OPTS6_SABOTAGE): "" must pass; a sabotage must fail with the sabotaged name in a FAIL line of an inventory check, a non-zero exit and
## no crash (an empty output or a missing RESULT line fails the case whichever way it was meant to go)
func _inventory_run(sabotage: String, name: String) -> void:
	var dir := OS.get_user_data_dir().path_join("opts6-test")
	var file := dir.path_join("opts6-inventory-options.ini")
	var f := FileAccess.open(file, FileAccess.WRITE)
	f.store_string("OpenBFMEMainMenuStyle = rotwk\nBrightness = 50\nScrollFactor = 50\nStaticGameLOD = High\n")
	f.close()
	OS.set_environment("OPTS6_ONLY", "inventory")
	if sabotage.is_empty():
		OS.unset_environment("OPTS6_SABOTAGE")
	else:
		OS.set_environment("OPTS6_SABOTAGE", sabotage)
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "--", "--opts6-check",
		"--opts6-size=1024x768", "--options-file=" + file]), output, true)
	OS.unset_environment("OPTS6_ONLY")
	OS.unset_environment("OPTS6_SABOTAGE")
	DirAccess.remove_absolute(file)
	var text: String = "".join(output)
	var result := ""
	var fails := PackedStringArray()
	var oks := 0
	for line in text.split("\n"):
		if line.begins_with("OPTS6 RESULT"):
			result = line
		elif line.begins_with("OPTS6 ") and line.contains(": FAIL "):
			fails.append(line)
		elif line.begins_with("OPTS6 ") and line.ends_with(": ok"):
			oks += 1
	var what := "the checker's inventory%s" % ("" if sabotage.is_empty() else " sabotaged (%s)" % sabotage)
	if sabotage.is_empty():
		_check(code == 0 and result.ends_with(" 0 failed") and oks >= 10, "%s passes: exit %d, %s, %d ok" % [what, code, result if not result.is_empty() else "NO RESULT", oks]) # tolerance: the inventory checks of the main menu, 8 tabs, the bar
	else:
		var named := false
		for l in fails:
			named = named or (l.contains("controls are exactly the required inventory") and l.contains(name))
		_check(code != 0 and not result.is_empty() and not result.ends_with(" 0 failed") and named,
			"%s fails by name: exit %d, %s, %d FAIL lines%s" % [what, code, result if not result.is_empty() else "NO RESULT", fails.size(), "" if named else " (none names " + name + ")"])
