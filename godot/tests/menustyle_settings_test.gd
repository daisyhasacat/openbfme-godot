## MENUSTYLE-1 (lane DEVPORT-8): the main menu style is an Options.ini entry like the other OpenBFME options (OpenBFMEMainMenuStyle = rotwk / bfme2),
## written through the shell's options and read back before the shell boots by AptMenuPlayer.read_options_file (the shell's own reader).
## godot --headless --path godot --script res://tests/menustyle_settings_test.gd
extends SceneTree

const Options := preload("res://scripts/game_options.gd")

class ShellSpy extends Node:
	var opts := {}
	var writes := []
	var saves := 0
	func get_options() -> Dictionary:
		return opts.duplicate()
	func set_option(key: String, value: String) -> bool:
		writes.append([key, value])
		opts[key] = value
		return true
	func save_options() -> Dictionary:
		saves += 1
		return {"ok": true}

class GameSpy extends Node:
	var requested := []
	func request_menu_style(style: String) -> void:
		requested.append(style)

var failures := 0


func check(ok: bool, message: String) -> void:
	print("MENUSTYLE SETTINGS ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1


func write(path: String, text: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()


func _initialize() -> void:
	if not ClassDB.class_exists("AptMenuPlayer"):
		printerr("MENUSTYLE SETTINGS FAIL: the openbfme extension is not loaded")
		quit(1)
		return
	var path := OS.get_user_data_dir().path_join("menustyle-settings-%d.ini" % OS.get_process_id())
	DirAccess.remove_absolute(path)
	check(Options.MENU_STYLES == [["rotwk", "Rise of the Witch-king"], ["bfme2", "Battle for Middle-earth II"]], "exact dropdown choices")
	check(Options.EXTRAS[Options.MENU_STYLE_KEY] == {"default": "bfme2", "choices": ["rotwk", "bfme2"]}, "an OpenBFME extra of Options.ini, default bfme2")
	# before the shell boots: the file as the shell reads it
	check(Options.menu_style_from_file(path) == {"ok": true, "style": "bfme2", "problem": ""}, "no Options.ini (the first run): BFME2")
	write(path, "Brightness = 50\r\nOpenBFMEMainMenuStyle = bfme2\r\nScrollFactor = 40\r\n")
	check(Options.menu_style_from_file(path) == {"ok": true, "style": "bfme2", "problem": ""}, "Options.ini's bfme2 is read before the shell")
	write(path, "Brightness = 50\r\n")
	check(Options.menu_style_from_file(path).style == "bfme2", "a file without the key: BFME2")
	write(path, "OpenBFMEBFME2UI = no\r\n")
	check(Options.menu_style_from_file(path).style == "rotwk", "legacy no migrates to RotWK")
	write(path, "OpenBFMEBFME2UI = yes\r\n")
	check(Options.menu_style_from_file(path).style == "bfme2", "legacy yes migrates to BFME2")
	write(path, "OpenBFMEBFME2UI = yes\r\nOpenBFMEMainMenuStyle = rotwk\r\n")
	check(Options.menu_style_from_file(path).style == "rotwk", "explicit new style overrides legacy toggle")
	write(path, "OpenBFMEMainMenuStyle = bfme3\r\n")
	var bad := Options.menu_style_from_file(path)
	check(bad.ok and bad.style == "bfme2" and str(bad.problem).contains("bfme3"), "a value the key does not take: the default, reported by name: " + JSON.stringify(bad))
	DirAccess.remove_absolute(path)
	DirAccess.make_dir_recursive_absolute(path) # a folder where the file should be: not readable as a file
	var unreadable := Options.menu_style_from_file(path)
	check(not unreadable.ok and str(unreadable.get("error", "")).begins_with("Options.ini: "), "an unreadable Options.ini is an error: " + JSON.stringify(unreadable))
	DirAccess.remove_absolute(path)
	# the Options screen's commit: through the shell's options (Options.ini), like every other option, then the style switch is requested
	var shell := ShellSpy.new()
	var game := GameSpy.new()
	var options := Options.new()
	options.shell = shell
	options.game = game
	check(options.current()[Options.MENU_STYLE_KEY] == "bfme2", "the default in current()")
	check(options.commit({Options.MENU_STYLE_KEY: "bfme2"}).ok, "accept BFME2")
	check(shell.writes == [[Options.MENU_STYLE_KEY, "bfme2"]] and shell.saves == 1, "written to Options.ini through the shell: %s, %d saves" % [shell.writes, shell.saves])
	check(game.requested == ["bfme2"], "the switch is requested: %s" % [game.requested])
	check(options.current()[Options.MENU_STYLE_KEY] == "bfme2", "current() reads it back from the shell's options")
	check(not options.commit({Options.MENU_STYLE_KEY: "unknown"}).ok and shell.writes.size() == 1 and game.requested.size() == 1, "an unknown style is refused before anything is written")
	shell.opts[Options.MENU_STYLE_KEY] = "unknown"
	var found := options.current()
	check(found[Options.MENU_STYLE_KEY] == "bfme2" and "\n".join(options.problems).contains("unknown"), "an unknown value: the default, reported")
	check(not ResourceLoader.exists("res://scripts/menu_campaigns.gd"), "no OpenBFME campaign panel script")
	options.free()
	shell.free()
	game.free()
	quit(0 if failures == 0 else 1)
