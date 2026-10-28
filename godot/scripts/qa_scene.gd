## Lane QA-2: the feedback scenes (game.gd --auto --qa --qa-scene=<name>). A real skirmish through the real flow (main menu -> lobby -> load -> game), in
## which a logged test hook (GameWorld.create_object, printed as QA TEST HOOK) puts the units of one feedback item in front of the local base; the orders
## then go through the player's input paths (clicks on the world and on the Palantir's buttons, as qa_player.gd), and the scene is recorded: every rendered
## frame while it plays, as <screens>/qa2-<scene>/fNNNNN.jpg with the game time of each frame in times.txt (tools/qa/qa_scenes.py makes the clips).
## The motion invariants of qa_player.gd (treadmill, stuck) run through every scene. The end of the game is the usual QA flow (Esc, Exit, score screen).
##
## Scenes (the feedback items, workspace/rebuild/FEEDBACK-1.md / FEEDBACK-2.md):
##   melee     G1 / G2 / F4: two Gondor soldier hordes attack two Mordor orc hordes, then an orc pit (running in place, stuck behind the front line)
##   archers   G3: a Gondor archer horde shoots at an orc horde (the firing cycle)
##   troll     G5: a Mordor attack troll attacks a Gondor soldier horde (damage before the swing ends)
##   rohirrim  G8: Rohirrim shoot standing, then while riding past (missing fire animations, arrows out of the bodies)
##   charge    G4: Knights of Dol Amroth charge an orc horde (knock-back, slow-down)
##   garrison  G6: a Gondor archer horde garrisons a keep through a click and leaves through the Evacuate button (the common exit)
##   grond     G7 / FB-0002: Grond moves once its crew is drawn (the trolls push, the wheels turn)
##   wall      F7: a wall hub's Begin Wall Span button and a click (the span rises along its length)
##   wavefight the frame cost of a wave fight: an enemy marker's waves meet the local army between the bases; the logic frame, the waits on the logic
##             and the spawners' own per-frame work are timed (WAVEFIGHT_STEER=0: the spawners never re-order their waves)
##   waves     the map editor's wave spawners (scripts/wave_spawners.gd, not retail): one marker in the map border, one inside, both owned by the enemy's
##             start; their waves appear, come in from the border and close on the local base, the second wave follows the interval
##   produce   QA-2's treadmill finding: a barracks trains three soldier hordes through its buttons; every member's AI state is counted for 60 s
##             (members left in AI_FOLLOW_EXITPRODUCTION_PATH standing still with MOVING set)
extends "res://scripts/qa_player.gd"

var _rec_dir := ""
var _rec_n := 0
var _rec_t0 := 0
var _rec_frame0 := 0
var _rec_times: PackedStringArray = []
var _rec_scale := 1.0
var _enemy := ""
var _enemy_index := -1
var _spot := Vector2()
var _dir := Vector2(1, 0)


func _process(delta: float) -> void:
	super(delta)
	if _rec_dir.is_empty():
		return
	var image := get_viewport().get_texture().get_image()
	if image == null:
		return
	image.save_jpg("%s/f%05d.jpg" % [_rec_dir, _rec_n], 0.9)
	var game_ms := float(Time.get_ticks_msec() - _rec_t0) * _rec_scale
	_rec_times.append("%d %.1f %d" % [_rec_n, game_ms, _world.get_presented_frame()]) # lane RENDERWAIT-1: the frame shown (get_frame waits for the worker)
	_rec_n += 1


## records every rendered frame from now on, the logic at `scale` times speed (the capture of a frame takes longer than a 60 Hz frame)
func _record(label: String, scale := 0.5) -> void:
	_rec_stop()
	if _screens.is_empty() or DisplayServer.get_name() == "headless":
		return
	_rec_dir = "%s/qa2-%s-%s" % [_screens, _tag, label]
	DirAccess.make_dir_recursive_absolute(_rec_dir)
	_rec_n = 0
	_rec_t0 = Time.get_ticks_msec()
	_rec_frame0 = _world.get_frame()
	_rec_times = []
	_rec_scale = scale
	_world.set_time_scale(scale)
	print("QA scene recording %s from logic frame %d" % [_rec_dir, _rec_frame0])


func _rec_stop() -> void:
	if _rec_dir.is_empty():
		return
	var f := FileAccess.open(_rec_dir.path_join("times.txt"), FileAccess.WRITE)
	f.store_string("\n".join(_rec_times) + "\n")
	f.close()
	print("QA scene recorded %d frames in %s (logic frames %d .. %d)" % [_rec_n, _rec_dir, _rec_frame0, _world.get_frame()])
	_rec_dir = ""
	_world.set_time_scale(1.0)


