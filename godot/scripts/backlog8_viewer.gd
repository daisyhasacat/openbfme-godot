## BACKLOG-8 proof viewer: the tower guards' line and porcupine poses (the move hub's PORCUPINE, RW 0x875121) and the same scripted force-move of four
## hordes without and with the formation march (AIData UseFormations, RW 0x774897 -> 0x7747DE / 0x77348C). For the lane's before / after screenshots.
##
##   godot --path godot res://scenes/backlog8_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-pose-line.png, -pose-porcupine.png, -march-plain-fNN.png, -march-formation-fNN.png there
##   --prefix=<name>   the screenshot prefix (default backlog8)
##   --speed=<f>       the logic speed (default 1.0)
##   --only=<part>     pose or march (default: both)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "backlog8"
var _speed := 1.0
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--only="):
			_only = arg.substr(7)
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(fs, _map_name, {"markers": false, "fog": false})
	if root == null:
		_fail("build_map failed: %s" % [builder.get_report().errors])
		return
	add_child(root)
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(_map_name, {"slots": slots, "seed": 4711, "test_hooks": true})
	if not rep.ok:
		_fail("load_map failed: %s" % [rep.errors.slice(0, 5)])
		return
	root.add_child(_world)
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and _local < 0:
			_local = idx
		if "Player_2" in str(pl) and _enemy < 0:
			_enemy = idx
		idx += 1
	_world.set_logic_thread(false)
	_world.set_shroud_drawn(false)
	_world.set_auto_advance(true)
	_world.set_time_scale(_speed)
	_world.set_local_player(_local)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_font_size_override("font_size", 22)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 5)
	layer.add_child(_label)
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("BACKLOG8VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _aim(focus: Vector3, pos: Vector2, height: float) -> void:
	var g: float = _world.get_ground_height(pos.x, pos.y)
	_camera.position = _sage_to_godot(Vector3(pos.x, pos.y, g + height))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


## an open spot near the middle of the map's trees: no tree within 260 of it
func _pick_spot() -> Vector2:
	var trees: PackedVector3Array = _world.get_terrain_trees(-1)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for t in trees:
		lo = Vector2(minf(lo.x, t.x), minf(lo.y, t.y))
		hi = Vector2(maxf(hi.x, t.x), maxf(hi.y, t.y))
	if trees.is_empty():
		return Vector2(2000, 2000)
	var centre := (lo + hi) * 0.5
	var best := centre
	var best_d := 1e18
	var step := 100.0
	for gx in range(-20, 21):
		for gy in range(-20, 21):
			var p := centre + Vector2(gx, gy) * step
			var ok := true
			for t in trees:
				if Vector2(t.x, t.y).distance_to(p) < 260.0:
					ok = false
					break
			if ok and p.distance_squared_to(centre) < best_d:
				best = p
				best_d = p.distance_squared_to(centre)
	return best


func _wait(frames: int) -> void:
	for k in frames:
		await get_tree().process_frame


func _health(horde: int) -> float:
	var h := 0.0
	var o: Dictionary = _world.get_object(horde)
	for m in o.get("members", []):
		var mo: Dictionary = _world.get_object(m)
		h += mo.get("health", 0.0)
	return h


func _pos(id: int) -> Vector3:
	var o: Dictionary = _world.get_object(id)
	return Vector3(o.get("x", 0.0), o.get("y", 0.0), o.get("z", 0.0))


func _follow(id: int, back: Vector2, height: float) -> void:
	var p := _pos(id)
	_aim(Vector3(p.x, p.y, p.z + 30.0), Vector2(p.x + back.x, p.y + back.y), height)




var _only := ""


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("BACKLOG8VIEW SAVED ", path)


func _clear(ids: Array) -> void:
	for id in ids:
		if not _world.get_object(id).get("destroyed", true):
			_world.destroy_object(id)
	await _wait(10)


## the tower guards on the spot: their line formation, then MSG_HORDE_TOGGLE_FORMATION: the members walk to the porcupine slots and take the pose there
func _pose(spot: Vector2) -> void:
	var horde: int = _world.create_object("GondorTowerShieldGuardHorde", _local, spot.x, spot.y, 0.0)
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	_aim(Vector3(spot.x, spot.y, g0 + 10.0), Vector2(spot.x - 120.0, spot.y - 150.0), 90.0)
	_label.text = "Tower guards in their line formation (frame %d)" % _world.get_frame()
	await _wait(60)
	_label.text = "Tower guards in their line formation (frame %d)" % _world.get_frame()
	await _wait(2)
	_save("pose-line")
	print("BACKLOG8VIEW toggle ", JSON.stringify(_world.order_toggle_formation(horde)))
	var f0: int = _world.get_frame()
	while _world.get_frame() < f0 + 60:
		await get_tree().process_frame
		_label.text = "Porcupine formation: members on their slots take PORCUPINE (frame %d)" % _world.get_frame()
	_save("pose-porcupine")
	await _clear([horde])


## four hordes west of the spot, force-moved 900 east: without UseFormations each goes its own way (the ZH group move), with it the group marches
func _march(spot: Vector2, formations: bool) -> void:
	print("BACKLOG8VIEW use_formations ", JSON.stringify(_world.debug_set_use_formations(formations)))
	var ids := []
	var templates := ["GondorFighterHorde", "GondorFighterHorde", "GondorArcherHorde", "GondorArcherHorde"]
	for i in templates.size():
		ids.append(_world.create_object(templates[i], _local, spot.x - 450.0 + 120.0 * floorf(i / 2.0), spot.y - 90.0 + 180.0 * float(i % 2), 0.0))
	await _wait(30)
	print("BACKLOG8VIEW order ", JSON.stringify(_world.order_move(ids, spot.x + 450.0, spot.y, {"type": "force"})))
	var tag := "formation" if formations else "plain"
	var f0: int = _world.get_frame()
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	while _world.get_frame() < f0 + 160:
		await get_tree().process_frame
		var c := Vector3.ZERO
		for id in ids:
			c += _pos(id)
		c /= float(ids.size())
		_aim(Vector3(c.x + 80.0, c.y, g0 + 10.0), Vector2(c.x - 200.0, c.y - 520.0), 380.0)
		var k: int = _world.get_frame() - f0
		_label.text = "Force-move of four hordes, UseFormations %s (frame %d after the order)" % ["on: the formation march" if formations else "off: the ZH group move", k]
		if k == 40 or k == 100 or k == 159:
			await get_tree().process_frame
			_save("march-%s-f%03d" % [tag, k])
	await _clear(ids)


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	print("BACKLOG8VIEW spot (%.0f, %.0f) local %d" % [spot.x, spot.y, _local])
	if _only.is_empty() or _only == "pose":
		await _pose(spot)
	if _only.is_empty() or _only == "march":
		await _march(spot, false)
		await _march(spot, true)
	print("BACKLOG8VIEW DONE")
	get_tree().quit(0)
