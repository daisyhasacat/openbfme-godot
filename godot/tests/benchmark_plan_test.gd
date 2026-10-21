extends SceneTree
const Plan := preload("res://scripts/benchmark_plan.gd")
func _initialize() -> void:
	var types := [{"name": "swords", "cp": 3}, {"name": "archers", "cp": 4}, {"name": "cavalry", "cp": 6}]
	for target in [0, 1000, 20000]:
		var army := Plan.army(target, types)
		assert(army.ok)
		var actual := 0
		for name in army.units:
			for entry in types:
				if entry.name == name: actual += int(entry.cp)
		assert(actual == target)
		assert(army == Plan.army(target, types), "The workload must be deterministic")
	assert(not Plan.army(7, [{"name": "unit", "cp": 4}]).ok)
	assert(not Plan.army(1000, [{"name": "unit", "cp": 0}]).ok)
	assert(not Plan.army(20001, types).ok)
	assert(Plan.frame_summary([]).is_empty())
	assert(Plan.frame_summary([{"frame": 10}, {"frame": 20}, {"frame": 30}, {"frame": 40}]) == {
		"fps": 40.0, "frame_p95_ms": 40.0, "frame_p99_ms": 40.0, "seconds": 0.1, "frames": 4})
	assert(Plan.camera_pose(0.0, Vector2(1000, 1000), 100, 5000) == {"position": Vector2(400, 800), "height": 5000.0})
	assert(Plan.camera_pose(0.2, Vector2.ZERO, 100, 5000) == Plan.camera_pose(0.0, Vector2.ZERO, 100, 5000))
	assert(Plan.camera_pose(0.5, Vector2.ZERO, 100, 5000) == {"position": Vector2.ZERO, "height": 2550.0})
	assert(Plan.camera_pose(0.8, Vector2.ZERO, 100, 5000) == {"position": Vector2(600, 200), "height": 100.0})
	assert(Plan.camera_pose(1.0, Vector2.ZERO, 100, 5000) == Plan.camera_pose(0.8, Vector2.ZERO, 100, 5000))
	var screen = load("res://scripts/openbfme_options_screen.gd").new()
	assert("Benchmark" in screen.TABS)
	screen._tab = "Benchmark"
	assert(screen._spec().note.contains("20,000 CP"))
	screen.free()
	print("BENCHMARK PLAN PASS")
	quit()
