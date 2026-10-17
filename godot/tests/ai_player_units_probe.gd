extends SceneTree
## Player-unit behaviour probe: a human player that gives no orders against a computer player. Every 30 seconds of game time it counts the human's units by
## AI state and reports the idle ones with an enemy unit nearby (auto-acquire should leave none for long). Not a pass/fail test.
## Args (after --): factions=<human,ai> difficulty=<2..5> minutes=<n> seed=<n> near=<distance> walls=<0|1>
var _world: Node3D
var _args := {"factions": "3,8", "difficulty": "4", "minutes": "12", "seed": "4242", "near": "300", "walls": "1"}


func _initialize() -> void:
	call_deferred("_main")


func _main() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			_args[kv[0]] = kv[1]
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		print("UNITS FAIL mount"); quit(1); return
	_world = ClassDB.instantiate("GameWorld")
	root.add_child(_world)
	_world.setup(fs)
	var factions: PackedStringArray = String(_args.factions).split(",")
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Human", "accepted": true, "color": 0, "start_pos": -1, "player_template": int(factions[0]), "team": 0}
	slots[1] = {"state": int(_args.difficulty), "name": "AI", "accepted": true, "color": 1, "start_pos": -1, "player_template": int(factions[1]), "team": 1}
	var prep: Dictionary = _world.prepare_new_game({"mode": "skirmish", "difficulty": 1, "rank_points": 0, "map": "maps/map mp evendim/map mp evendim.map",
		"map_crc": 0, "map_size": 0, "map_mask": 0, "seed": int(_args.seed), "starting_cash": -1, "slots": slots})
	if not prep.ok or not _world.start_new_game({} if _args.walls == "1" else {"wall_rebalance": false}).ok:
		print("UNITS FAIL start"); quit(1); return
	var r: Dictionary = _world.execute_lua("return bfme.players.localPlayer.id")
	var human := int(String(r.get("output", "-1")).strip_edges())
	print("UNITS human player ", human)
	# an idle army for the human: hordes in front of its start, given no orders (Men's templates; other factions: army=<t1,t2,...>)
	var start: String = String(_world.execute_lua("local s=bfme.players.localPlayer:getStartPosition() return s.x..','..s.y").get("output", "0,0")).strip_edges()
	var sp := start.split(",")
	var at := Vector2(float(sp[0]), float(sp[1]))
	var army: PackedStringArray = String(_args.get("army", "GondorFighterHorde,GondorFighterHorde,GondorArcherHorde,GondorArcherHorde,GondorTowerShieldGuardHorde,RohanEoredHorde")).split(",")
	var made := 0
	for i in army.size():
		var p := at + Vector2(cos(i * 0.9) * 450.0, sin(i * 0.9) * 450.0)
		var res = _world.create_object(army[i], human, p.x, p.y, 0.0)
		made += 1 if (res is Dictionary and res.get("ok", false)) or (res is int and res > 0) else 0
	print("UNITS spawned %d of %d at %s" % [made, army.size(), at])
	var near := float(_args.near)
	var raid: bool = _args.get("raid", "0") == "1"
	var ai_start := Vector2.ZERO
	if raid:
		var rs: String = String(_world.execute_lua("for _,p in ipairs(bfme.ai.list()) do local s=p:getStartPosition() return s.x..','..s.y end").get("output", "0,0")).strip_edges()
		ai_start = Vector2(float(rs.split(",")[0]), float(rs.split(",")[1]))
	for step in int(_args.minutes) * 2:
		if raid and step == 1:
			var ids := []
			for oid in _world.get_object_ids():
				var o: Dictionary = _world.get_object(oid)
				if o.get("ok", false) and int(o.get("player_id", -1)) == human and str(o.template).ends_with("Horde"):
					ids.append(oid)
			print("UNITS raid: %d hordes attack-move to the AI base %s: %s" % [ids.size(), ai_start, _world.order_move(ids, ai_start.x, ai_start.y, {"type": "attack"})])
		for f in 150:
			_world.advance(0.2)
		if raid:
			var tactics := []
			for t in _world.get_ai_report():
				tactics.append("%s%s" % [t.kind, "*" if t.started else ""])
			var home := 0
			for oid in _world.get_object_ids():
				var o: Dictionary = _world.get_object(oid)
				if o.get("ok", false) and str(o.template).ends_with("Horde") and int(o.get("player_id", -1)) != human and str(o.owner).begins_with("Player_"):
					home += 1 if Vector2(o.x, o.y).distance_to(ai_start) < 1200 else 0
			print("UNITS   AI tactics %s; AI hordes within 1200 of its base %d" % [str(tactics), home])
		var states := {}
		var idle_near := []
		var enemies := []
		var mine := []
		for oid in _world.get_object_ids():
			var o: Dictionary = _world.get_object(oid)
			if not o.get("ok", false) or o.get("structure", false) or not o.has("ai_state_id") or not o.get("selectable", false):
				continue
			if int(o.player_id) == human:
				mine.append(o)
			elif str(o.owner).begins_with("Player_"):
				enemies.append(o)
		for o in mine:
			var st := int(o.ai_state_id)
			states[st] = int(states.get(st, 0)) + 1
			if st == 0:
				for e in enemies:
					if Vector2(o.x, o.y).distance_to(Vector2(e.x, e.y)) < near:
						idle_near.append("%s#%d (enemy %s at %.0f)" % [o.template, int(o.id), e.template, Vector2(o.x, o.y).distance_to(Vector2(e.x, e.y))])
						break
		print("UNITS t=%ds human units %d states %s idle-near-enemy %d %s" % [(step + 1) * 30, mine.size(), str(states), idle_near.size(), str(idle_near.slice(0, 4))])
		if _args.get("hordes", "0") == "1":
			for o in mine:
				if str(o.template).ends_with("Horde"):
					var best := 1e9
					var what := ""
					for e in enemies:
						var d := Vector2(o.x, o.y).distance_to(Vector2(e.x, e.y))
						if d < best:
							best = d
							what = e.template
					print("UNITS   horde %s#%d state %s victim %s melee %s members %d nearest enemy %s at %.0f" % [o.template, int(o.id), o.ai_state_id, o.get("ai_victim"),
						o.get("horde_melee"), o.get("members", []).size(), what, best])
					var ms := {}
					for mid in o.get("members", []):
						var m: Dictionary = _world.get_object(int(mid))
						if m.get("ok", false):
							var k := "%s%s" % [m.get("ai_state_id"), "v" if int(m.get("ai_victim", 0)) != 0 else ""]
							ms[k] = int(ms.get(k, 0)) + 1
					print("UNITS     members by state (v: has a victim): ", ms)
	quit(0)
