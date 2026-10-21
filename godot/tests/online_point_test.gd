## Online walkthrough clicks must reach the visible button's Hit records, including Create Game's translated hit area.
## Run with -- --lobby=URL; tools/net/test_online_point.py supplies an isolated lobby and retail mounts.
extends SceneTree

var shell: Node


func _initialize() -> void:
	_run.call_deferred()


func _wait(predicate: Callable) -> bool:
	for frame in 600:
		shell.tick(1.0 / 30.0)
		if predicate.call():
			return true
		await create_timer(0.005).timeout
	return false


func _click(path: String) -> void:
	var pointed: Dictionary = shell.online_apply({"point": path})
	assert(pointed.ok, str(pointed.errors))
	shell.tick(1.0 / 30.0)
	assert(String(shell.menu_nav_state().current_button).contains(path.trim_prefix("*")))
	assert(shell.online_apply({"mouse": true}).ok)
	shell.tick(1.0 / 30.0)
	assert(shell.online_apply({"mouse": false}).ok)
	shell.tick(1.0 / 30.0)


func _run() -> void:
	create_timer(45.0).timeout.connect(func(): quit(1))
	var lobby := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--lobby="):
			lobby = arg.trim_prefix("--lobby=")
	assert(not lobby.is_empty())
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"seed": 1}).ok)
	assert(shell.set_online(world, {"lobby_url": lobby}).ok)
	var http: Node = load("res://scripts/online_http.gd").new()
	http.shell = shell
	http.lobby_url = lobby
	root.add_child(http)
	assert(shell.shell_push("OnlineShell.apt"))
	assert(await _wait(func(): return shell.online_status().get("page", "") == "OnlineLogin"))
	for frame in 150:
		shell.tick(1.0 / 30.0)
	assert(shell.online_apply({"nickname": "PointTest", "command": "AptOnline::Login::Login"}).ok)
	assert(await _wait(func(): return shell.online_status().get("signed_in", false)))
	for frame in 150:
		shell.tick(1.0 / 30.0)
	_click("Toggles.ButtonNav.OpenPlay")
	assert(await _wait(func(): return shell.online_status().get("state", -1) == 2))
	for frame in 150:
		shell.tick(1.0 / 30.0)
	_click("*Controls.CreateGame")
	assert(await _wait(func(): return shell.online_status().get("state", -1) == 3))
	for frame in 60:
		shell.tick(1.0 / 30.0)
	assert(shell.online_apply({"game_name": "Hit area test"}).ok)
	_click("*CreateGamePopup.Accept")
	assert(await _wait(func(): return shell.online_status().get("in_game", false)))
	assert(shell.online_status().host == true)
	assert(shell.online_apply({"point": "*NoSuchOnlineButton"}).ok == false)
	print("ONLINE POINT PASS: navigation, Create Game, popup acceptance, missing target refused")
	quit()
