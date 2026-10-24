extends SceneTree

func _initialize() -> void:
	var diagnostics = load("res://scripts/online_compatibility.gd").new()
	var messages: Array[String] = []
	diagnostics.report_ready.connect(func(text: String) -> void: messages.append(text))
	# No diagnostic room is needed to retain the server's original explanation.
	diagnostics.begin("", "short-secret-token", false, "Mount order differs: archive 0 bfme2:window.big vs rotwk:lotrsec.big. short-secret-token")
	assert(messages[0] == "Mount order differs: archive 0 bfme2:window.big vs rotwk:lotrsec.big. <redacted>")
	assert(messages[1].contains("Per-file comparison unavailable"))
	diagnostics.free()
	print("Online refusal console: original explanation retained and credentials redacted")
	quit()
