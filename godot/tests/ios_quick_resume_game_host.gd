## Isolate the full-host test's replay recording from the player's Last Replay.
extends "res://scripts/game.gd"
func _last_replay_path() -> String:
	return OS.get_environment("RESUME_TEST_DIR").path_join("record.BfME2Replay")
