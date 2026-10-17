extends SceneTree
## AI observation probe: a headless skirmish between computer players, logged once per game minute (economy, army, buildings, the skirmish AI's
## tactics and the combat counters), so the AI's behaviour can be read from the log. Not a pass/fail test.
## Args (after --): map=<map key stem> factions=<idx,idx,...> difficulty=<2..5> minutes=<n> seed=<n> human=<0|1> walls=<0|1> inspect=<0|1>
## (faction indices: 3 Men, 5 Elves, 6 Dwarves, 7 Isengard, 8 Mordor, 9 Wild; walls=0 plays without the wall-balance profile, as retail)
## Needs ROTWK_INSTALL / BFME2_INSTALL.
var _world: Node3D
var _args := {"map": "map mp evendim", "factions": "3,8", "difficulty": "4", "minutes": "15", "seed": "4242", "human": "0", "walls": "1"}


func _initialize() -> void:
	call_deferred("_main")


func _lua(code: String) -> String:
	var r: Dictionary = _world.execute_lua(code)
	return String(r.output).strip_edges() if r.ok else "LUA ERROR " + String(r.output)


func _message() -> Dictionary:
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	var factions: PackedStringArray = String(_args.factions).split(",")
	for i in factions.size():
		var human: bool = i == 0 and _args.human == "1"
		slots[i] = {"state": 6 if human else int(_args.difficulty), "name": "Human" if human else "AI %d" % i, "accepted": true, "color": i,
			"start_pos": -1, "player_template": int(factions[i]), "team": i}
	var stem: String = _args.map
	return {"mode": "skirmish", "difficulty": 1, "rank_points": 0, "map": "maps/%s/%s.map" % [stem, stem], "map_crc": 0, "map_size": 0, "map_mask": 0,
		"seed": int(_args.seed), "starting_cash": -1, "slots": slots}


func _main() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			_args[kv[0]] = kv[1]
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		print("PROBE FAIL mount"); quit(1); return
	_world = ClassDB.instantiate("GameWorld")
	root.add_child(_world)
	if not _world.setup(fs).ok:
		print("PROBE FAIL setup"); quit(1); return
	var prep: Dictionary = _world.prepare_new_game(_message())
	if not prep.ok:
		print("PROBE FAIL prepare ", prep); quit(1); return
	var started: Dictionary = _world.start_new_game({} if _args.walls == "1" else {"wall_rebalance": false})
	if not started.ok:
		print("PROBE FAIL start ", started); quit(1); return
	print("PROBE start %s" % JSON.stringify(_args))
	print("PROBE prepare ", JSON.stringify(prep).left(600))
	print("PROBE all players ", _lua("local o={} for i,p in pairs(bfme.players) do if type(i)=='number' then local st=p:getAIStatus() o[#o+1]=i..':'..tostring(p.name)..' side='..tostring(p.side)..' faction='..tostring(p.faction)..' ai='..tostring(st and st.enabled)..' u='..#p:units()..' b='..#p:buildings() end end return table.concat(o,' ')"))
	print("PROBE players ", _lua("local o={} for _,p in ipairs(bfme.ai.list()) do local s=p:getStartPosition() o[#o+1]=p.id..':'..tostring(p.name)..' '..tostring(p.faction or p.side)..' start '..(s and string.format('%.0f,%.0f',s.x,s.y) or '?') end return table.concat(o,' | ')"))
	var minutes := int(_args.minutes)
	var t0 := Time.get_ticks_msec()
	for minute in range(1, minutes + 1):
		for second in 60:
			for f in 5:
				_world.advance(0.2)
		_report(minute)
		if _lua("local alive=0 for _,p in ipairs(bfme.ai.list()) do if #p:buildings()>0 then alive=alive+1 end end return alive") == "1":
			print("PROBE one player left at minute %d" % minute)
			break
	print(_lua("""local o={} for _,p in ipairs(bfme.ai.list()) do local t={} for _,b in ipairs(p:buildings()) do t[#t+1]=b.typeId..'('..math.floor(b.health)..'/'..math.floor(b.maxHealth)..')' end o[#o+1]='PROBE P'..p.id..' buildings: '..table.concat(t,', ') end return table.concat(o,'\\n')"""))
	if _args.has("object"):
		print("PROBE object ", JSON.stringify(_world.get_object(int(_args.object))))
	print("PROBE done in %.1f s wall" % ((Time.get_ticks_msec() - t0) / 1000.0))
	quit(0)


