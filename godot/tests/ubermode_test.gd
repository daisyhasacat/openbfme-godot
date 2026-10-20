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
	check_lua(world, "local p=bfme.players.localPlayer; oldCP={p.commandPointsBase,p.commandPointsCap,p.commandPointsBonus}; ubermode.enable()")
	world.update_lua()
	check_lua(world, "local p=bfme.players.localPlayer; assert(p.money==1000000); assert(p.commandPoints==1000000)")
	check_lua(world, "local p=bfme.players.localPlayer; local near=p:buildings()[1].position; local at=p:findBuildSite('GondorBarracks',near,2000); assert(at); build=p:construct('GondorBarracks',at)")
	world.update_lua()
	for i in 4:
		world.advance(0.2)
		world.update_lua()
	check_lua(world, "assert(bfme.mutations.getBuilding(build).underConstruction)")
	world.advance(0.2)
	world.update_lua()
	check_lua(world, "assert(not bfme.mutations.getBuilding(build).underConstruction)")
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
	check_lua(world, "b2=assert(bfme.mutations.getBuilding(build2)); assert(ubermode.massTrain('GondorFighterHorde',b)); assert(#ubermode.pending==2); assert(ubermode.pending[1].left==5 and ubermode.pending[2].left==5)")
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
