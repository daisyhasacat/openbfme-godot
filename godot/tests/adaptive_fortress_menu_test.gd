extends SceneTree
## Installed-data regression: the builder side bar must not replace fortress submenus.
func _initialize() -> void:
	call_deferred('_run')

func _run() -> void:
	var fs = ClassDB.instantiate('RetailFileSystem')
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate('GameWorld')
	root.add_child(world)
	assert(world.setup(fs).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	for construction in [false, true]:
		var slots:Array=[]
		for i in 8:slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
		# Installed RotWK store: Men is player-template index 4.
		slots[0]={'state':6,'name':'Fortress test','accepted':true,'color':0,'start_pos':0,'player_template':4,'team':0}
		slots[1]={'state':3,'name':'AI builders test','accepted':true,'color':1,'start_pos':1,'player_template':4,'team':1}
		var prepared:Dictionary=world.prepare_new_game({'mode':'skirmish','difficulty':1,'rank_points':0,'map_crc':0,'map_size':0,'map_mask':0,'map':'maps/map mp bamfurlong/map mp bamfurlong.map','seed':4330,'starting_cash':100000,'fortress_construction':construction,'slots':slots})
		assert(prepared.ok,str(prepared))
		var started:Dictionary=world.start_new_game({'logic_thread':false})
		assert(started.ok,str(started.get('errors',[])))
		var builders: Dictionary = world.execute_lua("local counts={}; for _,p in pairs(bfme.players) do if p.name=='Player_1' or p.name=='Player_2' then local n=0; for _,u in ipairs(p:units()) do if u:isKindOf('DOZER') then n=n+1 end end; counts[p.name]=n end end; return counts.Player_1,counts.Player_2")
		assert(builders.ok and builders.output == ('0\t2\n' if construction else '2\t2\n'), str(builders))
		var hud=ClassDB.instantiate('InGameHudNode');root.add_child(hud)
		assert(hud.setup(fs,world,camera,'Player_1',{'camera_start':Vector3(1800,0,-1800)}).ok)
		var fortress:=0
		for id in world.get_object_ids():
			var object:Dictionary=world.get_object(id)
			if object.get('template','')=='MenFortressCitadel' and object.get('owner','')=='Player_1':fortress=id;break
		# tolerance: object IDs are allocated at runtime; zero is the invalid-ID sentinel.
		assert(fortress!=0,'Missing starting fortress')
		hud.select_controller_group(PackedInt64Array([fortress]))
		var ui=load('res://scripts/adaptive_hud.gd').new()
		ui.hud=func():return hud
		ui.gameplay=func():return true
		ui.open_menu=func():pass
		root.add_child(ui);ui.mode='modern';ui._apply_mode()
		for i in 6:await process_frame
		ui._refresh()
		assert(not ui._construction_entries.is_empty() if construction else ui._construction_entries.is_empty())
		for menu in ['Command_SelectUpgradesMenFortress','Command_SelectRevivablesMenFortress']:
			var index: int=_find(ui._command_entries,menu)
			assert(index!=-1,menu+' absent with fortress construction='+str(construction))
			ui._command_tiles[index].pressed.emit()
			for i in 4:await process_frame
			ui._refresh()
			var back:int=_find(ui._command_entries,'Command_RadialBack')
			assert(back!=-1,'Submenu did not open: '+menu)
			if menu=='Command_SelectUpgradesMenFortress':
				assert(_find(ui._command_entries,'Command_PurchaseUpgradeMenFortressBanners')!=-1)
			assert(not ui._construction_entries.is_empty() if construction else ui._construction_entries.is_empty())
			ui._command_tiles[back].pressed.emit()
			for i in 4:await process_frame
			ui._refresh()
			assert(_find(ui._command_entries,menu)!=-1,'Back failed')
		ui.free();hud.free()
		assert(world.clear_game_data(false).ok)
		print('ADAPTIVE FORTRESS MENUS PASS construction=',construction)
	world.free();camera.free();quit()

func _find(entries:Array,command:String)->int:
	for i in entries.size():
		if entries[i].get('name','')==command:return i
	return -1
