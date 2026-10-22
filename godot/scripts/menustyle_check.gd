## MENUSTYLE-1 integration check: Options accept/cancel, deferred switching, data isolation.
extends RefCounted

var game: Node
var failures := 0
const Options := preload("res://scripts/game_options.gd")


var _switches := 0


## lane DEVPORT-8: with --screens=DIR, the clean main menu (after its reveal) and its Solo Play list (each game's campaign buttons visible there), named
## menustyle-<WxH>-<tag>.png
func _shots(tag: String) -> void:
	if game._screens.is_empty():
		return
	var size: Vector2i = game.get_tree().root.size
	var res := "%dx%d" % [size.x, size.y]
	await game._wait_seconds(2.0)
	game._save_named("menustyle-%s-%s" % [res, tag])
	var top: int = game._shell.shell_top_level()
	var nav: Dictionary = game._shell.find_button(top, "SoloPlayNav")
	if nav.get("found", false):
		await _click(Vector2(nav.x, nav.y))
		await game._wait_seconds(1.5)
		game._save_named("menustyle-%s-%s-solo" % [res, tag])
		print("MENUSTYLE campaign buttons in the Solo Play list: ", JSON.stringify(game.bfme2_campaign_buttons()))
		await _close_list(nav)


## the player closes the list with a click on the nav's own button (once the list is open, its button is the nav's direct child that takes the pointer:
## the list's entries are named clips); the menu is clean again (the style switch waits for that)
## (BFME2's nav: the first button under SoloPlayNav once the list is open; RotWK's: its button that takes the pointer)
func _close_list(_nav: Dictionary) -> void:
	var top: int = game._shell.shell_top_level()
	var first: Dictionary = game._shell.find_button(top, "SoloPlayNav")
	if first.get("found", false):
		await _click(Vector2(first.x, first.y))
		await game._wait_seconds(1.5)
	if not game._clean_main_menu():
		var prefix := "_level%d.SoloPlayNav." % top
		for b in game._shell.list_buttons(top):
			var rest := str(b.path).trim_prefix(prefix)
			if str(b.path).begins_with(prefix) and bool(b.hittable) and rest.begins_with("instance") and not rest.contains("."):
				await _click(Vector2(b.x, b.y))
				break
	var deadline := Time.get_ticks_msec() + 10000
	while Time.get_ticks_msec() < deadline and not game._clean_main_menu():
		await game.get_tree().process_frame
	check(game._clean_main_menu(), "the Solo Play list closed by a click on its nav button")