func _report(minute: int) -> void:
	print("PROBE ---- minute %d (frame %d, %d objects)" % [minute, _world.get_frame(), _world.get_object_count()])
	print(_lua("""local o={}
for _,p in ipairs(bfme.ai.list()) do
 local units,heroes,builders=0,0,0
 for _,u in ipairs(p:units()) do units=units+1; if u.isHero then heroes=heroes+1 end; if u.isBuilder then builders=builders+1 end end
 local eco,fac,other=0,0,0
 for _,b in ipairs(p:buildings()) do if b.isEconomy then eco=eco+1 elseif b.isFactory then fac=fac+1 else other=other+1 end end
 o[#o+1]=string.format('PROBE P%d money %d cp %d/%d units %d (heroes %d builders %d) buildings eco %d factory %d other %d',
  p.id,p.money,p.commandPointsUsed,p.commandPointsCap,units,heroes,builders,eco,fac,other)
end return table.concat(o,'\\n')"""))
	var by_player := {}
	for t in _world.get_ai_report():
		var key := int(t.player)
		if not by_player.has(key):
			by_player[key] = []
		var members := 0
		for team in t.teams:
			members += team.members.size()
		# where the tactic's units stand: the average position, and its distance to the target
		var sum := Vector2.ZERO
		var n := 0
		for team in t.teams:
			for id in team.members:
				var o: Dictionary = _world.get_object(id)
				if o.get("ok", false):
					sum += Vector2(float(o.x), float(o.y))
					n += 1
		var where := ""
		if n > 0:
			var c := sum / n
			where = " at %.0f,%.0f" % [c.x, c.y]
			if t.has_target:
				where += " %.0f from target" % c.distance_to(t.target)
		by_player[key].append("%s(%s step %d, %d teams, %d units%s%s)" % [t.kind, "started" if t.started else "forming", t.step, t.teams.size(), members, ", target %.0f,%.0f" % [t.target.x, t.target.y] if t.has_target else "", where])
	for p in by_player:
		print("PROBE P%d tactics: %s" % [p, ", ".join(by_player[p])])
	if _args.get("inspect", "0") == "1":
		for t in _world.get_ai_report():
			for team in t.teams:
				for id in team.members:
					var o: Dictionary = _world.get_object(int(id))
					if o.get("ok", false):
						print("PROBE inspect P%d %s %s#%d at %.0f,%.0f state %s(%s) temp %s victim %s goal %s goal_obj %s source %s horde_melee %s" % [int(t.player), t.kind,
							o.template, int(id), o.x, o.y, o.get("ai_state", "?"), o.get("ai_state_id", "?"), o.get("ai_temporary_state_id", "?"), o.get("ai_victim", "?"),
							o.get("ai_goal", "?"), o.get("ai_goal_object", "?"), o.get("ai_command_source", "?"), o.get("horde_melee", "-")])
						print("PROBE   mover waiting %s path %s blocked %s/%s moving %s path_frame %s queue %s mover_goal %s frame %d" % [o.get("ai_waiting_for_path"), o.get("ai_has_path"),
							o.get("ai_blocked"), o.get("ai_blocked_frames"), o.get("ai_moving"), o.get("ai_path_frame"), o.get("ai_queue_for_path_frame"), o.get("ai_mover_goal"), _world.get_frame()])
	# the armies' spread: near their own start, near the enemy's, in between
	print(_lua("""local o={} local ais=bfme.ai.list()
for _,p in ipairs(ais) do local mine=p:getStartPosition() local foe
 for _,q in ipairs(ais) do if q.id~=p.id then foe=q:getStartPosition() end end
 local home,away,mid=0,0,0
 for _,u in ipairs(p:units()) do if not u.isBuilder then
  local dh=bfme.map.distance(u,mine) local de=foe and bfme.map.distance(u,foe) or 1e9
  if dh<1200 then home=home+1 elseif de<1200 then away=away+1 else mid=mid+1 end end end
 o[#o+1]=string.format('PROBE P%d army: home %d, mid %d, at enemy base %d',p.id,home,mid,away) end
return table.concat(o,'\\n')"""))
	if _args.get("build", "0") == "1":
		for b in _world.get_ai_build_report():
			var pend := {}
			for r in b.pending:
				var k := "%s[%s c%d%s]" % [r.template, "u" if r.unit else ("f" if r.farm else "s"), int(r.last_check), "" if r.unit else " @%.0f,%.0f" % [r.site.x, r.site.y]]
				pend[k] = int(pend.get(k, 0)) + 1
			print("PROBE P%d build: phase %d reserve %d (saved %d drawn %d) units made %d executed %d heroes %d attackers %d attacker factories %d factories %s" % [int(b.player), int(b.phase),
				int(b.money_reserve), int(b.reserve_saved), int(b.reserve_drawn), int(b.units_made), int(b.units_executed), int(b.heroes_made), int(b.fortress_attacker_requests),
				int(b.get("attacker_factory_requests", 0)), str(b.factory_entries)])
			print("PROBE P%d pending: %s" % [int(b.player), str(pend)])
			print("PROBE P%d economy: %s" % [int(b.player), str(b.economy)])
	var c: Dictionary = _world.get_combat_report()
	print("PROBE combat kills %s auto_acquire %s damage %s" % [str(c.get("kills", "?")), str(c.get("auto_acquire", "?")), str(c.get("damage_applications", "?"))])
