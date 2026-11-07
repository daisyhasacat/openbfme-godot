## Installed BFME2 data: enhanced open-gate clearance at the rotated tutorial gate.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("GATE CLEARANCE TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	assert(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	var world: Node = game._world
	world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	assert(game._state == game.State.PLAYING, game._failed)
	world.set_auto_advance(false)
	game._campaign_flow._cine.set_process(false)
	var named: Dictionary = world.debug_script_unit("Front Gate")
	assert(named.ok)
	var gate: Dictionary = world.get_object(named.id)
	for i in 35: world.advance(0.2) # let the tutorial's initial open request settle
	for cycle in 2:
		assert(world.debug_script_action("GATE_CLOSE", [{"type":14,"string":"Front Gate"}]).ok)
		for i in 35: world.advance(0.2)
		assert(_cell(world, gate, 0).type == 4)
		assert(world.debug_script_action("GATE_OPEN", [{"type":14,"string":"Front Gate"}]).ok)
		for i in 35: world.advance(0.2)
		# Posts occupy local Y +/-40..60. Leaves formerly occupied +/-20..40.
		# At this rotated gate the -30 sample shares a 10-unit cell with the -50 post;
		# the -20 and +30 cells are outside the rasterized posts and must open.
		for offset in [-20, -10, 0, 10, 20, 30]:
			var cell: Dictionary = _cell(world, gate, offset)
			print("GATE CLEARANCE ", offset, " ", cell)
			assert(cell.type == 0)
		for offset in [-50, 50]: assert(_cell(world, gate, offset).type == 4)
	game.free()
	print("GATE CLEARANCE PASS")
	quit()

func _cell(world: Node, gate: Dictionary, offset: float) -> Dictionary:
	return world.debug_pathfind_cell(gate.x - sin(gate.angle) * offset, gate.y + cos(gate.angle) * offset)