func _hook_create(template: String, player_index: int, p: Vector2, angle: float) -> int:
	var id: int = _world.create_object(template, player_index, p.x, p.y, angle)
	print("QA TEST HOOK: create_object %s for player %d at (%.0f, %.0f) -> %d" % [template, player_index, p.x, p.y, id])
	if id <= 0:
		issue("scene_setup", template, "create_object failed: %s" % str(_world.get_command_errors() if _world.has_method("get_command_errors") else ""))
	return id


## a left click on the object as it is drawn (a horde: one of its soldiers); BFME's default mouse gives the selected units the context order
func _click_object(id: int) -> void:
	var target := id
	var members: Array = _world.get_object(id).get("members", [])
	if not members.is_empty():
		target = int(members[0])
	var pose: Dictionary = _world.get_render_pose(target)
	var o: Dictionary = _world.get_object(target)
	var at := Vector2(pose.x, pose.y) if pose.get("ok", false) else Vector2(o.get("x", 0.0), o.get("y", 0.0))
	await _click_world(at)


func _select_id(id: int) -> bool:
	var o: Dictionary = _world.get_object(id)
	if not o.get("ok", false):
		return false
	return await _select({"id": id, "template": o.template, "x": o.x, "y": o.y})


func _order_log_since(before: int) -> PackedStringArray:
	var log: PackedStringArray = _hud.get_message_log()
	return log.slice(before)


func _members(id: int) -> Array:
	return _world.get_object(id).get("members", [])


## the members' health and conditions in one line (what the scene's judge reads)
func _state_line(id: int) -> String:
	var o: Dictionary = _world.get_object(id)
	if not o.get("ok", false):
		return "gone"
	var hp := 0.0
	var conds := {}
	var mem: Array = o.get("members", [])
	for m in mem:
		var mo: Dictionary = _world.get_object(int(m))
		hp += float(mo.get("health", 0.0))
		for c in mo.get("conditions", []):
			conds[c] = conds.get(c, 0) + 1
	if mem.is_empty():
		hp = float(o.get("health", 0.0))
		for c in o.get("conditions", []):
			conds[c] = 1
	return "%s at (%.0f, %.0f): %d members, health %.0f, conditions %s" % [o.template, o.x, o.y, mem.size(), hp, JSON.stringify(conds)]


func _setup_players() -> void:
	for p in _world.get_economy().get("players", []):
		if p.playable and p.name != _local and _enemy.is_empty():
			_enemy = p.name
			_enemy_index = int(p.index)
	for o in _own_objects():
		if o.commandcenter:
			_base = Vector2(o.x, o.y)
	var enemy_base := Vector2()
	for o in _world.get_player_objects(_enemy):
		if o.commandcenter:
			enemy_base = Vector2(o.x, o.y)
	if enemy_base != Vector2():
		_dir = (enemy_base - _base).normalized()
	_spot = _base + _dir * 420.0


func scene(name: String) -> Dictionary:
	_end_frame += _world.get_frame()
	_setup_players()
	print("QA scene %s: %s (index %d) at (%.0f, %.0f) against %s (index %d); the scene at (%.0f, %.0f)" % [name, _local, _local_index(), _base.x, _base.y,
		_enemy, _enemy_index, _spot.x, _spot.y])
	match name:
		"melee":
			await _scene_melee()
		"archers":
			await _scene_ranged("GondorArcherHorde", "MordorFighterHorde", 300.0, "archers")
		"troll":
			await _scene_ranged("MordorAttackTroll", "GondorFighterHorde", 220.0, "troll")
		"rohirrim":
			await _scene_rohirrim()
		"charge":
			await _scene_charge()
		"garrison":
			await _scene_garrison()
		"grond":
			await _scene_grond()
		"wall":
			await _scene_wall()
		"produce":
			await _scene_produce()
		"waves":
			await _scene_waves()
		"wavefight":
			await _scene_wavefight()
		_:
			issue("scene_setup", name, "unknown scene")
	_rec_stop()
	return summary()


func _zoom_in(p: Vector2) -> void:
	await _look(p)
	# the mouse wheel over the world, as a player zooms (the camera's height follows the notches): down to the lowest height the camera allows
	var cam: Dictionary = _hud.get_camera()
	var h0: float = cam.get("height_above_ground", 0.0)
	var sign := 1
	for i in 12:
		_hud.inject_mouse_wheel(2 * sign, _hud.world_to_pixel(p))
		await _frames(8)
		var h: float = _hud.get_camera().get("height_above_ground", 0.0)
		if i == 0 and h > h0:
			sign = -1
		elif i > 0 and absf(h - h0) < 1.0:
			break
		h0 = h
	print("QA scene camera: %s" % JSON.stringify(_hud.get_camera()).left(300))
	await _look(p)


