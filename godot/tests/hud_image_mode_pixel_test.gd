## Pixel test of the HUD's Apt image modes (community PR #37, Sol review r1): InGameHudNode.image_mode_shader(mode) drawn the way the HUD draws a
## RenderImage clip (canvas_item_add_texture_rect_region with the clip's colour as the vertex colour), rendered by the GPU and read back.
## Sol r1 found the first shader multiplied the texture into COLOR twice (a canvas_item fragment's COLOR already holds texture * vertex colour):
## SOLID kept the texture alpha and GRAYSCALE stayed coloured.
##
##   godot --path godot --script res://tests/hud_image_mode_pixel_test.gd        (needs a GPU and a window; NOT --headless)
##
## Exit codes: 0 pass, 1 fail, 77 skip (headless / no extension).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code := await _run()
	print("HUD IMAGE MODE PIXEL TEST %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	if ok:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		_failures += 1


## one 32 x 32 quad of a 4 x 4 texture filled with `texel`, drawn with `mode` and vertex colour `tint` over `background`; the centre pixel
func _draw_one(mode: String, texel: Color, tint: Color, background: Color) -> Color:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(64, 64)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.transparent_bg = false
	get_root().add_child(viewport)
	var back := ColorRect.new()
	back.color = background
	back.size = Vector2(64, 64)
	viewport.add_child(back)
	var node := Node2D.new()
	viewport.add_child(node)
	var image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	image.fill(texel)
	var tex := ImageTexture.create_from_image(image)
	var rs := RenderingServer
	var item: RID = node.get_canvas_item()
	var code: String = ClassDB.class_call_static("InGameHudNode", "image_mode_shader", mode)
	var material: ShaderMaterial = null
	if not code.is_empty():
		var shader := Shader.new()
		shader.code = code
		material = ShaderMaterial.new()
		material.shader = shader
		rs.canvas_item_set_material(item, material.get_rid())
	rs.canvas_item_add_texture_rect_region(item, Rect2(16, 16, 32, 32), tex.get_rid(), Rect2(0, 0, 4, 4), tint)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var out := viewport.get_texture().get_image().get_pixel(32, 32)
	viewport.queue_free()
	return out


func _near(a: float, b: float) -> bool:
	return absf(a - b) < 0.03 # tolerance: 8-bit render target and the blend's rounding


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: needs a window and a GPU (run without --headless)")
		return EXIT_SKIP
	if not ClassDB.class_exists("InGameHudNode"):
		print("FAIL: the openbfme extension is not loaded")
		return EXIT_FAIL
	await process_frame
	var black := Color(0, 0, 0, 1)
	var white := Color(1, 1, 1, 1)
	var clear_red := Color(1, 0, 0, 0)  # a transparent texel whose RGB still holds a colour (the recruitment art)
	var red := Color(1, 0, 0, 1)

	_check(String(ClassDB.class_call_static("InGameHudNode", "image_mode_shader", "alpha")).is_empty(), "ALPHA has no shader (the default draw)")
	_check(String(ClassDB.class_call_static("InGameHudNode", "image_mode_shader", "nonsense")).is_empty(), "an unknown mode is ALPHA (0x412AF3)")

	var p := await _draw_one("", clear_red, white, black)
	print("  info ALPHA transparent red ", p)
	_check(_near(p.r, 0.0), "ALPHA: a transparent texel is not drawn")

	p = await _draw_one("solid", clear_red, white, black)
	print("  info SOLID transparent red ", p)
	_check(_near(p.r, 1.0) and _near(p.g, 0.0) and _near(p.b, 0.0), "SOLID ignores the texture alpha: the texel's RGB is drawn")

	p = await _draw_one("SOLID", clear_red, Color(0.5, 1, 1, 1), black)
	print("  info SOLID tinted ", p)
	_check(p.r > 0.15 and p.r < 0.8, "SOLID: the clip's colour still multiplies the texel (red 0.5 in, %.3f out)" % p.r)

	p = await _draw_one("grayscale", red, white, black)
	print("  info GRAYSCALE red ", p)
	_check(_near(p.r, p.g) and _near(p.g, p.b) and p.r > 0.05, "GRAYSCALE: equal channels")

	p = await _draw_one("solid_grayscale", clear_red, white, black)
	print("  info SOLID_GRAYSCALE transparent red ", p)
	_check(_near(p.r, p.g) and _near(p.g, p.b) and p.r > 0.05, "SOLID_GRAYSCALE: grey and drawn although the texel is transparent")

	var grey := Color(0.25, 0.25, 0.25, 1)
	var base := await _draw_one("", Color(0, 0, 0, 0), white, grey)
	p = await _draw_one("additive", Color(0.5, 0.5, 0.5, 1), white, grey)
	print("  info ADDITIVE background %s, drawn %s" % [base, p])
	_check(p.r > base.r + 0.1, "ADDITIVE adds to the background")
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
