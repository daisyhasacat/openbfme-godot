## Exercise the real options input handler without retail skin setup.
extends "res://scripts/openbfme_options_screen.gd"
func _ready() -> void:
	_mods_button = Button.new()
	add_child(_mods_button)
	_open = true
func _show_tab(name: String) -> void:
	_tab = name
