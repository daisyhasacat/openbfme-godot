extends SceneTree
func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("BFME1 actions timed out"); quit(1))
	call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280, 720)
	if OS.get_environment("BFME1_INSTALL").is_empty():
		print("BFME1 PLOT SKIP: set BFME1_INSTALL")
		quit()
		return
	var faction := 4
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--faction-index="):faction=int(arg.trim_prefix("--faction-index="))
	var prefix:String={3:"Rohan",4:"Gondor",5:"Isengard",6:"Mordor"}[faction]
	var factory:String={3:"RohanArcherRange",4:"GondorBarracks",5:"IsengardUrukPit",6:"MordorOrcPit"}[faction]
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(OS.get_environment("BFME1_INSTALL")).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var start_slots: Array = []
	for i in 8: start_slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	start_slots[0] = {"state":6,"name":"PlotTest","accepted":true,"color":0,"start_pos":0,"player_template":faction,"team":0}
	start_slots[1] = {"state":2,"name":"Enemy","accepted":true,"color":1,"start_pos":1,"player_template":6,"team":1}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":100000,"slots":start_slots}).ok)
	assert(world.start_new_game({"starting_walls":false,"require_enclosed":false,"logic_thread":false}).ok)
	for i in 8: world.advance(0.2)
	var plot := 0
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.get("owner","")=="Player_1" and object.template==prefix+"BuildingFoundation": plot=id; break
	assert(plot>0,"No fortress plot")
	var info: Dictionary = world.get_object(plot)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(info.x,0,-info.y)}).ok)
	world.set_shroud_drawn(false)
	for i in 10: await process_frame
	var pixel: Vector2 = camera.unproject_position(Vector3(info.x,info.z,-info.y))
	var probe: Dictionary = hud.pick_probe(pixel,plot)
	assert(probe.picked == plot, str(probe))
	hud.inject_mouse_move(pixel)
	hud.inject_mouse_button(1,true,pixel,false)
	hud.inject_mouse_button(1,false,pixel,false)
	for i in 4: await process_frame
	world.advance(0.2)
	assert(hud.get_selection().has(plot), str(hud.get_selection()))
	for i in 90: await process_frame
	var radial: Dictionary = hud.get_hud6_state().radial
	assert(radial.count > 0 and radial.draws > 0, str(radial))
	for error in hud.get_report().errors:
		assert(not "BuildingRadialButtons" in error and not "RadialBorders" in error, error)
	var command := {}
	for button in hud.get_command_buttons():
		if button.command == "FOUNDATION_CONSTRUCT" and button.template == factory: command=button
	assert(not command.is_empty(), str(hud.get_command_buttons()))
	var existing: Array = world.get_object_ids()
	assert(hud.activate_command(command.slot,command.in_palantir,false))
	var barracks := 0
	for i in 400:
		world.advance(0.2)
		if i%20==0: await process_frame
		for id in world.get_object_ids():
			var object: Dictionary=world.get_object(id)
			if not existing.has(id) and object.template==factory: barracks=id
		if barracks>0 and not world.get_object(barracks).under_construction: break
	assert(barracks>0 and not world.get_object(barracks).under_construction,"Barracks did not finish")
	print("BFME1 ACTION built factory ",barracks)
	hud.select_controller_group(PackedInt64Array([barracks]))
	var recruit := {}
	for i in 400:
		world.advance(0.2)
		await process_frame
		for button in hud.get_command_buttons():
			if button.command=="UNIT_BUILD" and button.state==1: recruit=button;break
		if not recruit.is_empty():break
	assert(not recruit.is_empty(),str(hud.get_command_buttons()))
	existing=world.get_object_ids()
	assert(hud.activate_command(recruit.slot,recruit.in_palantir,false))
	var army := 0
	for i in 500:
		world.advance(0.2)
		if i%20==0: await process_frame
		for id in world.get_object_ids():
			var object: Dictionary=world.get_object(id)
			if not existing.has(id) and object.template==recruit.template: army=id
		if army>0:break
	assert(army>0,"Training did not produce "+str(recruit))
	print("BFME1 ACTION trained ",recruit.template," id ",army)
	for i in 100: world.advance(0.2)
	var at:Dictionary=world.get_object(army)
	var victim:int=world.create_object("MordorFighter",4,at.x+75,at.y,0)
	assert(victim>0,str(world.get_report()))
	var before:float=world.get_object(victim).health
	assert(world.order_attack([army],victim).ok)
	var damaged := false
	for i in 400:
		world.advance(0.2)
		if i%20==0:await process_frame
		var target:Dictionary=world.get_object(victim)
		if not target.ok or target.health<before:damaged=true;break
	if not damaged:
		print("BFME1 COMBAT army ",world.get_object(army)," enemy ",world.get_object(victim))
		for id in world.get_object(army).members: print("BFME1 COMBAT member ",world.get_object(id))
	assert(damaged,"Trained army failed to damage enemy")
	print("BFME1 ACTION attacked enemy")
	var neutral := 0
	for id in world.get_object_ids():
		var object:Dictionary=world.get_object(id)
		if object.template in ["SettlementFlag","OutpostFlag","CampFlag"] and object.player_id!=3:
			neutral=id;break
	assert(neutral>0,"No neutral capture plot")
	var site:Dictionary=world.get_object(neutral)
	print("BFME1 ACTION capture target ",site)
	assert(world.order_move([army],site.x,site.y,{}).ok)
	var captured := false
	for i in 2000:
		world.advance(0.2)
		if i%20==0:await process_frame
		if world.get_object(neutral).player_id==3:captured=true;break
	assert(captured,"Army did not capture plot: "+str(world.get_object(neutral))+" army "+str(world.get_object(army)))
	hud.select_controller_group(PackedInt64Array([neutral]))
	for i in 90: await process_frame
	var unpack := {}
	for button in hud.get_command_buttons():
		if button.command=="CASTLE_UNPACK" and button.state in [1,5]:unpack=button;break
	assert(not unpack.is_empty(),"Captured plot has no unpack command: "+str(hud.get_command_buttons()))
	existing=world.get_object_ids()
	assert(hud.activate_command(unpack.slot,unpack.in_palantir,false))
	var expansion := 0
	for i in 500:
		world.advance(0.2)
		if i%20==0:await process_frame
		for id in world.get_object_ids():
			var object:Dictionary=world.get_object(id)
			if not existing.has(id) and object.player_id==3 and object.template==prefix+"BuildingFoundation":expansion=id
		if expansion>0:break
	assert(expansion>0,"Captured outpost did not unpack build plots")
	print("BFME1 ACTION unpacked captured outpost")
	print("BFME1 ACTIONS PASS ",prefix,": plot construction, training, combat, neutral capture and outpost unpack")
	quit()
