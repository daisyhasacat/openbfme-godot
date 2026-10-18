## Headless test of lane PLATFORMICON-1: the three platform icons load as Godot resources (the engine's mapped images name res://assets/platform_icons/<name>.png), are
## 32 x 32 white-on-transparent glyphs of their own (not empty, not the same picture twice), and the first-run import produced them.
##
##   godot --headless --path godot --script res://tests/platformicon1_test.gd
##
## Exit codes: 0 pass, 1 fail.
extends SceneTree

var _failures := 0
## SHA-256 of each icon's RGBA8 pixels as tools/platformicon/make_icons.py draws them (tools/platformicon/test_make_icons.py pins the same values): the imported
## resource must be the CURRENT picture, not an older import
const PIXELS_SHA256 := {
	"windows": "c7f152a8e34943c85133926a6a56766316da93055cd2e659fc540d9798ecaa1f",
	"linux": "c39e3f5d8f29e09f77a7cd9d184b04d77dfbb6efc410431086a442f9e7bae67e",
	"macos": "7be127ff253d4f371d182341c1f3b0e2b9d27c4257fc065f45925677e90eb380",
}


func _initialize() -> void:
	var code: int = _run()
	print("PLATFORMICON1 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	var covered: Array[int] = []
	var pictures := {}
	for name in ["windows", "linux", "macos"]:
		var path := "res://assets/platform_icons/%s.png" % name
		var tex = ResourceLoader.load(path)
		_check(tex is Texture2D, "%s loads as a Texture2D" % path)
		if not (tex is Texture2D):
			continue
		var image: Image = tex.get_image()
		_check(image.get_width() == 32 and image.get_height() == 32, "%s is 32 x 32 (%d x %d)" % [name, image.get_width(), image.get_height()])
		image.convert(Image.FORMAT_RGBA8)
		var opaque := 0
		var solid_white := true
		var data := image.get_data()
		for i in range(0, data.size(), 4):
			if data[i + 3] > 0:
				opaque += 1
				if data[i] != 255 or data[i + 1] != 255 or data[i + 2] != 255:
					solid_white = false
		var hasher := HashingContext.new()
		hasher.start(HashingContext.HASH_SHA256)
		hasher.update(data)
		_check(hasher.finish().hex_encode() == PIXELS_SHA256[name], "%s's imported pixels are the current glyph (sha256 %s)" % [name, PIXELS_SHA256[name].substr(0, 12)])
		covered.append(opaque)
		_check(opaque > 150 and opaque < 700, "%s draws a glyph: %d of 1024 pixels have alpha" % [name, opaque])
		_check(solid_white, "%s is white on transparent (the engine tints nothing: 0xFFFFFFFF)" % name)
		pictures[hash(data)] = name
	_check(pictures.size() == 3, "the three glyphs are three different pictures (%d)" % pictures.size())
	return 0 if _failures == 0 else 1