## lane COMBAT-4: `notches` wheel steps toward the ground over `p` (a large object stays in the picture)
func _zoom_some(p: Vector2, notches: int) -> void:
	await _look(p)
	var h0: float = _hud.get_camera().get("height_above_ground", 0.0)
	var sign := 1
	for i in notches:
		_hud.inject_mouse_wheel(2 * sign, _hud.world_to_pixel(p))
		await _frames(8)
		var h: float = _hud.get_camera().get("height_above_ground", 0.0)
		if i == 0 and h > h0:
			sign = -1
		h0 = h
	await _look(p)


## the camera follows the middle of the objects (no wait: a recording goes on)
func _follow(ids: Array) -> void:
	var c := Vector2()
	var n := 0
	for id in ids:
		var o: Dictionary = _world.get_object(int(id))
		if o.get("ok", false) and not o.get("destroyed", false):
			c += Vector2(o.x, o.y)
			n += 1
	if n > 0:
		_hud.camera_look_at(c / n)


## `seconds` of game time while the camera follows `ids`; every `every` seconds a state line of each of `ids`
func _watch_scene(label: String, ids: Array, seconds: float, every := 2.5, report: Array = []) -> void:
	var t := 0.0
	var next := every
	if report.is_empty():
		report = ids
	while t < seconds - 0.01:
		_follow(ids)
		await _wait_game(0.5)
		t += 0.5
		if t >= next - 0.01:
			next += every
			var parts := []
			for id in report:
				parts.append(_state_line(int(id)))
			print("QA scene %s t+%.1f s: %s" % [label, t, " | ".join(parts)])


func _wait_game(seconds: float) -> void:
	await _wait_logic(int(seconds * 5.0), false)


func _attack(attacker: int, target: int, label: String) -> bool:
	if not await _select_id(attacker):
		issue("scene_select_failed", label, "the %s could not be selected by a click" % _world.get_object(attacker).get("template", "?"))
		return false
	await _look(Vector2(_world.get_object(target).get("x", 0.0), _world.get_object(target).get("y", 0.0)))
	# a click on a soldier as it is drawn; a click that lands between the soldiers is a move (the player clicks again on another one)
	var picks: Array = _members(target)
	if picks.is_empty():
		picks = [target]
	var sent: PackedStringArray = []
	for k in mini(picks.size(), 6):
		var before: int = _hud.get_message_log().size()
		var pose: Dictionary = _world.get_render_pose(int(picks[k]))
		var mo: Dictionary = _world.get_object(int(picks[k]))
		var feet := Vector2(pose.x, pose.y) if pose.get("ok", false) else Vector2(mo.get("x", 0.0), mo.get("y", 0.0))
		# on the soldier's body, a little above its feet on the screen (where a player clicks)
		await _click_pixel(_hud.world_to_pixel(feet) - Vector2(0, 10 + 4 * (k % 3)))
		await _frames(3)
		sent = _order_log_since(before)
		for m in sent:
			if m.begins_with("MSG_DO_ATTACK_OBJECT"):
				print("QA scene %s order (click %d): %s" % [label, k + 1, str(sent)])
				if k > 0:
					issue("scene_click_retry", label, "the first click on a drawn enemy soldier was not an attack")
				return true
	issue("scene_order", label, "six clicks on the enemy's drawn soldiers sent %s, not an attack" % str(sent))
	return false


func _scene_melee() -> void:
	var side := Vector2(-_dir.y, _dir.x)
	var ang := _dir.angle()
	var own1 := _hook_create("GondorFighterHorde", _local_index(), _spot - side * 60.0, ang)
	var own2 := _hook_create("GondorFighterHorde", _local_index(), _spot + side * 60.0, ang)
	# the orcs within the soldiers' sight (a click on an enemy in the fog is a move: retail's rule, S-565 / QA-1 F4)
	var en1 := _hook_create("MordorFighterHorde", _enemy_index, _spot + _dir * 170.0 - side * 60.0, ang + PI)
	var en2 := _hook_create("MordorFighterHorde", _enemy_index, _spot + _dir * 170.0 + side * 60.0, ang + PI)
	await _wait_game(3.0)
	await _zoom_in(_spot + _dir * 120.0)
	_shot("melee-start")
	# both hordes at the first orc horde (two clicks: select one, attack; select the other, attack)
	await _attack(own1, en1, "melee")
	await _attack(own2, en1, "melee")
	await _record("melee", 0.5)
	await _watch_scene("melee", [own1, own2, en1, en2], 20.0)
	_rec_stop()
	_shot("melee-end")
	# a horde attacks a building (G1: the soldiers at a building's walls)
	var pit := _hook_create("MordorOrcPit", _enemy_index, _spot + _dir * 300.0 + side * 260.0, ang + PI)
	await _wait_game(1.0)
	var attacker := own2 if _world.get_object(own2).get("ok", false) else own1
	await _attack(attacker, pit, "melee-building")
	await _record("building", 0.5)
	await _watch_scene("building", [attacker, pit], 20.0)
	_rec_stop()
	_shot("building-end")


