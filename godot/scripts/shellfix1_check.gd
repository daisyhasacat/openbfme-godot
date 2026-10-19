## Lane SHELLFIX-1 check (game.gd --shellfix1-check; with --auto --shellfix1-game over a live game): the owner's v0.4.9 reports, driven with the
## player's clicks (the pointer through the viewport) in the style Options.ini starts with (OpenBFMEMainMenuStyle):
##   * every screen shows the shell assets of that style only: each movie on the shell stack and each picture of the frame drawn last comes from that
##     style's archives (AptMenuPlayer.shell_drawn_sources; RotWK: the game's own mount, its movies from the RotWK install; BFME2: the BFME2 1.06 install),
##     never an image of the other game (original_images);
##   * Options' ACCEPT is back on the screen it came from on the next shell update (2 frames: the release's and the pop's), the main menu's, the online
##     home's and the quit menu's in a game (before: the BFME2 movie's 0.55 s closing animation), whose buttons then take the pointer again;
##   * with OPENBFME_LOBBY (a local test lobby, never production): signed in, the online home's OPTIONS opens the same menu; a style chosen there
##     takes effect when the player is back on the main menu (before: never while the online client was signed in), and Online signs in again after it.
## Prints "SHELLFIX1 PASS|FAIL <what>", the ACCEPT timings ("SHELLFIX1 TIMING ...") and "SHELLFIX1 RESULT: <n> failed"; --screens=DIR saves the screens.
extends RefCounted

const Options := preload("res://scripts/game_options.gd")
const STYLE_SOURCE := {"rotwk": "rotwk-201", "bfme2": "bfme2-106"}

var game: Node
var shell: Node
var failures := 0
var _checks := 0


