extends SceneTree

func _initialize() -> void:
	var game = load("res://scripts/game.gd").new()
	var previous := OS.get_environment("OPENBFME_LOBBY")
	OS.set_environment("OPENBFME_LOBBY", "https://old-lobby.invalid")
	game._online_lobby = "https://launch-argument.invalid"
	assert(game._online_lobby_url() == "https://secondage.dev")
	game._online_lobby = ""
	assert(game._online_lobby_url() == "https://secondage.dev")
	OS.set_environment("OPENBFME_LOBBY", previous)
	game.free()
	print("FORK LOBBY PASS: Second Age is used despite legacy overrides")
	quit()
