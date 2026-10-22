extends "res://tests/ios_quick_resume_game_host.gd"
var checked_loading_art := false
func _on_load_progress(percent: int) -> void:
	super._on_load_progress(percent)
	assert(_shell.get_backdrop_state().backdrop_image == "ShellMapLowLOD")
	checked_loading_art = true
