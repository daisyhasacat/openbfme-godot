## Lane OWNERFIX-3: the generated window icon (scripts/app_icon.gd) decodes to the 256 x 256 OpenBFME icon, and the game sets it at start.
## godot --headless --path godot --script res://tests/app_icon_test.gd   ->  "APP ICON OK" and exit 0, else "APP ICON FAIL ..." and exit 1
extends SceneTree

const AppIcon := preload("res://scripts/app_icon.gd")


func _init() -> void:
	var fails: Array[String] = []
	var img: Image = AppIcon.image()
	if img == null:
		fails.append("the icon does not decode")
	else:
		if img.get_width() != 256 or img.get_height() != 256:
			fails.append("the icon is %dx%d" % [img.get_width(), img.get_height()])
		# the ring is gold on a dark tile (Godot's logo is blue and white): count the gold pixels
		var gold := 0
		for y in range(0, 256, 2):
			for x in range(0, 256, 2):
				var c := img.get_pixel(x, y)
				gold += 1 if c.a > 0.9 and c.r > 0.55 and c.r > c.b + 0.3 else 0
		if gold < 500:
			fails.append("the icon has %d gold sample pixels (the ring has thousands)" % gold)
	if AppIcon.apply() != "":
		fails.append("apply: " + AppIcon.apply())
	var game := FileAccess.get_file_as_string("res://scripts/game.gd")
	if not game.contains("AppIcon.apply()"):
		fails.append("game.gd does not set the icon at start")
	if fails.is_empty():
		print("APP ICON OK")
		quit(0)
	else:
		print("APP ICON FAIL ", "; ".join(fails))
		quit(1)
