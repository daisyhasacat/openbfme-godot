## Reads the extended real command-recorded fight, then proves identical metrics after restart/fast-forward.
extends SceneTree

var failures := 0


func check(ok: bool, what: String) -> void:
	print("OVERLAY ", "PASS " if ok else "FAIL ", what)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	check(fs.mount_retail().ok, "mount retail fixture inputs")
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	world.set_auto_advance(false)
	check(world.setup(fs).ok, "setup")
	var path := OS.get_user_data_dir().path_join("caster-overlay.replay")
	check(world.prepare_replay(path).ok, "prepare fixture")
	check(world.start_replay({"logic_thread": false}).ok, "start fixture")
	var final_frame: int = world.replay_status().final_frame
	var baseline: Dictionary = {}
	var queues := false
	var upgrades := false
	var alive := false
	var dead := false
	var source_match := true
	var no_mutation := true
	while world.get_frame() < final_frame:
		world.advance(0.2)
		var hash_before: int = world.get_state_hash()
		var stats: Dictionary = world.caster_metrics()
		var economy: Dictionary = world.get_economy()
		for player in world.caster_status().players:
			var rows: Array = stats.players.filter(func(p): return p.index == player.index)
			var money: Array = economy.players.filter(func(p): return p.index == player.index)
			if rows.is_empty() or money.is_empty():
				source_match = false
				continue
			var row: Dictionary = rows[0]
			source_match = source_match and row.money == money[0].money and row.cp_used == money[0].cp_used and row.cp_limit == money[0].cp_limit
			for q in row.production:
				queues = queues or (q.kind == "hero" and q.name == "RohanEomer" and q.percent >= 0 and q.percent <= 100)
				upgrades = upgrades or (q.kind == "upgrade" and q.name == "Upgrade_MenFortressBanners")
			for h in row.heroes:
				if h.state == "alive":
					alive = alive or (h.name == "RohanEomer" and h.health > 0)
					var object: Dictionary = world.get_object(h.object)
					source_match = source_match and object.ok and h.health == object.health
				dead = dead or h.state == "dead"
		no_mutation = no_mutation and hash_before == world.get_state_hash()
		if world.get_frame() == 305:
			baseline = stats.duplicate(true)
	check(source_match, "money, command points and live hero health equal native sources every frame")
	check(no_mutation, "statistics reads never change a state hash")
	check(queues and upgrades, "real hero and upgrade queues appear with progress")
	check(alive and dead, "real living and dead hero states appear")
	check(world.replay_status().mismatches == 0 and world.replay_status().hashes_compared == final_frame - 1, "every recorded CRC matches with metrics enabled")
	var full: Dictionary = world.caster_metrics()
	check(full.history.size() == 1 + final_frame / 5, "initial graph sample plus each completed simulation second")
	check(not baseline.is_empty(), "straight-playback frame-305 baseline captured")
	world.clear_game_data(false)
	check(world.prepare_replay(path).ok, "prepare rewind")
	check(world.start_replay({"logic_thread": true}).ok, "restart with threaded logic")
	while true:
		var seek: Dictionary = world.replay_seek_step(305, 20)
		if not seek.ok:
			check(false, "seek: " + str(seek.error))
			break
		if seek.ready:
			break
	check(world.caster_metrics() == baseline, "restart/fast-forward reproduces all counters, queues, heroes and graph samples")
	check(world.replay_status().mismatches == 0 and world.replay_status().hashes_compared == 304, "rewind checks every intervening CRC")
	var plot = load("res://scripts/caster_overlays.gd").Graph.new()
	plot.metric = "income"
	check(not plot.valid({"income_complete": false}), "incomplete income remains a graph gap")
	plot.metric = "army_value"
	check(not plot.valid({"army_complete": false}), "unknown build cost remains a graph gap")
	plot.free()
	world.clear_game_data(false)
	world.free()
	print("OVERLAY RESULT failures=", failures)
	quit(0 if failures == 0 else 1)