func check(ok: bool, message: String) -> void:
	print("MENUSTYLE ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1


func run(owner: Node) -> void:
	game = owner
	if game._options_file.is_empty():
		check(false, "requires an isolated --options-file")
		game.get_tree().quit(1)
		return
	if not game._menustyle_expect.is_empty():
		# lane DEVPORT-8: the run after a restart: the style the last run chose in Options.ini is the one the shell boots with
		await _settled(game._menustyle_expect)
		if not game._menustyle_size.is_empty():
			await _resize()
		await _shots("restart-" + game._menustyle_expect)
		check(game._menu_style == game._menustyle_expect, "after a restart the menu is " + game._menustyle_expect)
		check(str(game._shell.get_shell_report().get("presentation_style", "")) == ("bfme2-106" if game._menustyle_expect == "bfme2" else "rotwk-201"),
			"after a restart the shell's presentation source is " + str(game._shell.get_shell_report().get("presentation_style", "")))
		await _check_campaign_buttons(game._menustyle_expect)
		print("MENUSTYLE RESULT: %d failed" % failures)
		game.get_tree().quit(0 if failures == 0 else 1)
		return
	await _settled("rotwk")
	if not game._menustyle_size.is_empty():
		await _resize()
	await _shots("start-rotwk")
	if game._screens.is_empty():
		game.get_tree().root.size = Vector2i(1600, 1200)
	await game._step(6)
	var original_hash: int = game._world.get_state_hash()
	var original_data: String = game._fs.get_archive_for_file("data/ini/gamedata.ini")
	var original_shell: String = game._fs.get_archive_for_file("mainmenu.apt")
	check(game._menu_style == "rotwk", "untouched option is RotWK")
	await _open_options()
	await _choose_style("bfme2")
	await game._step(5)
	check(game._menu_style == "rotwk", "selection does not interrupt Options")
	game._display_screen.close(false)
	await game._wait_main_ready()
	check(game._pending_menu_style == "rotwk", "cancel leaves style unchanged")
	check(not AptMenuPlayer.read_options_file(game._options_path()).get("values", {}).has(Options.MENU_STYLE_KEY), "cancel writes no menu style")
	await _check_campaign_buttons("rotwk")
	for style in ["bfme2", "rotwk", "bfme2", "rotwk", "bfme2"]:
		await _open_options()
		await _choose_style(style)
		game._display_screen.close(true)
		await _settled(style)
		_switches += 1
		await _shots("runtime%d-%s" % [_switches, style])
		check(game._menu_style == style, "Options acceptance applied " + style)
		# lane DEVPORT-8: the choice is an Options.ini entry like every other option (the file on disk and the shell's entries)
		check(AptMenuPlayer.read_options_file(game._options_path()).get("values", {}).get(Options.MENU_STYLE_KEY, "") == style, "Options.ini holds " + style)
		check(str(game._shell.get_options().get(Options.MENU_STYLE_KEY, "")) == style, "the shell's options hold " + style)
		check(str(game._shell.get_shell_report().get("presentation_style", "")) == ("bfme2-106" if style == "bfme2" else "rotwk-201"), "presentation source of " + style)
		check(game._world.get_state_hash() == original_hash, "switch did not mutate world")
		check(game._fs.get_archive_for_file("data/ini/gamedata.ini") == original_data, "gameplay source unchanged")
		check(game._fs.get_archive_for_file("mainmenu.apt") == original_shell, "original mount not reordered")
		check(game._audio.engine_api_installed(), "gameplay AudioApi still installed")
		await _check_campaign_buttons(style)
	print("MENUSTYLE RESULT: %d failed" % failures)
	game.get_tree().quit(0 if failures == 0 else 1)


## Each presentation movie retains its authored campaigns and dropdown layout.
const ROTWK_SOLO_ENTRIES := ["BonusCampaign", "Expansion1Campaign", "LoadGame", "Skirmish", "WarOfTheRing"]
func _check_campaign_buttons(style: String) -> void:
	var overlay := _visible_godot_buttons(game)
	check(overlay.is_empty(), "%s: no Godot button over the main menu (every button is the movie's): %s" % [style, overlay])
	var top: int = game._shell.shell_top_level()
	var nav: Dictionary = game._shell.find_button(top, "SoloPlayNav")
	var opened := false
	if nav.get("found", false):
		# the Solo Play list (BFME2: Skirmish, War of the Ring, Evil / Good campaign, Load Game) is opened with a click, as the player would
		await _click(Vector2(nav.x, nav.y))
		await game._wait_seconds(1.5)
		opened = not game._clean_main_menu()
	var native: Dictionary = game.bfme2_campaign_buttons()
	if style == "rotwk":
		check(opened, "rotwk: the Solo Play list opened")
		var names := []
		for entry in game.solo_play_entries():
			var name := String(entry).get_slice(".", 0)
			if not names.has(name):
				names.append(name)
		names.sort()
		check(names == ROTWK_SOLO_ENTRIES, "rotwk: retail's Solo Play entries take the pointer: %s" % [game.solo_play_entries()])
		check(not game.campaign_entry("Expansion1Campaign").is_empty(), "rotwk: SoloPlayNav.Expansion1Campaign can be clicked")
	if style == "bfme2":
		check(opened, "bfme2: the Solo Play list opened")
		check(native.get("found", []) == ["SoloPlayNav.GoodCampaign", "SoloPlayNav.EvilCampaign"], "bfme2: BFME2's MainMenu.apt GoodCampaign / EvilCampaign found: %s" % JSON.stringify(native))
		var shown := []
		for b in game._shell.list_buttons(top):
			if bool(b.hittable) and str(b.path).begins_with("_level%d.SoloPlayNav." % top):
				shown.append(str(b.path).split(".")[2])
		check(shown.has("Skirmish") and shown.has("WarOfTheRing") and shown.has("LoadGame"), "bfme2: the list's other entries can be clicked: %s" % [shown])
		check(native.visible == native.found and not native.hittable.is_empty(), "bfme2: Good/Evil campaign entries remain visible and clickable: %s" % [native])
		check(shown.has("GoodCampaign") and shown.has("EvilCampaign"), "bfme2: both campaigns take pointer input")
	else:
		check(native.get("visible", []).is_empty() and native.get("hittable", []).is_empty(), "%s: no BFME2 campaigns mixed into this menu" % style)
	if opened:
		await _close_list(nav)
	for alignment in ["good", "evil"]:
		var line: String = game.start_bfme2_campaign(alignment)
		check(line.begins_with("[S-1914] The BFME2 %s campaign " % alignment.capitalize()), style + ": a " + alignment + " request is refused by name: " + line)
		# the main menu's "not available" box (GuiFX.apt's MessageBox): its Ok gives the menu back
		await game._step(30)
		var ok: Dictionary = game._walk_find_box_ok()
		check(not ok.is_empty(), style + ": the " + alignment + " refusal shows the not-available box")
		if not ok.is_empty():
			await _click(Vector2(ok.x, ok.y))
		await game._wait_main_ready()
		check(game._clean_main_menu(), style + ": the menu is usable again after the " + alignment + " box")


## lane CAMPMENU-1: the Godot buttons drawn over the clean main menu (an OpenBFME panel such as MENUSTYLE-1's campaign panel), by their node paths
func _visible_godot_buttons(node: Node) -> Array:
	var out := []
	for child in node.get_children():
		if child is BaseButton and (child as BaseButton).is_visible_in_tree():
			out.append(str(child.get_path()))
		out.append_array(_visible_godot_buttons(child))
	return out


## --menustyle-size=WxH: a windowed run's window size for the captures
func _resize() -> void:
	var parts: PackedStringArray = game._menustyle_size.split("x")
	var root: Window = game.get_tree().root
	root.mode = Window.MODE_WINDOWED
	root.size = Vector2i(int(parts[0]), int(parts[1]))
	await game._wait_seconds(2.0)
	check(game.get_tree().root.size == Vector2i(int(parts[0]), int(parts[1])), "the window is %s: %s" % [game._menustyle_size, game.get_tree().root.size])


func _settled(style: String) -> void:
	var deadline := Time.get_ticks_msec() + 45000
	while Time.get_ticks_msec() < deadline:
		if game._state == game.State.FAILED:
			check(false, game._failed)
			return
		if game._menu_style == style and not game._switching_menu_style and game._clean_main_menu():
			return
		await game.get_tree().process_frame
	check(false, "timeout waiting for clean " + style + " main menu")


func _open_options() -> void:
	await game._wait_main_ready()
	game._shell.shell_fscommand("AptMainMenu::Options", "false")
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline and not game._display_screen.is_open():
		await game.get_tree().process_frame
	check(game._display_screen.is_open(), "OpenBFME Options opened")
	await game._wait_seconds(0.5) # lane DEVPORT-8: its page is laid out before the first click (windowed 1920x1080 runs missed the first dropdown)


func _click(stage_position: Vector2) -> void:
	game._mouse_event(stage_position, false, false)
	await game._step(2)
	game._mouse_event(stage_position, true, true)
	await game._step(2)
	game._mouse_event(stage_position, false, true)
	await game._step(6)


func _choose_style(style: String) -> void:
	var shell: Node = game._shell
	var state: Dictionary = shell.openbfme_page_state()
	var tab: int = game._display_screen.TABS.find("Interface")
	await _click((state.tabs[tab] as Rect2).get_center())
	await game._wait_seconds(0.3)
	state = shell.openbfme_page_state()
	var row: Rect2 = state.rows[Options.MENU_STYLE_KEY].rect
	var health_bars: int = state.rows["Retail.HealthBars"].value
	await _click(Vector2(row.end.x - 12, row.get_center().y))
	await game._wait_seconds(0.5) # lane DEVPORT-8: the opened list is drawn before its entry is looked up (a 1024x768 run missed it once)
	var label: String = Options.MENU_STYLES[0 if style == "rotwk" else 1][1]
	var target := Rect2()
	for command in shell.gadget_draw_list():
		if command.lone and command.kind == "text" and command.text == label:
			var rect: Rect2 = command.rect
			if shell.window_to_stage(rect.position).y >= row.end.y:
				target = rect
	check(target != Rect2(), "dropdown renders " + label)
	if target == Rect2():
		return
	await _click(Vector2(row.get_center().x, shell.window_to_stage(target.position).y + 6))
	check(game._display_screen.extra_value(Options.MENU_STYLE_KEY) == style, "dropdown click chooses " + style)
	check(shell.openbfme_page_state().rows["Retail.HealthBars"].value == health_bars, "dropdown does not change the covered row")
