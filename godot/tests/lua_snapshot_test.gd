extends SceneTree
## Installed-data regression and repeatable main-thread snapshot benchmark.
func _initialize() -> void:
	call_deferred('_run')

func _run() -> void:
	var fs = ClassDB.instantiate('RetailFileSystem')
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate('GameWorld')
	root.add_child(world)
	assert(world.setup(fs).ok)
	var initial: Dictionary = world.execute_lua('return #bfme.templates')
	assert(initial.ok, str(initial))
	var samples: Array[float] = []
	for i in 30:
		var before := Time.get_ticks_usec()
		var result: Dictionary = world.execute_lua('return bfme.match.getStatus().ready')
		samples.append((Time.get_ticks_usec()-before)/1000.0)
		assert(result.ok and result.output == 'false\n', str(result))
	samples.sort()
	print('LUA SNAPSHOT BENCHMARK templates=', initial.output.strip_edges(), ' median_ms=', samples[15], ' p95_ms=', samples[28], ' max_ms=', samples[29])
	var mutation: Dictionary = world.execute_lua("snapshot_template=bfme.templates.MenFortress; snapshot_cost=snapshot_template.cost; snapshot_ticket=snapshot_template:set('cost', snapshot_cost+1)")
	assert(mutation.ok, str(mutation))
	world.update_lua()
	var changed: Dictionary = world.execute_lua("return bfme.mutations.getStatus(snapshot_ticket),snapshot_template.cost==snapshot_cost+1")
	assert(changed.ok and changed.output=='applied\ttrue\n', str(changed))
	var restore: Dictionary = world.execute_lua("snapshot_template:set('cost', snapshot_cost)")
	assert(restore.ok, str(restore))
	world.update_lua()
	assert(world.execute_lua('return snapshot_template.cost==snapshot_cost').output=='true\n')
	# A map start invalidates old handles even though most definitions retain their names.
	var slots: Array = []
	for i in 8:
		slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
	slots[0]={'state':6,'name':'Snapshot test','accepted':true,'color':0,'start_pos':0,'player_template':4,'team':0}
	var prepared: Dictionary = world.prepare_new_game({'mode':'skirmish','difficulty':1,'rank_points':0,'map_crc':0,'map_size':0,'map_mask':0,'map':'maps/map mp bamfurlong/map mp bamfurlong.map','seed':2762,'starting_cash':1000,'slots':slots})
	assert(prepared.ok, str(prepared))
	var started: Dictionary = world.start_new_game({'logic_thread':false})
	assert(started.ok, str(started.get('errors',[])))
	assert(not world.execute_lua('return snapshot_template.cost').ok)
	samples.clear()
	for i in 30:
		var before := Time.get_ticks_usec()
		var result: Dictionary = world.execute_lua('return bfme.match.getStatus().ready')
		samples.append((Time.get_ticks_usec()-before)/1000.0)
		assert(result.ok and result.output == 'true\n', str(result))
	samples.sort()
	print('LUA LIVE SNAPSHOT BENCHMARK median_ms=', samples[15], ' p95_ms=', samples[28], ' max_ms=', samples[29])
	assert(world.clear_game_data(false).ok)
	world.free()
	print('LUA SNAPSHOT PASS')
	quit()
