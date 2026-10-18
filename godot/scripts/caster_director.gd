## Presentation-only camera policy over completed simulation events.
extends RefCounted

const CATEGORIES := {
	"hero": "Hero deaths", "capture": "Base ownership", "base": "Base damage / loss",
	"power": "Power uses", "fight": "Fights / building losses",
}

var enabled := false
var priorities := {"hero": 5, "capture": 5, "base": 4, "power": 3, "fight": 2}
var dwell_seconds := 6.0
var return_seconds := 12.0
var move_seconds := 2.0
var far_cut_distance := 2500.0
var camera_height := 450.0
var lookback_frames := 75
var choice: Dictionary = {}
var manual_remaining := 0.0
var _clock := 0.0
var _hold_until := 0.0
var _move_elapsed := 0.0
var _from_position := Vector2.ZERO
var _from_height := 0.0
var _cut := false
var _last_pose: Dictionary = {}


static func category(event: Dictionary) -> String:
	if event.kind == 1 and event.hero:
		return "hero"
	if event.kind == 3 and event.base:
		return "capture"
	if event.base and event.kind in [0, 1]:
		return "base"
	if event.kind == 2:
		return "power"
	return "fight"


static func event_key(event: Dictionary) -> String:
	return "%d:%d:%d" % [event.frame, event.kind, event.object]


func choose(events: Array, frame: int) -> Dictionary:
	var best: Dictionary = {}
	for event in events:
		if event.frame > frame or event.frame < frame - lookback_frames:
			continue
		var priority: int = priorities[category(event)]
		if priority <= 0:
			continue
		if best.is_empty() or _better(event, best):
			best = event
	return best


func _better(a: Dictionary, b: Dictionary) -> bool:
	var ap: int = priorities[category(a)]
	var bp: int = priorities[category(b)]
	if ap != bp:
		return ap > bp
	var aw: float = a.weight if a.kind == 0 else 0.0
	var bw: float = b.weight if b.kind == 0 else 0.0
	if aw != bw:
		return aw > bw
	if a.frame != b.frame:
		return a.frame > b.frame
	return a.object < b.object


func set_enabled(value: bool) -> void:
	enabled = value
	choice = {}
	_last_pose = {}
	manual_remaining = 0.0
	_hold_until = 0.0


func take_over() -> void:
	manual_remaining = return_seconds
	choice = {}
	_last_pose = {}
	_hold_until = 0.0


func remember_pose(camera: Dictionary) -> void:
	_last_pose = camera.duplicate()


func _externally_moved(camera: Dictionary) -> bool:
	if _last_pose.is_empty():
		return false
	return (camera.position.distance_to(_last_pose.position) > 0.05
		or absf(camera.height_above_ground - _last_pose.height_above_ground) > 0.05
		or absf(camera.angle - _last_pose.angle) > 0.001
		or absf(camera.pitch - _last_pose.pitch) > 0.001)


func tick(events: Array, frame: int, delta: float, camera: Dictionary, manual: bool = false) -> Dictionary:
	if not enabled or camera.is_empty():
		return {}
	_clock += delta
	if manual or camera.get("scrolling", false) or camera.get("rotating", false) or _externally_moved(camera):
		take_over()
		return {}
	manual_remaining = maxf(0.0, manual_remaining - delta)
	if manual_remaining > 0.0:
		return {}
	if _clock >= _hold_until:
		var candidate := choose(events, frame)
		if candidate.is_empty():
			choice = {}
		elif choice.is_empty() or event_key(candidate) != event_key(choice):
			choice = candidate
			_hold_until = _clock + dwell_seconds
			_from_position = camera.position
			_from_height = camera.height_above_ground
			_move_elapsed = 0.0
			_cut = _from_position.distance_to(choice.position) > far_cut_distance
	if choice.is_empty():
		return {}
	_move_elapsed += delta
	var t := 1.0 if _cut else clampf(_move_elapsed / maxf(0.01, move_seconds), 0.0, 1.0)
	var eased := t * t * (3.0 - 2.0 * t)
	return {"position": _from_position.lerp(choice.position, eased), "height": lerpf(_from_height, camera_height, eased), "cut": _cut}


func preferences() -> Dictionary:
	return {"enabled": enabled, "priorities": priorities.duplicate(), "dwell_seconds": dwell_seconds,
		"return_seconds": return_seconds, "move_seconds": move_seconds, "far_cut_distance": far_cut_distance,
		"camera_height": camera_height}


func restore_preferences(values: Dictionary) -> void:
	for key in values:
		if key != "enabled":
			set(key, values[key])
	set_enabled(values.get("enabled", false))
