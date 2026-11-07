extends SceneTree

func _initialize() -> void:
	var client = load("res://scripts/online_lobby.gd").new()
	assert(client._compatibility_details() == {})
	client.profile_details = "engine test\narchive 0 rotwk:ini.big aaaa 10\n"
	assert(client._compatibility_details() == {"profile_details": client.profile_details})
	client.profile_details = "engine test\n" + "é".repeat(6000) + "\n"
	assert(client._compatibility_details() == {"profile_details": "engine test\ndetails truncated\n"})
	client.free()
	print("Online compatibility details: 3 checks passed")
	quit()
