extends SceneTree
## Installed-data integration: C++ match/placement notifications and timer-free scenery setup.
func _initialize() -> void:
	call_deferred('_run')

func _run() -> void:
	var fs = ClassDB.instantiate('RetailFileSystem')
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate('GameWorld')
	root.add_child(world)
	assert(world.setup(fs).ok)
	var script := FileAccess.get_file_as_string('res://dev/neutral_decorations.lua')
	assert(world.execute_lua(script).ok)
	assert(world.execute_lua("starts=0; placed=0; bfme.events.onMatchStarted('test-start',function(e) starts=starts+1; ready=bfme.match.getStatus().ready end); bfme.events.onBuildingPlaced('test-place',function(e) placed=placed+1; placement=e end)").ok)
	var slots: Array = []
	for i in 8:
		slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
	slots[0]={'state':6,'name':'Events test','accepted':true,'color':0,'start_pos':0,'player_template':4,'team':0}
	var prepared: Dictionary = world.prepare_new_game({'mode':'skirmish','difficulty':1,'rank_points':0,'map_crc':0,'map_size':0,'map_mask':0,'map':'maps/map mp bamfurlong/map mp bamfurlong.map','seed':2762,'starting_cash':100000,'slots':slots})
	assert(prepared.ok, str(prepared))
	var started: Dictionary = world.start_new_game({'logic_thread':true})
	assert(started.ok, str(started.get('errors',[])))
	assert(world.execute_lua('return starts,placed').output=='0\t0\n')
	world.update_lua()
	assert(world.execute_lua('return starts,ready,placed').output=='1\ttrue\t0\n')
	world.update_lua() # applies the neutral-decoration option queued by the start listener
	var timers: Dictionary = world.execute_lua("for _,e in ipairs(bfme.events.list()) do if e.name=='neutral-decoration-placement' then return e.event,e.intervalMs end end")
	assert(timers.ok and timers.output=='matchStarted\tnil\n', str(timers))
	var build: Dictionary = world.execute_lua("local p=bfme.players.localPlayer; local near=p:buildings()[1].position; local at=p:findBuildSite('GondorBarracks',near,2000); assert(at,'No construction site'); build_ticket=p:construct('GondorBarracks',at)")
	assert(build.ok, str(build))
	world.update_lua() # constructs the foundation in C++; callback must wait for the next safe point
	assert(world.execute_lua('return placed').output=='0\n')
	world.update_lua()
	var event: Dictionary = world.execute_lua("return placed,placement.typeId,placement.builderId,placement.objectId==bfme.mutations.getBuilding(build_ticket).id,type(placement.position.x)")
	assert(event.ok and event.output=='1\tGondorBarracks\t0\ttrue\tnumber\n', str(event))
	world.update_lua()
	assert(world.execute_lua('return placed,starts').output=='1\t1\n')
	assert(world.execute_lua(script).ok) # hot reload replaces the listener and applies its current value
	world.update_lua()
	assert(world.clear_game_data(false).ok)
	world.update_lua()
	assert(world.execute_lua('return starts,placed').output=='1\t1\n')
	world.free()
	print('LUA ENGINE EVENTS PASS')
	quit()
