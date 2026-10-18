## OpenBFME campaign additions use the selected shell's font and mapped-image skin.
extends CanvasLayer

## lane DEVPORT-8: THE switch of the BFME2 Good / Evil campaigns (lane B2CAMP-1 turns it on with its campaign entry point). Off, their buttons are not in
## the shipped menu: not in this panel, and BFME2's own MainMenu.apt GoodCampaign / EvilCampaign buttons are hidden (game.gd); a request that still
## names them is refused by name (game.gd start_bfme2_campaign). The test: godot/tests/menustyle_campaigns_test.gd
const BFME2_CAMPAIGNS := false

var game: Node
var _root: Control
var _panel: VBoxContainer
var _notice: Label
var _buttons: Array[Button] = []
var _font: Font
var _font_size := 16
var _normal: StyleBoxTexture
var _hover: StyleBoxTexture


func _ready() -> void:
	layer = 15
	_root = Control.new()
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	var font: Dictionary = game._shell.shell_font("Albertus MT", 18.0)
	if font.get("font") == null or font.get("fallback", true):
		game._fail("campaign menu: selected shell font is unavailable")
		return
	_font = font.font
	_font_size = int(font.size)
	_normal = _skin("AptTextEntryEnabledRepeatingCenter")
	_hover = _skin("AptListBoxHiliteItem")
	if _normal == null or _hover == null:
		return
	var toggle := _button("Campaigns", "campaigns")
	toggle.position = Vector2(36, 588)
	toggle.size = Vector2(270, 32)
	_root.add_child(toggle)
	_panel = VBoxContainer.new()
	_panel.position = Vector2(36, 352)
	_panel.size = Vector2(270, 220)
	_panel.add_theme_constant_override("separation", 3)
	_root.add_child(_panel)
	_panel.hide()
	for entry in entries():
		_panel.add_child(_button(entry[0], entry[1]))
	_notice = Label.new()
	_notice.position = Vector2(36, 630)
	_notice.add_theme_font_override("font", _font)
	_notice.add_theme_font_size_override("font_size", _font_size)
	_notice.set_meta("game_text", true)
	_notice.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_root.add_child(_notice)


## the panel's rows [title, shell action]: the BFME2 campaigns only with BFME2_CAMPAIGNS
static func entries() -> Array:
	var out := []
	if BFME2_CAMPAIGNS:
		out.append(["BFME2 Good campaign", "BFME2GoodCampaign"])
		out.append(["BFME2 Evil campaign", "BFME2EvilCampaign"])
	out.append_array([
		["Angmar campaign", "Expansion1Campaign"],
		["Angmar bonus campaign", "BonusCampaign"],
		["Continue campaign", "ContinueCampaign"],
	])
	return out


## the names of the panel's buttons (the checks)
func button_names() -> PackedStringArray:
	var out := PackedStringArray()
	for b in _buttons:
		out.append(str(b.name))
	return out


func _skin(image_name: String) -> StyleBoxTexture:
	var image: Dictionary = game._shell.shell_mapped_image(image_name)
	if not image.get("ok", false):
		game._fail("campaign menu: " + str(image.get("error", "missing skin")))
		return null
	var texture := AtlasTexture.new()
	texture.atlas = image.texture
	texture.region = image.region
	var box := StyleBoxTexture.new()
	box.texture = texture
	box.content_margin_left = 10
	box.content_margin_right = 10
	box.content_margin_top = 5
	box.content_margin_bottom = 5
	return box


func _button(title: String, action: String) -> Button:
	var button := Button.new()
	button.name = action
	button.text = title
	button.set_meta("game_text", true)
	button.custom_minimum_size = Vector2(270, 32)
	button.add_theme_font_override("font", _font)
	button.add_theme_font_size_override("font_size", _font_size)
	button.add_theme_stylebox_override("normal", _normal)
	for state in ["hover", "pressed", "focus"]:
		button.add_theme_stylebox_override(state, _hover)
	button.pressed.connect(func():
		if action == "campaigns":
			_panel.visible = not _panel.visible
		else:
			_panel.hide()
			game._on_shell_request(action, ""))
	_buttons.append(button)
	return button


func show_notice(message: String) -> void:
	_notice.text = message


func notice_text() -> String:
	return _notice.text


func _process(_delta: float) -> void:
	var show_menu: bool = game._clean_main_menu()
	_root.visible = show_menu
	if not show_menu:
		if _panel != null:
			_panel.hide()
		return
	var origin: Vector2 = game._shell.stage_to_window(Vector2.ZERO)
	var corner: Vector2 = game._shell.stage_to_window(Vector2(1024, 768))
	_root.scale = (corner - origin) / Vector2(1024, 768)
	_root.position = origin