func _scene_ranged(own_t: String, enemy_t: String, dist: float, label: String) -> void:
	var ang := _dir.angle()
	var own := _hook_create(own_t, _local_index(), _spot, ang)
	var en := _hook_create(enemy_t, _enemy_index, _spot + _dir * dist, ang + PI)
	await _wait_game(3.0)
	await _zoom_in(_spot + _dir * dist * 0.5)
	_shot("%s-start" % label)
	# recorded from before the order: the first contact can come while the player is still clicking
	await _record(label, 0.5)
	await _attack(own, en, label)
	# the camera on the attacker (its firing / swing cycle is the item), the target in the state lines
	await _watch_scene(label, [own], 16.0, 2.0, [own, en])
	_rec_stop()
	_shot("%s-end" % label)


func _scene_rohirrim() -> void:
	var ang := _dir.angle()
	var side := Vector2(-_dir.y, _dir.x)
	var own := _hook_create("RohanRohirrimHorde", _local_index(), _spot, ang)
	var en := _hook_create("MordorFighterHorde", _enemy_index, _spot + _dir * 330.0, ang + PI)
	await _wait_game(3.0)
	await _zoom_in(_spot + _dir * 160.0)
	# the bows: the horde's weapon-set toggle button (Command_ToggleRohirrimWeapon), as a player switches before shooting
	if await _select_id(own):
		await _frames(5)
		var toggled := false
		var cmds := []
		for b in _hud.get_command_buttons():
			cmds.append("%s %s" % [b.name, b.command])
			if not toggled and b.name.find("ToggleRohirrimWeapon") >= 0:
				var before: int = _hud.get_message_log().size()
				await _press(b)
				await _frames(4)
				print("QA scene rohirrim toggle: %s" % str(_order_log_since(before)))
				toggled = true
		print("QA scene rohirrim buttons: %s" % str(cmds))
		if not toggled:
			issue("scene_order", "rohirrim", "the Rohirrim's command bar shows no weapon toggle: %s" % str(cmds))
		await _wait_game(2.0)
		print("QA scene rohirrim after the toggle: %s" % _state_line(own))
	await _attack(own, en, "rohirrim")
	await _record("rohirrim-stand", 0.5)
	await _watch_scene("rohirrim stand", [own], 10.0, 2.0, [own, en])
	_rec_stop()
	# riding past: a move order across the enemy's front (retail's mounted archers fire on the move)
	var before: int = _hud.get_message_log().size()
	var pass_to := _spot + _dir * 200.0 + side * 400.0
	await _look(pass_to)
	await _click_world(pass_to)
	await _frames(3)
	print("QA scene rohirrim move order: %s" % str(_order_log_since(before)))
	await _record("rohirrim-move", 0.5)
	await _watch_scene("rohirrim move", [own], 10.0, 2.0)
	_rec_stop()


## G4: the knights attack-move (A, then a click) at an orc horde 650 away (in the fog: an attack-move fights its way there) and ride into it
func _scene_charge() -> void:
	var ang := _dir.angle()
	var own := _hook_create("GondorKnightsofDolHorde", _local_index(), _spot, ang)
	var en := _hook_create("MordorFighterHorde", _enemy_index, _spot + _dir * 650.0, ang + PI)
	await _wait_game(3.0)
	await _zoom_in(_spot + _dir * 300.0)
	if not await _select_id(own):
		issue("scene_select_failed", "charge", "the knights could not be selected")
		return
	var before: int = _hud.get_message_log().size()
	var target := _spot + _dir * 650.0
	await _look(target)
	await _key(DIK_A)
	await _click_world(target)
	await _frames(3)
	print("QA scene charge order: %s" % str(_order_log_since(before)))
	await _record("charge", 0.5)
	await _watch_scene("charge", [own], 18.0, 2.0, [own, en])
	_rec_stop()
	_shot("charge-end")


