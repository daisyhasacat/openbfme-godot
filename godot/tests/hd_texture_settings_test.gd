extends SceneTree

const Packs := preload("res://scripts/texture_pack_settings.gd")
const Display := preload("res://scripts/display_settings.gd")

class GammaOptions extends Node:
	var hold_gamma := true

class Shell extends Node2D:
	var shown := {}
	func set_openbfme_available(_enabled): pass
	func shell_stack(): return PackedStringArray(["Options.apt"])
	func openbfme_page_events(): return []
	func openbfme_page_open(): return {"ok":true}
	func openbfme_page_state(): return {"menu":false}
	func openbfme_page_show(spec):
		shown = spec
		return {"ok":true}
	func openbfme_page_set_row(_row): pass

func _initialize(): call_deferred("_run")

func _run():
	create_timer(30).timeout.connect(func(): quit(1))
	var path := OS.get_environment("TMPDIR").path_join("openbfme-hd-textures-test.cfg")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	assert(Packs.read(path) == {"ok":true,"enabled":true})
	var config := ConfigFile.new()
	config.set_value("mods","enabled",["HD Edition","Gameplay mod"])
	config.set_value("mods","textures_only",["HD Edition"])
	config.set_value("mods","order",["Gameplay mod","HD Edition"])
	assert(config.save(path) == OK)
	var shell := Shell.new()
	root.add_child(shell)
	await process_frame
	var screen = load("res://scripts/openbfme_options_screen.gd").new()
	screen.shell = shell
	screen.settings = Display.new()
	screen.texture_packs_path = path
	root.add_child(screen)
	screen.open()
	screen._show_tab("Graphics")
	assert(screen.row("hd_textures").kind == "check")
	assert(screen.row("hd_textures").value == 1)
	screen._on_value("hd_textures",0)
	screen.close(false)
	assert(Packs.read(path).enabled) # Cancel keeps the on-disk setting.
	screen.open()
	screen._on_value("hd_textures",0)
	screen.close(true)
	assert(not Packs.read(path).enabled)
	assert(config.load(path) == OK)
	assert(config.get_value("mods","enabled") == ["HD Edition","Gameplay mod"])
	assert(config.get_value("mods","textures_only") == ["HD Edition"])
	assert(config.get_value("mods","order") == ["Gameplay mod","HD Edition"])
	screen.open()
	screen.reset_defaults()
	screen.close(true)
	assert(Packs.read(path).enabled)
	# A failed write keeps the screen open and explains the failure on the page.
	screen.open()
	screen.texture_packs_path = OS.get_environment("TMPDIR").path_join("missing-hd-settings-directory/mods.cfg")
	screen._on_value("hd_textures",0)
	var gamma := GammaOptions.new()
	screen.game_options = gamma
	screen.close(true)
	assert(gamma.hold_gamma)
	screen.game_options = null
	gamma.free()
	assert(screen.is_open())
	assert("Could not save HD textures" in shell.shown.note)
	screen.close(false)
	screen.queue_free()
	shell.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	await process_frame
	print("HD TEXTURE SETTINGS PASS")
	quit()