func check(ok: bool, message: String) -> void:
	_checks += 1
	print("SHELLFIX1 ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1


func _finish() -> void:
	check(_checks > 1, "the check ran its steps")
	print("SHELLFIX1 RESULT: %d failed" % failures)
	game.get_tree().quit(0 if failures == 0 else 1)


func _top() -> String:
	var s: PackedStringArray = shell.shell_stack()
	return "" if s.is_empty() else s[-1]


func _shot(name: String) -> void:
	if game._screens.is_empty() or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var size: Vector2i = game.get_tree().root.size
	game._save_named("shellfix1-%dx%d-%s" % [size.x, size.y, name])


func _click(stage: Vector2) -> void:
	game._mouse_event(stage, false, false)
	await game._step(2)
	game._mouse_event(stage, true, true)
	await game._step(2)
	game._mouse_event(stage, false, true)


## the hittable button of the top screen whose path holds `part` (OnlineShell.apt: "Options.Button_Shell")
func _button(part: String) -> Dictionary:
	for b in shell.list_buttons(shell.shell_top_level()):
		if b.hittable and str(b.path).contains(part):
			return b
	return {}


func _install_root(style: String) -> String:
	return OS.get_environment("ROTWK_INSTALL" if style == "rotwk" else "BFME2_INSTALL").simplify_path().to_lower()


## every movie on the stack and every picture of the last frame from `style`'s archives; none of the other game's images
func _check_sources(where: String, style: String) -> void:
	await game._step(3)
	var d: Dictionary = shell.shell_drawn_sources()
	check(d.get("style", "") == STYLE_SOURCE[style] and d.get("view3d_style", "") == STYLE_SOURCE[style],
		"%s: the shell and its 3D menu models are %s's (%s, %s)" % [where, style, d.get("style", d.get("error", "?")), d.get("view3d_style", "?")])
	var movies: Dictionary = d.get("movies", {})
	var root := _install_root(style)
	var wrong := []
	for movie in movies:
		if not str(movies[movie]).simplify_path().to_lower().begins_with(root):
			wrong.append("%s <- %s" % [movie, movies[movie]])
	check(not movies.is_empty() and not root.is_empty() and wrong.is_empty(), "%s: the movies %s come from the %s install %s" % [where, movies.keys(), style, wrong])
	var textures: Dictionary = d.get("textures", {})
	var stray := []
	for t in textures:
		var archive := str(textures[t])
		# RotWK reads through the game's own mount (RotWK 2.01 over BFME2 1.06, as retail): the archive that mount resolves; BFME2: its install only
		var expected: bool = archive == game._fs.get_archive_for_file("art/textures/" + str(t)) if style == "rotwk" else archive.simplify_path().to_lower().begins_with(root)
		if archive.is_empty() or not expected:
			stray.append("%s <- %s" % [t, archive])
	check(not textures.is_empty() and stray.is_empty(), "%s: the %d pictures drawn come from %s's archives %s" % [where, textures.size(), style, stray])
	check(d.get("original_images", []).is_empty(), "%s: no picture of the other game: %s" % [where, d.get("original_images", [])])


## the main menu's navigation can be pressed (SoloPlayNav, MultiPlayNav, OptionsNav)
func _main_interactive() -> bool:
	if _top() != "MainMenu.apt":
		return false
	var level: int = shell.shell_top_level()
	var buttons: Array = shell.list_buttons(level)
	for nav in ["SoloPlayNav", "MultiPlayNav", "OptionsNav"]:
		var b: Dictionary = shell.find_button(level, nav)
		var ok := false
		for x in buttons if b.get("found", false) else []:
			ok = ok or (x.path == b.path and x.hittable)
		if not ok:
			return false
	return true


## the online home with its bottom bar back: LOG OUT, OPTIONS and the page buttons pressable
func _online_interactive() -> bool:
	if _top() != "OnlineShell.apt" or shell.online_status().get("page", "") != "OnlineHome":
		return false
	for part in ["Exit.Button_Shell", "Options.Button_Shell", "ButtonNav.OpenPlay"]:
		if _button(part).is_empty():
			return false
	return true


## the quit menu over the game, its buttons pressable, Options gone
func _quit_interactive() -> bool:
	if game._display_screen.is_open() or _top() == "Options.apt":
		return false
	var r: Dictionary = shell.quit_menu({"kind": "state"})
	if not r.get("open", false):
		return false
	for b in shell.list_buttons(r.level):
		if b.hittable:
			return true
	return false


## ACCEPT pressed with the pointer: the frames from its release until `on_top` holds (Options gone, the previous screen on top; at most 10 s; -1 when
## it never does), then until `pressable` holds (the previous screen's buttons take the pointer: after the movie's own re-entry animation, which
## retail plays too: BFME2 1.06's QuitMenu.apt reloaded under Options (RW 0x812A37 / 0x812913) and its OnlineShell.apt's ShowButtonBar keep their buttons
## from the pointer until they end; RotWK 2.01's do not)
func _accept(where: String, on_top: Callable, pressable: Callable) -> int:
	var frames := await _accept_until(where, on_top, "on top")
	if frames >= 0:
		var more := await _wait_frames(pressable)
		check(more >= 0, "%s: the screen's buttons take the pointer again after ACCEPT (%d more frames)" % [where, more])
	return frames


func _wait_frames(cond: Callable) -> int:
	var t0 := Time.get_ticks_usec()
	var frames := 0
	while not cond.call():
		if Time.get_ticks_usec() - t0 > 10000000:
			return -1
		await game.get_tree().process_frame
		frames += 1
	print("SHELLFIX1 TIMING   pressable %d frames, %.1f ms later" % [frames, (Time.get_ticks_usec() - t0) / 1000.0])
	return frames


func _accept_until(where: String, back: Callable, what: String) -> int:
	var buttons: Dictionary = shell.openbfme_page_state().get("buttons", {})
	check(buttons.has("accept"), where + ": the menu's ACCEPT is on screen")
	if not buttons.has("accept"):
		return -1
	var p: Vector2 = (buttons.accept as Rect2).get_center()
	game._mouse_event(p, false, false)
	await game._step(2)
	game._mouse_event(p, true, true)
	await game._step(2)
	var t0 := Time.get_ticks_usec()
	game._mouse_event(p, false, true)
	var frames := 0
	while frames < 600 and Time.get_ticks_usec() - t0 < 10000000:
		await game.get_tree().process_frame
		frames += 1
		if back.call():
			break
	var ms := (Time.get_ticks_usec() - t0) / 1000.0
	print("SHELLFIX1 TIMING %s: ACCEPT -> %s in %d frames, %.1f ms (window %s)" % [where, what, frames, ms, game.get_tree().root.size])
	return frames if back.call() else -1


func _open_settings(where: String) -> bool:
	var level: int = shell.shell_top_level()
	for b in ["OptionsNav", "OptionsNav.Settings"]:
		if not await game._click_button(level, b):
			check(false, "%s: press %s" % [where, b])
			return false
		await game._step(60)
	return await _wait_options(where)


func _wait_options(where: String) -> bool:
	var until := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < until and not (game._display_screen.is_open() and _top() == "Options.apt"):
		await game.get_tree().process_frame
	await game._step(30)
	var open: bool = game._display_screen.is_open() and _top() == "Options.apt"
	check(open, "%s: the OpenBFME Options menu is open %s" % [where, shell.shell_stack()])
	return open


func _settled(style: String) -> bool:
	var until := Time.get_ticks_msec() + 45000
	while Time.get_ticks_msec() < until:
		if game._state == game.State.FAILED:
			check(false, str(game._failed))
			return false
		if game._menu_style == style and not game._switching_menu_style and game._clean_main_menu():
			await game._step(30)
			return true
		await game.get_tree().process_frame
	check(false, "a clean %s main menu within 45 s (style %s, pending %s, stack %s)" % [style, game._menu_style, game._pending_menu_style, shell.shell_stack()])
	return false


func run(owner: Node) -> void:
	game = owner
	shell = game._shell
	if game._options_file.is_empty() or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		check(false, "needs an isolated --options-file and ROTWK_INSTALL / BFME2_INSTALL")
		return _finish()
	var style: String = game._menu_style
	if not await _settled(style):
		return _finish()
	if DisplayServer.get_name() == "headless" and game.get_tree().root.size.x < 640:
		game.get_tree().root.size = Vector2i(1280, 800) # pointer positions need a real window size
		await game._step(10)
	await _check_sources("the main menu (%s)" % style, style)
	await _shot(style + "-main-menu")
	# 1. the main menu's OPTIONS > SETTINGS, then ACCEPT
	if not await _open_settings("main menu (%s)" % style):
		return _finish()
	await game._wait_seconds(1.0)
	await _check_sources("Options from the main menu (%s)" % style, style)
	await _shot(style + "-options-from-main-menu")
	var frames := await _accept("main menu (%s)" % style, func() -> bool: return _top() == "MainMenu.apt" and not game._display_screen.is_open(), _main_interactive)
	check(frames == 2, "main menu (%s): ACCEPT is back on the main menu on the next shell update (2 frames): %d" % [style, frames])
	await game._step(30)
	await _check_sources("the main menu after ACCEPT (%s)" % style, style)
	if OS.get_environment("OPENBFME_LOBBY").is_empty():
		print("SHELLFIX1 SKIP the online home part: OPENBFME_LOBBY names no test lobby")
	else:
		await _online(style)
	_finish()


func _sign_in(where: String) -> bool:
	var level: int = shell.shell_top_level()
	await game._click_button(level, "MultiPlayNav")
	await game._step(60)
	await game._click_button(level, "MultiPlayNav.Online")
	if not await game._online_wait("the sign-in page", func(st): return st.get("page", "") == "OnlineLogin" or (st.get("signed_in", false) and st.get("page", "") == "OnlineHome"), 30.0):
		check(false, where + ": the main menu's ONLINE opens the online screens")
		return false
	if shell.online_status().get("page", "") == "OnlineLogin":
		await game._step(60)
		game._online_apply({"nickname": "ShellFix"})
		game._online_apply({"command": "AptOnline::Login::Login"})
	var home: bool = await game._online_wait("the home page", func(st): return st.get("signed_in", false) and st.get("page", "") == "OnlineHome", 30.0)
	check(home, where + ": signed in, on the online home")
	if home:
		var until := Time.get_ticks_msec() + 10000
		while Time.get_ticks_msec() < until and not _online_interactive():
			await game.get_tree().process_frame
		await game._wait_seconds(1.0)
	return home


## 2. signed in: the online home's OPTIONS (the same menu, the same style), a style chosen there, ACCEPT back to the home; the style changes on the
## main menu; Online works again after the switch
func _online(style: String) -> void:
	var other := "bfme2" if style == "rotwk" else "rotwk"
	if not await _sign_in("online (%s)" % style):
		return
	await _check_sources("the online home (%s)" % style, style)
	await _shot(style + "-online-home")
	var options := _button("Options.Button_Shell")
	check(not options.is_empty(), "online (%s): the online home's OPTIONS can be pressed" % style)
	if options.is_empty():
		return
	await _click(Vector2(options.x, options.y))
	if not await _wait_options("online home (%s)" % style):
		return
	check(game._display_screen.current_tab() == "Display" and shell.options_page() == 3, "online (%s): the same OpenBFME menu as the main menu's (%s, page %d)" % [
		style, game._display_screen.current_tab(), shell.options_page()])
	await game._wait_seconds(1.0)
	await _check_sources("Options from the online home (%s)" % style, style)
	await _shot(style + "-options-from-online-home")
	var ms = load("res://scripts/menustyle_check.gd").new()
	ms.game = game
	await ms._choose_style(other)
	failures += ms.failures
	var frames := await _accept("online home (%s)" % style, func() -> bool: return _top() == "OnlineShell.apt" and not game._display_screen.is_open() and \
		shell.online_status().get("page", "") == "OnlineHome", _online_interactive)
	check(frames == 2, "online home (%s): ACCEPT is back on the online home on the next shell update (2 frames): %d" % [style, frames])
	check(shell.online_status().get("signed_in", false), "online (%s): still signed in after ACCEPT" % style)
	check(AptMenuPlayer.read_options_file(game._options_path()).get("values", {}).get(Options.MENU_STYLE_KEY, "") == other, "online (%s): Options.ini holds %s" % [style, other])
	await game._step(30)
	await _check_sources("the online home after ACCEPT (%s; %s waits for the main menu)" % [style, other], style)
	# back to the main menu with the online screens' EXIT: the chosen style takes effect there (the client is still signed in)
	var exit := _button("Exit.Button_Shell")
	check(not exit.is_empty(), "online (%s): EXIT can be pressed" % style)
	if exit.is_empty():
		return
	await _click(Vector2(exit.x, exit.y))
	if not await _settled(other):
		return
	await _check_sources("the main menu after leaving the online screens (%s -> %s)" % [style, other], other)
	await _shot(other + "-main-menu-after-online")
	if not await _sign_in("online again (%s)" % other):
		return
	await _check_sources("the online home after the switch (%s)" % other, other)
	await _shot(other + "-online-home-after-switch")


## --auto --shellfix1-game: the quit menu's Options over a live game, ACCEPT back to the quit menu
func run_game(owner: Node) -> int:
	game = owner
	shell = game._shell
	var root: Window = game.get_tree().root
	if DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
		await game._step(10)
	var style: String = game._menu_style
	check(game._hud != null and game._state == game.State.PLAYING, "a live game (%s)" % style)
	await game._press_escape()
	await game._wait_seconds(3.0)
	if not await game._click_quit("Options"):
		check(false, "the quit menu's Options can be pressed")
	elif await _wait_options("in a game (%s)" % style):
		await game._wait_seconds(1.0)
		await _check_sources("Options in a game (%s)" % style, style)
		await _shot(style + "-options-in-game")
		var frames := await _accept("in a game (%s)" % style, func() -> bool: return _top() != "Options.apt" and not game._display_screen.is_open() and \
			shell.quit_menu({"kind": "state"}).get("open", false), _quit_interactive)
		check(frames == 2, "in a game (%s): ACCEPT is back on the quit menu on the next shell update (2 frames): %d" % [style, frames])
	check(_checks > 1, "the check ran its steps")
	print("SHELLFIX1 RESULT: %d failed" % failures)
	return failures
