## Camera policy checks; --record-fight also records commands for a real hero encounter.
extends SceneTree

const Director = preload("res://scripts/caster_director.gd")
var failures := 0


func check(ok: bool, description: String) -> void:
	print("DIRECTOR ", "PASS " if ok else "FAIL ", description)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func event(id: int, frame: int, weight: float, position: Vector2, kind: int = 0, hero: bool = false) -> Dictionary:
	return {"object": id, "frame": frame, "weight": weight, "position": position, "kind": kind,
		"hero": hero, "base": false, "structure": false, "name": "fixture", "player": 1}


func camera() -> Dictionary:
	return {"position": Vector2.ZERO, "height_above_ground": 300.0, "angle": 0.0, "pitch": 0.0}


func tick(d: RefCounted, events: Array, frame: int, delta: float, cam: Dictionary, manual: bool = false) -> Dictionary:
	var pose: Dictionary = d.tick(events, frame, delta, cam, manual)
	if not pose.is_empty():
		cam.position = pose.position
		cam.height_above_ground = pose.height
	d.remember_pose(cam)
	return pose


func policy() -> void:
	var small := event(10, 100, 20, Vector2(100, 0))
	var large := event(20, 100, 200, Vector2(400, 0))
	var hero := event(30, 100, 0, Vector2(500, 0), 1, true)
	var power := event(40, 100, 0, Vector2(600, 0), 2)
	var d = Director.new()
	check(d.choose([small, large], 100).object == 20, "largest actual-damage fight")
	check(d.choose([large, hero, power], 100).object == 30, "hero priority")
	var base_power := power.duplicate()
	base_power.base = true
	check(Director.category(base_power) == "power", "a fortress power is still a power, not damage")
	d.priorities.power = 10
	check(d.choose([large, hero, power], 100).object == 40, "caster can change priority")
	d.priorities.power = 0
	check(d.choose([power], 100).is_empty(), "zero disables a category")
	check(d.choose([small], 99).is_empty(), "future history is not a current event")
	check(d.choose([small], 176).is_empty(), "old events expire")
	d.set_enabled(true)
	var cam := camera()
	var pose := tick(d, [small], 100, 0.5, cam)
	check(is_equal_approx(pose.position.x, 15.625) and not pose.cut, "nearby motion uses smoothstep, not a cut")
	pose = tick(d, [small, large], 101, 0.5, cam)
	check(d.choice.object == 10, "minimum dwell holds against a stronger fight")
	pose = tick(d, [small, large], 101, 6.0, cam)
	check(d.choice.object == 20, "stronger fight selected after dwell")
	check(tick(d, [large], 101, 0.1, cam, true).is_empty(), "manual input takes over immediately")
	check(tick(d, [large], 101, 11.9, cam).is_empty(), "director stays off during idle interval")
	check(not tick(d, [large], 101, 0.2, cam).is_empty(), "director returns after idle")
	cam.position += Vector2(70, 0)
	check(tick(d, [large], 101, 0.1, cam).is_empty() and d.manual_remaining > 0, "external camera movement also takes over")
	d.set_enabled(true)
	cam = camera()
	pose = tick(d, [event(50, 100, 1, Vector2(3000, 0))], 100, 0.01, cam)
	check(pose.cut and pose.position == Vector2(3000, 0), "far-only cut")
	var restored = Director.new()
	restored.restore_preferences(d.preferences())
	check(restored.enabled and restored.priorities.power == 0, "tuning round-trips")


