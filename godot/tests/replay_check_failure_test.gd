## A stopped replay with a mismatch must fail --replay-check instead of waiting forever for a finished flag.
extends SceneTree

class FailedPlayback:
	extends Node3D
	func replay_status() -> Dictionary:
		return {"active": true, "finished": false, "frame": 2, "final_frame": 150,
			"hashes_compared": 1, "mismatches": 1, "first_mismatch_frame": 2}

func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	create_timer(2.0).timeout.connect(func():
		print("REPLAY CHECK FAILURE TEST timed out")
		quit(2))
	var script := GDScript.new()
	script.source_code = 'extends "res://scripts/game.gd"\nfunc _ready() -> void:\n\tpass\nfunc _process(_delta: float) -> void:\n\tpass\n'
	assert(script.reload() == OK)
	var game: Node = script.new()
	root.add_child(game)
	var playback := FailedPlayback.new()
	root.add_child(playback)
	game._world = playback
	game._replay_check = true
	game._replay_active = true
	game._replay_tick()