func _scene_garrison() -> void:
	var ang := _dir.angle()
	var keep := _hook_create("GondorKeep", _local_index(), _spot + _dir * 150.0, 0.0)
	var archers := _hook_create("GondorArcherHorde", _local_index(), _spot - _dir * 120.0, ang)
	await _wait_game(3.0)
	await _zoom_in(_spot)
	_shot("garrison-start")
	if not await _select_id(archers):
		issue("scene_select_failed", "garrison", "the archers could not be selected")
		return
	var before: int = _hud.get_message_log().size()
	await _click_object(keep)
	await _frames(3)
	var sent := _order_log_since(before)
	print("QA scene garrison order: %s" % str(sent))
	var inside := false
	for i in 60:
		await _wait_game(0.5)
		var g: Dictionary = _world.get_garrison(keep)
		if g.get("count", 0) >= 1 and g.get("entering", 1) == 0 and g.get("members", 0) > 0 and g.get("members_hidden", 0) == g.get("members", 0):
			inside = true
			break
	print("QA scene garrison: inside %s, %s" % [str(inside), JSON.stringify(_world.get_garrison(keep))])
	if not inside:
		issue("scene_order", "garrison", "a click on the keep with the archers selected did not garrison them in 30 s (sent %s)" % str(sent))
		return
	await _deselect()
	if not await _select_id(keep):
		issue("scene_select_failed", "garrison", "the keep could not be selected")
		return
	await _frames(5)
	# Evacuate (the whole garrison); else the first rider's exit button
	var evac := {}
	var buttons: Array = _hud.get_command_buttons()
	print("QA scene garrison keep buttons: %s" % str(buttons.map(func(x): return "%s %s" % [x.name, x.command])))
	for b in buttons:
		if b.command == "EVACUATE" and evac.is_empty():
			evac = b
	for b in buttons:
		if b.command == "EXIT_CONTAINER" and evac.is_empty():
			evac = b
	if evac.is_empty():
		issue("scene_order", "garrison", "the selected keep shows no Evacuate button: %s" % str(_hud.get_command_buttons().map(func(x): return x.command)))
		return
	await _look(_spot + _dir * 60.0)
	await _record("garrison-exit", 0.5)
	before = _hud.get_message_log().size()
	await _press(evac)
	await _frames(3)
	print("QA scene garrison evacuate: %s" % str(_order_log_since(before)))
	for i in 6:
		await _wait_game(1.5)
		print("QA scene garrison exit t+%.1f s: %s" % [1.5 * (i + 1), _state_line(archers)])
	_rec_stop()
	_shot("garrison-end")


func _scene_grond() -> void:
	var ang := _dir.angle()
	var grond := _hook_create("MordorGrond", _local_index(), _spot, ang)
	# lane COMBAT-4: the crew trolls have Model None while JUST_BUILT (BuildFadeInOnCreateTime 16 s): wait them out, close enough to see the push and the wheels
	await _wait_game(18.0)
	await _zoom_some(_spot, 3)
	_shot("grond-start")
	print("QA scene grond: %s" % JSON.stringify(_world.get_object(grond)).left(600))
	if not await _select_id(grond):
		issue("scene_select_failed", "grond", "Grond could not be selected")
		return
	var to := _spot + _dir * 350.0
	var before: int = _hud.get_message_log().size()
	await _click_world(to)
	await _frames(3)
	print("QA scene grond move order: %s" % str(_order_log_since(before)))
	await _record("grond", 0.5)
	await _watch_scene("grond", [grond], 12.0, 2.0)
	_rec_stop()


func _scene_wall() -> void:
	var ang := _dir.angle()
	var side := Vector2(-_dir.y, _dir.x)
	var hub := _hook_create("MenWallHubSmall", _local_index(), _spot, ang)
	await _wait_game(2.0)
	await _zoom_in(_spot + side * 150.0)
	if not await _select_id(hub):
		issue("scene_select_failed", "wall", "the wall hub could not be selected")
		return
	await _frames(5)
	var span := {}
	var commands := []
	for b in _hud.get_command_buttons():
		commands.append("%s %s" % [b.name, b.command])
		# lane QA2-FIX: the line build button (DOZER_CONSTRUCT); since HUD-4 the hub's side bar also shows Command_CancelWallSpan, which the old
		# name match ("WallSpan", last one wins) pressed instead
		if b.command == "DOZER_CONSTRUCT" and b.name.find("WallSpan") >= 0:
			span = b
	print("QA scene wall hub buttons: %s" % str(commands))
	if span.is_empty():
		issue("scene_order", "wall", "the selected wall hub shows no wall span button: %s" % str(commands))
		return
	var before: int = _hud.get_message_log().size()
	if not await _press(span):
		return
	await _frames(4)
	var after_press: Dictionary = _hud.get_state()
	print("QA scene wall after the press: placement %s, messages %s, unported presses %s" % [JSON.stringify(_hud.get_placement()), str(after_press.get("messages", [])),
		str(_hud.get_report().get("unported_presses", {}))])
	var end := _spot + side * 320.0
	await _look(_spot + side * 160.0)
	_hud.inject_mouse_move(_hud.world_to_pixel(end))
	await _frames(4)
	print("QA scene wall placement: %s" % JSON.stringify(_hud.get_placement()))
	await _record("wall", 0.5)
	await _click_world(end)
	await _frames(3)
	print("QA scene wall order: %s" % str(_order_log_since(before)))
	for i in 10:
		await _wait_game(3.0)
		var pieces := 0
		for o in _own_objects():
			if o.structure and Vector2(o.x, o.y).distance_to(_spot + side * 160.0) < 200.0:
				pieces += 1
		print("QA scene wall t+%.1f s: %d structures along the span" % [3.0 * (i + 1), pieces])
	_rec_stop()
	_shot("wall-end")


