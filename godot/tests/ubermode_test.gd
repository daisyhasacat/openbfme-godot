extends SceneTree

var failed := false

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var cfg: Dictionary = ClassDB.instantiate("InstallSetup").configured()
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail_paths(cfg.rotwk, cfg.bfme2).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.execute_lua("return bfme.match.getUnitRoster(1).ready").output == "false\n")
	assert(world.execute_lua("return bfme.match.getCompletedBuildingRoster(1).ready").output == "false\n")
	var script := FileAccess.get_file_as_string("res://dev/ubermode.lua")
	assert(world.execute_lua(script).ok)
	assert(not world.execute_lua("ubermode.enable()").ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"Ubermode test","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	var prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp bamfurlong/map mp bamfurlong.map","seed":2762,"starting_cash":100000,"slots":slots})
	assert(prepared.ok, str(prepared))
	assert(world.start_new_game({"logic_thread":true}).ok)
	world.update_lua()
	check_lua(world, "local p=bfme.players.localPlayer; local r=bfme.match.getUnitRoster(p.id); local units=p:units(); assert(r.ready and #r.ids==#units); for i,id in ipairs(r.ids) do assert(id==units[i].id) end; r.ids[1]=-1; assert(bfme.match.getUnitRoster(p.id).ids[1]==units[1].id)")
	check_lua(world, "local p=bfme.players.localPlayer; oldCP={p.commandPointsBase,p.commandPointsCap,p.commandPointsBonus}; ubermode.enable()")
	world.update_lua()
	check_lua(world, "local p=bfme.players.localPlayer; assert(p.money==1000000); assert(p.commandPoints==1000000)")
	var fog: Dictionary = world.get_shroud_report()
	assert(fog.fogged==0 and fog.shrouded==0)
	assert(fog.clear==fog.cells_x*fog.cells_y)
	check_lua(world, "local p=bfme.players.localPlayer; local near=p:buildings()[1].position; local at=p:findBuildSite('GondorBarracks',near,2000); assert(at); build=p:construct('GondorBarracks',at)")
	world.update_lua()
	for i in 4:
		world.advance(0.2)
		world.update_lua()
	check_lua(world, "local b=bfme.mutations.getBuilding(build); assert(b.underConstruction); unfinishedLevel=b.level; for _,id in ipairs(bfme.match.getCompletedBuildingRoster(b.playerId).ids) do assert(id~=b.id) end")
	OS.delay_msec(210) # Let the 200 ms cheat update run before construction completes.
	world.update_lua()
	world.update_lua()
	check_lua(world, "assert(bfme.mutations.getBuilding(build).level==unfinishedLevel)")
	world.advance(0.2)
	world.update_lua()
	check_lua(world, "assert(not bfme.mutations.getBuilding(build).underConstruction)")
	OS.delay_msec(210)
	world.update_lua()
	world.update_lua() # Apply the level mutation queued by the cheat callback.
	check_lua(world, "local b=bfme.mutations.getBuilding(build); assert(b.level==b.maxLevel); assert(b.level==3); local found=false; for _,id in ipairs(bfme.match.getCompletedBuildingRoster(b.playerId).ids) do if id==b.id then found=true end end; assert(found)")
	for i in 3:
		world.advance(0.2)
		world.update_lua()
	check_lua(world, "b=assert(bfme.mutations.getBuilding(build)); assert(not b.underConstruction); b:trainUnit('GondorFighterHorde',1)")
	world.update_lua()
	for i in 20:
		world.advance(0.2)
		await create_timer(0.21).timeout
		world.update_lua()
	check_lua(world, "local count=0; for _,u in ipairs(bfme.players.localPlayer:units()) do if u.typeId=='GondorFighterHorde' then count=count+1; assert(u.level==u.maxLevel); for _,up in ipairs(u:getUpgrades()) do if up.kind=='object' then assert(up.status=='complete',up.upgrade) end end end end; assert(count==1)")
	check_lua(world, "local p=bfme.players.localPlayer; local at=p:findBuildSite('GondorBarracks',b.position,2000); assert(at); build2=p:construct('GondorBarracks',at)")
	world.update_lua()
	for i in 8:
		world.advance(0.2)
		world.update_lua()
	check_lua(world, "b2=assert(bfme.mutations.getBuilding(build2))")
	var producer_result: Dictionary = world.execute_lua("return b.id")
	assert(producer_result.ok, str(producer_result))
	var producer := int(String(producer_result.output).strip_edges())
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(1800, 0, -1800)}).ok)
	hud.select_controller_group(PackedInt64Array([producer]))
	hud.camera_look_at(Vector2(world.get_object(producer).x, world.get_object(producer).y))
	for i in 12: await process_frame
	var button: Dictionary = {}
	for entry in hud.get_hud6_state().radial.buttons:
		if entry.name == "Command_ConstructGondorFighterHorde": button = entry; break
	assert(not button.is_empty(), str(hud.get_hud6_state()))
	var point: Vector2 = button.rect.get_center()
	var picked: Dictionary = hud.command_button_at(point)
	assert(picked.found and picked.slot == button.slot, str(picked))
	# Exercise real viewport input, including Control focus and button release.
	var key := InputEventKey.new()
	key.keycode = KEY_K
	key.physical_keycode = KEY_K
	key.pressed = true
	root.push_input(key)
	await process_frame
	var motion := InputEventMouseMotion.new()
	motion.position = point
	root.push_input(motion)
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.position = point
	click.pressed = true
	root.push_input(click)
	await process_frame
	click = click.duplicate()
	click.pressed = false
	root.push_input(click)
	key = key.duplicate()
	key.pressed = false
	root.push_input(key)
	check_lua(world, "assert(#ubermode.pending==2); assert(ubermode.pending[1].left==5 and ubermode.pending[2].left==5)")
	hud.free()
	camera.free()
	for i in 130:
		world.advance(0.2)
		await create_timer(0.21).timeout
		world.update_lua()
	check_lua(world, "assert(#ubermode.pending==0); local n=0; for _,u in ipairs(bfme.players.localPlayer:units()) do if u.typeId=='GondorFighterHorde' then n=n+1; assert(u.level==u.maxLevel) end end; assert(n==11,tostring(n))")
	check_lua(world, "ubermode.disable(); assert(not ubermode.enabled); assert(#ubermode.pending==0)")
	world.update_lua()
	check_lua(world, "local p=bfme.players.localPlayer; assert(p.commandPointsBase==oldCP[1]); assert(p.commandPointsCap==oldCP[2]); assert(p.commandPointsBonus==oldCP[3]); assert(not ubermode.massTrain('GondorFighterHorde',b))")
	world.free()
	print("UBERMODE FAIL" if failed else "UBERMODE PASS")
	quit(1 if failed else 0)

func check_lua(world: Node, code: String) -> void:
	var result: Dictionary = world.execute_lua(code)
	if not result.ok:
		failed = true
		printerr("UBERMODE LUA FAIL ", code, ": ", result)
