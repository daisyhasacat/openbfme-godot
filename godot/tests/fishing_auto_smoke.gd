## Real-map fishing placement check. Requires BFME2_INSTALL and --mod=<generated fishing mod>.
extends SceneTree
func _initialize() -> void:
	call_deferred("run")
func fail(message: String) -> void:
	push_error(message)
	quit(1)
func run() -> void:
	for map_name in ["map mp evendim", "map mp tournament udun"]:
		var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
		var mount: Dictionary = fs.mount_bfme2_path(OS.get_environment("BFME2_INSTALL"))
		if not mount.ok:
			fail(str(mount.errors)); return
		var world: Node3D = ClassDB.instantiate("GameWorld")
		root.add_child(world)
		var setup: Dictionary = world.setup(fs)
		if not setup.ok:
			fail(str(setup.errors)); return
		var result: Dictionary = world.load_map(map_name, {"slots": [
			{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
			{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1}
		], "seed": 4711, "logic_thread": false})
		if not result.ok:
			fail(str(result.errors)); return
		var spots: Array[Vector2] = []
		for id in world.get_object_ids():
			var obj: Dictionary = world.get_object(id)
			if obj.get("template", "") != "OpenBFMEFishingSpot": continue
			if obj.player_id != 0:
				fail("Fishing spot is not neutral"); return
			var cell: Dictionary = world.debug_pathfind_cell(obj.x, obj.y)
			if not cell.get("ok", false) or int(cell.type) != 7 or not cell.has("water_height") or float(cell.water_height) - float(cell.ground) < 20:
				fail("Spot is outside deep water"); return
			var point := Vector2(obj.x, obj.y)
			for other in spots:
				if point.distance_to(other) < 650:
					fail("Fishing spots are too close"); return
			spots.append(point)
		if (map_name == "map mp evendim" and spots.is_empty()) or (map_name == "map mp tournament udun" and not spots.is_empty()):
			fail("Unexpected spot count on %s: %d" % [map_name, spots.size()]); return
		print("FISHING AUTO MAP PASS: %s, %d neutral deep-water spots" % [map_name, spots.size()])
		world.free()
		fs = null
		await process_frame
	print("FISHING AUTO SMOKE PASS")
	quit(0)
