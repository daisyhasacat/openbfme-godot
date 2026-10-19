## Seven native AIs at 100x on an eight-player map. Report throughput and hash;
## timing is observational, never a pass/fail threshold. Requires retail installs.
## godot --headless --path godot --script tests/ai_fast_forward_bench.gd
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		quit(77); return
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	var factions := [3,8,5,7,6,9,3,8]
	for i in 8:
		slots.append({"state":6 if i==0 else 4,"name":"Human" if i==0 else "AI %d"%i,"accepted":true,
			"color":i,"start_pos":-1,"player_template":factions[i],"team":i})
	var map := "maps/map mp fall back 8p/map mp fall back 8p.map"
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":map,
		"map_crc":0,"map_size":0,"map_mask":0,"seed":4242,"starting_cash":-1,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false,"wall_rebalance":false}).ok)
	world.set_time_scale(100.0)
	var start_frame: int = world.get_frame()
	var started := Time.get_ticks_usec()
	# 180 render steps at 60 Hz request five game minutes at 100x.
	for i in 180: world.advance(1.0/60.0)
	var elapsed := (Time.get_ticks_usec()-started)/1000000.0
	print("AI FAST FORWARD ",JSON.stringify({"seconds":elapsed,"frames":world.get_frame()-start_frame,
		"objects":world.get_object_ids().size(),"hash":world.get_state_hash(),"scale":world.get_time_scale()}))
	quit()