func _scene_produce() -> void:
	var ang := _dir.angle()
	var barracks := _hook_create("GondorBarracks", _local_index(), _spot, ang)
	_world.give_money(_local_index(), 5000)
	print("QA TEST HOOK: give_money 5000 to player %d" % _local_index())
	await _wait_game(2.0)
	await _zoom_in(_spot + _dir * 120.0)
	if not await _select_id(barracks):
		issue("scene_select_failed", "produce", "the barracks could not be selected")
		return
	await _frames(5)
	var build := {}
	for b in _hud.get_command_buttons():
		if b.command == "UNIT_BUILD" and b.get("template", "") == "GondorFighterHorde":
			build = b
	if build.is_empty():
		for b in _hud.get_command_buttons():
			if b.command == "UNIT_BUILD" and build.is_empty():
				build = b
	if build.is_empty():
		issue("scene_order", "produce", "the barracks shows no unit button")
		return
	for k in 3:
		var before: int = _hud.get_message_log().size()
		await _press(build)
		await _frames(4)
		print("QA scene produce order %d: %s" % [k + 1, str(_order_log_since(before))])
	await _deselect()
	await _look(_spot + _dir * 150.0)
	await _record("produce", 0.5)
	var last := {}
	for i in 60:
		await _wait_game(1.0)
		var states := {}
		var still_moving := 0
		for o in _own_objects():
			if o.structure:
				continue
			var full: Dictionary = _world.get_object(o.id)
			if not full.get("members", []).is_empty():
				continue
			var key := "%d%s" % [int(full.get("ai_state", -1)), "M" if full.get("conditions", []).has("MOVING") else ""]
			states[key] = states.get(key, 0) + 1
			var pos := Vector2(o.x, o.y)
			if last.has(o.id) and pos.distance_to(last[o.id]) < MOTION_STILL and full.get("conditions", []).has("MOVING"):
				still_moving += 1
			last[o.id] = pos
		if i % 5 == 4:
			print("QA scene produce t+%d s: AI state (M = MOVING set) -> units %s; standing still with MOVING %d" % [i + 1, JSON.stringify(states), still_moving])
		if i == 20:
			_rec_stop()
	_rec_stop()
	_shot("produce-end")


func _scene_waves() -> void:
	var spawners: Node = preload("res://scripts/wave_spawners.gd").new()
	spawners.world = _world
	add_child(spawners)
	var ctx: Dictionary = spawners.context()
	print("QA scene waves context: %s" % JSON.stringify(ctx))
	var start := 0
	for entry in ctx.get("starts", []):
		if int(entry.player) == _enemy_index:
			start = int(entry.start)
	if start == 0:
		issue("scene_setup", "waves", "the enemy has no start position")
		return
	var at: Dictionary = spawners.start_entry(start)
	var home := Vector2(at.x, at.y)
	var area: Array = ctx.playable
	var full: Array = ctx.map
	# the border point beyond the playable edge nearest the enemy's start
	var edges := [[home.x - area[0], Vector2(area[0] - 120.0, home.y)], [area[2] - home.x, Vector2(area[2] + 120.0, home.y)],
		[home.y - area[1], Vector2(home.x, area[1] - 120.0)], [area[3] - home.y, Vector2(home.x, area[3] + 120.0)]]
	edges.sort_custom(func(a, b): return a[0] < b[0])
	var outside: Vector2 = edges[0][1]
	if not Rect2(full[0], full[1], full[2] - full[0], full[3] - full[1]).has_point(outside):
		issue("scene_setup", "waves", "the map has no border at %s (map %s, playable %s)" % [str(outside), str(full), str(area)])
		return
	var inside := home + (_base - home).normalized() * 250.0
	var units := [{"template": "MordorFighterHorde", "count": 2}]
	spawners.set_markers([
		{"id": 1, "x": outside.x, "y": outside.y, "start": start, "delay": 1.0, "interval": 40.0, "waves": 2, "units": units},
		{"id": 2, "x": inside.x, "y": inside.y, "start": start, "delay": 1.0, "interval": 40.0, "waves": 1, "units": units}])
	for marker in spawners.markers:
		print("QA scene waves marker %d at (%.0f, %.0f) outside %s: %s" % [marker.id, marker.x, marker.y, str(spawners.is_outside(Vector2(marker.x, marker.y))), spawners.describe(marker)])
	var log: Array = []
	spawners.wave_spawned.connect(func(id, created, message): log.append([id, created, message]); print("QA scene waves spawned: marker %d, %d objects: %s" % [id, created, message]))
	await _wait_game(3.0)
	if log.size() != 2 or log[0][1] != 2 or log[1][1] != 2:
		issue("scene_waves", "first", "expected two waves of two hordes, got %s" % str(log))
		return
	var first := {}
	for group in spawners._groups:
		var p := _group_centre(group.ids)
		first[group.marker] = p
		print("QA scene waves group of marker %d: %s at (%.0f, %.0f), %.0f from the base" % [group.marker, str(group.ids), p.x, p.y, p.distance_to(_base)])
	for t in 8:
		await _wait_game(5.0)
		for group in spawners._groups:
			var p := _group_centre(group.ids)
			print("QA scene waves t+%d s marker %d: (%.0f, %.0f), %.0f from the base, outside %s, target %d" % [(t + 1) * 5, group.marker, p.x, p.y, p.distance_to(_base),
				str(spawners.is_outside(p)), group.target])
	for group in spawners._groups.slice(0, 2):
		var p := _group_centre(group.ids)
		if spawners.is_outside(p):
			issue("scene_waves", "entry", "marker %d's wave is still outside the playable area at (%.0f, %.0f)" % [group.marker, p.x, p.y])
		elif p.distance_to(_base) > Vector2(first[group.marker]).distance_to(_base) - 300.0:
			issue("scene_waves", "march", "marker %d's wave did not close on the base: %.0f -> %.0f" % [group.marker, Vector2(first[group.marker]).distance_to(_base), p.distance_to(_base)])
	await _wait_game(5.0)
	if log.size() != 3 or log[2][0] != 1:
		issue("scene_waves", "interval", "expected the border marker's second wave after 40 s: %s" % str(log))
	print("QA scene waves log: %s" % str(log))
	await _waves_editor(home)
	for t in 8:
		await _wait_game(5.0)
		for group in spawners._groups:
			var p := _group_centre(group.ids)
			var o: Dictionary = _world.get_object(int(group.ids[0]))
			print("QA scene waves t+%d s marker %d: (%.0f, %.0f), %.0f from the base, state %s goal %s victim %s" % [45 + t * 5, group.marker, p.x, p.y, p.distance_to(_base),
				str(o.get("ai_state", "")), str(o.get("ai_goal", "")), str(o.get("ai_victim", o.get("ai_goal_object", "")))])