func record_fight() -> void:
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	check(fs.mount_retail().ok, "mount retail for scripted fight")
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	check(world.setup(fs).ok, "setup scripted fight")
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	for i in 2:
		slots[i] = {"state": 6, "name": "Encounter %d" % (i + 1), "accepted": true, "color": i, "start_pos": i, "player_template": 3, "team": i}
	var prepared: Dictionary = world.prepare_new_game({"mode": "skirmish", "difficulty": 1,
		"map": "maps/map mp fall back 4p/map mp fall back 4p.map", "map_crc": 0, "map_size": 0, "map_mask": 0,
		"seed": 777, "starting_cash": 10000, "slots": slots})
	check(prepared.ok, "prepare two opposing human slots")
	if not prepared.ok:
		print(prepared)
		world.queue_free()
		return
	var fixture := "caster-director"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--fixture-name="):
			fixture = arg.trim_prefix("--fixture-name=").validate_filename()
	var path := OS.get_user_data_dir().path_join(fixture + ".replay")
	var started: Dictionary = world.start_new_game({"record": path, "logic_thread": false})
	check(started.ok, "record scripted encounter")
	if not started.ok:
		print(started)
		world.queue_free()
		return
	var players: Array = []
	var player_names: Array = []
	var hashes: Array = []
	world.advance(0.2)
	hashes.append(world.get_state_hash())
	var midpoint := Vector2.ZERO
	for slot in 2:
		var player_name: String = started.start.slot_players[slot]
		player_names.append(player_name)
		var player := -1
		for entry in world.get_economy().players:
			if entry.name == player_name:
				player = entry.index
		check(player >= 0, "resolve slot's actual player index")
		players.append(player)
		var keep := -1
		for object in world.get_player_objects(player_name):
			if object.template == "MenFortressCitadel":
				keep = object.id
		check(keep > 0, "slot %d has a starting fortress" % slot)
		if keep <= 0:
			print(world.get_player_objects(player_name))
			world.clear_game_data(false)
			world.queue_free()
			return
		var object: Dictionary = world.get_object(keep)
		midpoint += Vector2(object.x, object.y) * 0.5
		var hero_index := -1
		for entry in world.get_heroes(player, keep):
			if entry.template == "RohanEomer":
				hero_index = entry.index
		check(hero_index >= 0, "Eomer is available through the real hero roster")
		world.queue_hero(player, keep, hero_index)
		if OS.get_cmdline_user_args().has("--until-death"):
			world.queue_upgrade(player, keep, "Upgrade_MenFortressBanners")
	var heroes := [0, 0]
	var health := [0.0, 0.0]
	var fight_frame := -1
	var ordered := false
	for step in 2000:
		world.advance(0.2)
		hashes.append(world.get_state_hash())
		for side in 2:
			if heroes[side] == 0:
				for object in world.get_player_objects(player_names[side]):
					if object.template == "RohanEomer":
						heroes[side] = object.id
						check(world.order_move([object.id], midpoint.x, midpoint.y).ok, "record hero approach order")
		if heroes[0] == 0 or heroes[1] == 0:
			continue
		var a: Dictionary = world.get_object(heroes[0])
		var b: Dictionary = world.get_object(heroes[1])
		if not a.ok or not b.ok:
			break
		var close: bool = Vector2(a.x, a.y).distance_to(midpoint) < 400 and Vector2(b.x, b.y).distance_to(midpoint) < 400
		if close and not ordered and Vector2(a.x, a.y).distance_to(Vector2(b.x, b.y)) < 120:
			check(world.order_attack([heroes[0]], heroes[1]).ok, "record first attack order")
			check(world.order_attack([heroes[1]], heroes[0]).ok, "record opposing attack order")
			ordered = true
		if ordered and fight_frame < 0 and (a.health < health[0] or b.health < health[1]):
			fight_frame = world.get_frame()
			print("DIRECTOR ENCOUNTER ", JSON.stringify({"frame": fight_frame, "heroes": heroes, "x": midpoint.x, "y": midpoint.y}))
		health = [a.health, b.health]
		if fight_frame > 0 and world.get_frame() >= fight_frame + (900 if OS.get_cmdline_user_args().has("--until-death") else 120):
			break
	check(fight_frame > 0, "scripted opposing heroes actually deal damage")
	world.clear_game_data(false)
	if fight_frame < 0:
		world.queue_free()
		return
	check(world.prepare_replay(path).ok, "prepare scripted replay")
	check(world.start_replay({"logic_thread": false}).ok, "reconstruct scripted replay")
	var d = Director.new()
	d.set_enabled(true)
	var cam := camera()
	var picked := false
	var hashes_match := true
	for i in hashes.size():
		world.advance(0.2)
		hashes_match = hashes_match and world.get_state_hash() == hashes[i]
		var frame: int = world.get_frame()
		var events: Array = world.caster_events(maxi(0, frame - 75))
		tick(d, events, frame, 0.2, cam)
		if frame == fight_frame + 20:
			picked = not d.choice.is_empty() and d.choice.object in heroes and d.choice.position.distance_to(midpoint) < 400
			print("DIRECTOR CHOICE ", d.choice)
	check(picked, "director selects the scripted fight in the recorded replay")
	check(hashes_match and world.replay_status().mismatches == 0, "every replay hash matches with director active")
	var metadata := {"replay": path, "frame": fight_frame + 20, "heroes": heroes, "x": midpoint.x, "y": midpoint.y,
		"final_frame": world.get_frame(), "hashes": hashes.size()}
	var file := FileAccess.open(OS.get_user_data_dir().path_join(fixture + ".json"), FileAccess.WRITE)
	check(file != null, "write runtime encounter metadata")
	if file != null:
		file.store_string(JSON.stringify(metadata))
		file.close()
	print("DIRECTOR REPLAY ", JSON.stringify(metadata))
	world.clear_game_data(false)
	world.queue_free()


func run() -> void:
	policy()
	if OS.get_cmdline_user_args().has("--record-fight"):
		record_fight()
	print("DIRECTOR RESULT failures=", failures)
	quit(0 if failures == 0 else 1)
