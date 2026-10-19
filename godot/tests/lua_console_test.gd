extends SceneTree
# The bfme.* scripting console (scripts/lua_console.lua) and the Lua paths (GameWorld.get_lua_paths): no game files needed.
# Option: -- --console-shot=<png>

func _initialize() -> void:
	call_deferred("_run")

func _write(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert(file != null, path)
	file.store_string(text)
	file.close()

func _run() -> void:
	var game_script = load("res://scripts/game.gd")
	assert(game_script != null and game_script.can_instantiate())
	# two extra Lua paths: the second replaces the first's script of the same name and supplies a lib/ module
	var base := OS.get_cache_dir().path_join("openbfme-lua-test-%d" % OS.get_process_id())
	var first := base.path_join("first")
	var second := base.path_join("second")
	_write(first.path_join("10_value.lua"), "luaTestValue = 'first'\n")
	_write(first.path_join("20_only_first.lua"), "luaTestOnlyFirst = true\n")
	_write(second.path_join("10_value.lua"), "luaTestValue = 'second'\nluaTestModule = require('test.helper').answer()\n")
	_write(second.path_join("lib/test/helper.lua"), "return { answer = function() return 42 end }\n")
	OS.set_environment("OPENBFME_LUA_PATH", first + ";" + second)

	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var paths: PackedStringArray = world.get_lua_paths()
	assert(paths.size() == 4 and paths[0] == "res://lua" and paths[1] == "user://lua" and paths[2] == first and paths[3] == second, str(paths))
	var console = preload("res://scripts/lua_console.lua").new()
	console.history_path = base.path_join("console_history.cfg")
	console.execute = func(code: String) -> Dictionary: return world.execute_lua(code)
	root.add_child(console)

	var version: Dictionary = world.execute_lua("return _VERSION")
	assert(version.ok and version.output.contains("Lua 5.1")) # LuaJIT
	var module: Dictionary = world.execute_lua("return bfme == import('bfme'), bfme == require('bfme'), bfme.match.getStatus().ready")
	assert(module.ok and module.output == "true\ttrue\tfalse\n")
	# a gameplay write needs a match: queued, then refused at the next safe point
	var pending: Dictionary = world.execute_lua("pause_ticket = bfme.match.pause(); return bfme.mutations.getStatus(pause_ticket)")
	assert(pending.ok and pending.output == "pending\n")
	world.update_lua()
	var failed: Dictionary = world.execute_lua("return bfme.mutations.getStatus(pause_ticket)")
	assert(failed.ok and failed.output == "no active match\n")

	# the toggle is Ctrl+Shift+C; backtick stays the game's (RotWK's powers menu)
	assert(not console.is_open())
	var tick := InputEventKey.new()
	tick.keycode = KEY_QUOTELEFT
	tick.physical_keycode = KEY_QUOTELEFT
	tick.pressed = true
	assert(not console.is_toggle_event(tick))
	console._input(tick)
	assert(not console.is_open())
	var toggle := InputEventKey.new()
	toggle.keycode = KEY_C
	toggle.ctrl_pressed = true
	toggle.shift_pressed = true
	toggle.pressed = true
	assert(console.is_toggle_event(toggle))
	console._input(toggle)
	assert(console.is_open())
	assert(console._panel.scale == Vector2(2, 2))
	var plain_c := InputEventKey.new()
	plain_c.keycode = KEY_C
	plain_c.pressed = true
	assert(not console.is_toggle_event(plain_c))

	# the console and the log, nothing else: two views and no buttons
	assert(console._tabs.get_tab_count() == 2)
	assert(console._tabs.get_tab_title(0) == "Command" and console._tabs.get_tab_title(1) == "Log")
	assert(console.find_children("*", "BaseButton", true, false).is_empty(), str(console.find_children("*", "BaseButton", true, false)))

	# Ctrl+Shift+F5 reloads the entry scripts of the Lua paths: the later path's file of the same name wins, require reads lib/
	console._entry.text = "-- keep my draft"
	var reload_key := InputEventKey.new()
	reload_key.keycode = KEY_F5
	reload_key.ctrl_pressed = true
	reload_key.shift_pressed = true
	reload_key.pressed = true
	console._input(reload_key)
	assert(console._entry.text == "-- keep my draft")
	console._entry.clear()
	var loaded: Dictionary = world.execute_lua("return luaTestValue, luaTestOnlyFirst, luaTestModule")
	assert(loaded.ok and loaded.output == "second\ttrue\t42\n", str(loaded))
	assert(console._log.text.contains("Reloaded " + second.path_join("10_value.lua")), console._log.text)
	assert(not console._log.text.contains("Reloaded " + first.path_join("10_value.lua")))

	# commands, output, history, errors and multiline scripts
	console._submit("console_value = 40")
	console._submit("=console_value + 2")
	assert(console._output.text.contains("42"))
	console._submit("print('console smoke')")
	assert(console._output.text.contains("console smoke"))
	console._append("scroll test\n".repeat(150))
	await process_frame
	await process_frame
	for view in [console._output, console._log]:
		view.set_v_scroll(0)
	console._append("newest output\n")
	await process_frame
	await process_frame
	for view in [console._output, console._log]:
		var bar = view.get_v_scroll_bar()
		# tolerance: the wrapped view settles its scroll range a frame late; within one row of the end is the bottom
		assert(view.get_v_scroll() >= bar.max_value - bar.page - 1, "Appended console output must scroll to the bottom")
	console._submit("error('expected console error')")
	assert(console._output.text.contains("expected console error"))
	var up := InputEventKey.new()
	up.keycode = KEY_UP
	up.pressed = true
	console._entry_input(up)
	assert(console._entry.text == "error('expected console error')")
	console._submit("local sum = 0\nfor i = 1, 4 do\n    sum = sum + i\nend\nreturn sum")
	assert(console._output.text.contains("10"))
	console._tabs.current_tab = 1
	console._clear_visible()
	assert(console._log.text.is_empty() and not console._output.text.is_empty())
	console._tabs.current_tab = 0
	console._submit("clear")
	assert(console._output.text.is_empty())
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--console-shot="):
			console._submit("print('LuaJIT · bfme runtime')")
			console._entry.text = "local me = bfme.players.localPlayer\nif me then\n    print(me, me.money)\nend"
			await process_frame
			await process_frame
			await RenderingServer.frame_post_draw
			assert(root.get_texture().get_image().save_png(arg.substr(15)) == OK)

	# Escape closes it; history persists between sessions
	var escape := InputEventKey.new()
	escape.keycode = KEY_ESCAPE
	escape.pressed = true
	console._input(escape)
	assert(not console.is_open())
	var persisted := "local value = 7\nreturn value * 2"
	console._submit(persisted)
	console._submit(persisted)
	var saved_history = console._history.duplicate()
	assert(saved_history[-1] == persisted and saved_history[-2] != persisted)
	var restored = preload("res://scripts/lua_console.lua").new()
	restored.history_path = console.history_path
	root.add_child(restored)
	assert(restored._history == saved_history)
	assert(restored._history_index == saved_history.size())
	restored._entry.text = "unfinished draft"
	restored._entry_input(up)
	assert(restored._entry.text == persisted)
	var down := InputEventKey.new()
	down.keycode = KEY_DOWN
	down.pressed = true
	restored._entry_input(down)
	assert(restored._entry.text == "unfinished draft")
	restored.queue_free()
	for path in [console.history_path, first.path_join("10_value.lua"), first.path_join("20_only_first.lua"), second.path_join("10_value.lua"), second.path_join("lib/test/helper.lua")]:
		DirAccess.remove_absolute(path)
	print("LUA CONSOLE PASS")
	quit(0)