## the editor's Waves tool: a click places a spawner owned by the nearest other start, units are added from the library, the edit file keeps it
func _waves_editor(home: Vector2) -> void:
	_game._open_map_editor()
	await _frames(3)
	var editor: CanvasLayer = _game._map_editor
	if not is_instance_valid(editor) or not editor.active:
		issue("scene_waves", "editor", "the map editor did not open")
		return
	editor.tool.select(11)
	editor._update_controls()
	await _look(home)
	editor.cursor = _hud.world_to_pixel(home + Vector2(0, 150))
	editor._wave_press()
	var markers: Array = editor.spawners.markers
	if markers.size() != 1 or int(markers[0].start) != int(editor.spawners.nearest_start(home)):
		issue("scene_waves", "editor", "a click did not place a spawner owned by the nearest start: %s" % str(markers))
		return
	# the + search: hordes and heroes only (a horde's soldier and a building never), the quantity is the count added
	if not editor.wave_picker.visible:
		issue("scene_waves", "editor", "placing a spawner did not open the + search")
		editor._toggle_wave_picker()
	var found := {}
	for query in ["fighter", "aragorn", "barracks"]:
		editor.wave_search.text = query
		editor._filter_wave_units()
		var names: Array = []
		for i in editor.wave_results.item_count:
			names.append(str(editor.wave_results.get_item_metadata(i)))
		found[query] = names
		print("QA scene waves search '%s': %d results %s" % [query, names.size(), str(names.slice(0, 8))])
	if "MordorFighterHorde" not in found.fighter or "MordorFighter" in found.fighter or "GondorFighter" in found.fighter:
		issue("scene_waves", "search", "'fighter' should list the fighter hordes, not their soldiers: %s" % str(found.fighter))
	if found.aragorn.is_empty() or not found.barracks.is_empty():
		issue("scene_waves", "search", "heroes should be listed and buildings not: aragorn %s, barracks %s" % [str(found.aragorn), str(found.barracks)])
	editor.wave_search.text = "MordorFighterHorde"
	editor._filter_wave_units()
	for i in editor.wave_results.item_count:
		if str(editor.wave_results.get_item_metadata(i)) == "MordorFighterHorde":
			editor.wave_results.select(i)
	editor.wave_add_count.value = 3
	editor._wave_add_unit()
	if editor.spawners.markers[0].units != [{"template": "MordorFighterHorde", "count": 3}]:
		issue("scene_waves", "search", "the quantity did not reach the wave: %s" % str(editor.spawners.markers[0].units))
	print("QA scene waves editor: %s (%s); status %s" % [str(editor.spawners.markers), editor.spawners.describe(editor.spawners.markers[0]), editor.status.text])
	editor.save_name.text = "qa-waves"
	editor._save()
	editor.overlay.clear()
	editor.spawners.set_markers([])
	editor._load()
	var loaded: Array = editor.spawners.markers
	if loaded.size() != 1 or loaded[0].units.is_empty() or str(loaded[0].units[0].template) != "MordorFighterHorde":
		issue("scene_waves", "editor", "the saved spawner did not load back: %s (%s)" % [str(loaded), editor.status.text])
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://map_edits/qa-waves.json"))
	editor.set_active(false)


