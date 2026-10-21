## Godot's cumulative compilation monitors. Counts are observations, not timings.
extends RefCounted
const MONITORS := {"canvas": Performance.PIPELINE_COMPILATIONS_CANVAS,
	"mesh": Performance.PIPELINE_COMPILATIONS_MESH, "surface": Performance.PIPELINE_COMPILATIONS_SURFACE,
	"draw": Performance.PIPELINE_COMPILATIONS_DRAW, "specialization": Performance.PIPELINE_COMPILATIONS_SPECIALIZATION}

static func available() -> bool:
	return DisplayServer.get_name() != "headless" and RenderingServer.get_current_rendering_method() != "gl_compatibility"

static func snapshot() -> Dictionary:
	var counts := {}
	for key in MONITORS: counts[key] = int(Performance.get_monitor(MONITORS[key]))
	return counts

static func difference(before: Dictionary, after: Dictionary) -> Dictionary:
	var counts := {}
	for key in MONITORS: counts[key] = int(after[key]) - int(before[key])
	return counts