func _group_centre(ids: Array) -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	for id in ids:
		var o: Dictionary = _world.get_object(int(id))
		if o.get("ok", false):
			sum += Vector2(o.x, o.y)
			n += 1
	return sum / maxf(1.0, float(n))


func _scene_wavefight() -> void:
	var steer := OS.get_environment("WAVEFIGHT_STEER") != "0"
	var spawners: Node = preload("res://scripts/wave_spawners.gd").new()
	spawners.world = _world
	add_child(spawners)
	spawners.set_process(false) # driven and timed below
	var start := 0
	for entry in spawners.context().get("starts", []):
		if int(entry.player) == _enemy_index:
			start = int(entry.start)
	var at: Dictionary = spawners.start_entry(start)
	if at.is_empty():
		issue("scene_setup", "wavefight", "the enemy has no start position")
		return
	var home := Vector2(at.x, at.y)
	var toward := (_base - home).normalized()
	var mid := (home + _base) * 0.5
	spawners.set_markers([{"id": 1, "x": home.x + toward.x * 300.0, "y": home.y + toward.y * 300.0, "start": start, "delay": 1.0, "interval": 25.0, "waves": 3,
		"units": [{"template": "MordorFighterHorde", "count": 6}, {"template": "MordorArcherHorde", "count": 2}]}])
	var side := toward.orthogonal()
	var army: Array = []
	for i in 12:
		var p := _base + toward * 300.0 + side * (float(i) - 5.5) * 70.0
		army.append(_hook_create("GondorFighterHorde", _local_index(), p, toward.angle()))
	await _wait_game(2.0)
	_world.order_move(army, mid.x, mid.y, {"type": "attack"})
	var end: int = _world.get_presented_frame() + 90 * 5 # not get_frame: it waits for the worker, a wait the scene would cause itself
	var spawner_us: Array[float] = []
	var worker_ms: Array[float] = []
	var frame_ms: Array[float] = []
	var waits0: float = float(_world.get_frame_timings().get("logic_wait_ms", 0.0))
	var last := Time.get_ticks_usec()
	var last_logic := -1
	var slow: Array = []
	_world.set_performance_enabled(true)
	var counters: Dictionary = _world.get_performance_counters()
	var slow_parts := {}
	while _world.get_presented_frame() < end:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		frame_ms.append((now - last) / 1000.0)
		last = now
		if not steer:
			spawners._next_steer = 1 << 30
		var t0 := Time.get_ticks_usec()
		spawners._process(0.0)
		spawner_us.append(float(Time.get_ticks_usec() - t0))
		var timings: Dictionary = _world.get_frame_timings()
		var frame: int = _world.get_presented_frame()
		if frame != last_logic:
			last_logic = frame
			var ms := float(timings.get("worker_frame_ms", 0.0))
			worker_ms.append(ms)
			var now_counters: Dictionary = _world.get_performance_counters()
			if ms > 60.0:
				for part in now_counters:
					slow_parts[part] = float(slow_parts.get(part, 0.0)) + float(now_counters[part].ms) - float(counters[part].ms)
			counters = now_counters
			if ms > 60.0 and slow.size() < 40:
				slow.append("f%d %.0f ms (%d objects)" % [frame, ms, int(timings.get("presented_objects", 0))])
	var waits: float = float(_world.get_frame_timings().get("logic_wait_ms", 0.0)) - waits0
	var alive := 0
	for group in spawners._groups:
		alive += group.ids.size()
	print("QA scene wavefight steer %s: %d render frames, frame ms %s; logic frame ms %s; spawner us %s; render waits on the logic %.0f ms; %d wave objects alive" % [
		str(steer), frame_ms.size(), _spread(frame_ms), _spread(worker_ms), _spread(spawner_us), waits, alive])
	print("QA scene wavefight slow logic frames: %s" % ", ".join(slow))
	print("QA scene wavefight time in the slow frames by part (ms): %s" % JSON.stringify(slow_parts))
	_world.set_performance_enabled(false)


## avg / p99 / max of a sample
func _spread(values: Array[float]) -> String:
	if values.is_empty():
		return "-"
	var sorted := values.duplicate()
	sorted.sort()
	var sum := 0.0
	for v in values:
		sum += v
	return "avg %.1f p99 %.1f max %.1f" % [sum / values.size(), sorted[int(values.size() * 0.99)], sorted[-1]]
