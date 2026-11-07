extends Node
## OpenBFME map editor wave spawners (not retail). A marker sends armies of the player at a nearby start position (Player_N_Start: whoever the lobby
## puts on that spot, normally an AI) at the local player, one wave every interval. A marker may stand in the map's border, outside the playable
## area: its waves then appear there and march in. Local skirmishes only, like the editor's other edits: the units are created directly in the logic
## (GameWorld.create_object) and attack-move through the command stream (GameWorld.order_move). The owner's skirmish AI leaves them alone
## (GameWorld.reserve_from_ai): it would recruit them into its own teams.
##
## A marker: { id, x, y, start (N of Player_N_Start), delay (s before the first wave), interval (s between waves), waves (0 = no end),
## units: [{ template, count }] }. The editor saves the list in its edit files.

const FRAMES_PER_SECOND := 5.0 # RotWK LOGICFRAMES_PER_SECOND (Common/GameCommon.h)
const STEER_SECONDS := 4.0     # how often the waves' targets and progress are checked
const MAX_PER_WAVE := 100
const SPACING := 70.0

signal wave_spawned(marker_id: int, created: int, message: String)

var world: Node
var markers: Array = []
var _runs := {}         # marker id -> { next (logic frame of the next wave), spawned (waves so far) }
var _groups: Array = [] # [{ marker, ids, target, seen: { id: Vector2 } }]
var _context := {}      # get_start_positions(), read once a match (players do not change start spots)
var _next_steer := 0


func set_markers(list: Array) -> void:
	markers = list.duplicate(true)
	var frame: int = world.get_frame()
	var kept := {}
	for marker in markers:
		var id := int(marker.id)
		var run: Dictionary = _runs.get(id, {})
		# a new marker, or a changed delay before the first wave: the first wave is that delay from now
		if run.is_empty() or (int(run.spawned) == 0 and float(run.delay) != float(marker.delay)):
			run = {"next": frame + int(float(marker.delay) * FRAMES_PER_SECOND), "spawned": 0, "delay": float(marker.delay)}
		kept[id] = run
	_runs = kept


func next_id() -> int:
	var top := 0
	for marker in markers:
		top = maxi(top, int(marker.id))
	return top + 1


func context() -> Dictionary:
	if _context.is_empty() or not _context.get("ok", false):
		_context = world.get_start_positions()
	return _context


## the start position entry of Player_N_Start ({} when the map has none)
func start_entry(start: int) -> Dictionary:
	for entry in context().get("starts", []):
		if int(entry.start) == start:
			return entry
	return {}


## the start nearest a point (0 when the map has no start waypoints)
func nearest_start(p: Vector2) -> int:
	var best := 0
	var distance := INF
	for entry in context().get("starts", []):
		var d := p.distance_to(Vector2(entry.x, entry.y))
		if d < distance:
			distance = d
			best = int(entry.start)
	return best


## the player index a marker's waves belong to, or the reason it has none (a String)
func owner_of(marker: Dictionary) -> Variant:
	var entry := start_entry(int(marker.start))
	if entry.is_empty():
		return "This map has no Player_%d_Start" % int(marker.start)
	if int(entry.player) < 0:
		return "No player at start %d. Put an AI on that spot in the lobby." % int(marker.start)
	if entry.get("local", false):
		return "Start %d is your own start; choose another." % int(marker.start)
	return int(entry.player)


func describe(marker: Dictionary) -> String:
	var owner = owner_of(marker)
	if owner is String:
		return owner
	var entry := start_entry(int(marker.start))
	return "Start %d · %s%s" % [int(marker.start), str(entry.get("name", "")), " (AI)" if entry.get("ai", false) else ""]


func is_outside(p: Vector2) -> bool:
	var area: Array = context().get("playable", [])
	return area.size() == 4 and not Rect2(area[0], area[1], area[2] - area[0], area[3] - area[1]).has_point(p)


func _process(_delta: float) -> void:
	if markers.is_empty() or world == null or not world.is_logic_held() or world.is_paused():
		return
	var frame: int = world.get_frame()
	for marker in markers:
		var run: Dictionary = _runs.get(int(marker.id), {})
		if run.is_empty() or frame < int(run.next):
			continue
		if int(marker.waves) > 0 and int(run.spawned) >= int(marker.waves):
			continue
		run.next = frame + maxi(1, int(float(marker.interval) * FRAMES_PER_SECOND))
		run.spawned = int(run.spawned) + 1
		spawn_wave(marker)
	if frame >= _next_steer:
		_next_steer = frame + int(STEER_SECONDS * FRAMES_PER_SECOND)
		_steer()


## one wave of the marker now; the message says what happened
func spawn_wave(marker: Dictionary) -> String:
	var owner = owner_of(marker)
	if owner is String:
		wave_spawned.emit(int(marker.id), 0, owner)
		return owner
	var origin := Vector2(marker.x, marker.y)
	var target := _target(origin)
	var facing := (Vector2(target.x, target.y) - origin).normalized() if not target.is_empty() else Vector2.RIGHT
	if facing == Vector2.ZERO:
		facing = Vector2.RIGHT
	var templates: Array[String] = []
	for unit in marker.units:
		for i in clampi(int(unit.count), 0, MAX_PER_WAVE):
			templates.append(str(unit.template))
	templates = templates.slice(0, MAX_PER_WAVE)
	if templates.is_empty():
		wave_spawned.emit(int(marker.id), 0, "The wave has no units.")
		return "The wave has no units."
	# rows across the line of march, the first row nearest the target
	var columns := ceili(sqrt(float(templates.size())))
	var side := facing.orthogonal()
	var ids: Array = []
	var failed := PackedStringArray()
	for i in templates.size():
		var row := i / columns
		var column := i % columns
		var p := origin + side * (float(column) - float(columns - 1) * 0.5) * SPACING - facing * float(row) * SPACING
		var id: int = world.create_object(templates[i], owner, p.x, p.y, facing.angle())
		if id > 0:
			ids.append(id)
		elif templates[i] not in failed:
			failed.append(templates[i])
	if not ids.is_empty():
		world.reserve_from_ai(ids) # the owner's skirmish AI would recruit the wave into its own teams
		var group := {"marker": int(marker.id), "ids": ids, "target": 0, "seen": {}}
		_groups.append(group)
		_order(group, target)
	var message := "Wave of %d from start %d" % [ids.size(), int(marker.start)]
	if not failed.is_empty():
		message += "; could not create " + ", ".join(failed)
	wave_spawned.emit(int(marker.id), ids.size(), message)
	return message


## the local player's object nearest `from`, structures first: { id, x, y } or {} when the player has nothing left
func _target(from: Vector2) -> Dictionary:
	var best := {}
	var best_score := INF
	for o in world.get_player_objects(str(context().get("local_name", ""))):
		var score := from.distance_squared_to(Vector2(o.x, o.y))
		if not o.structure:
			score *= 4.0 # units only when they are much nearer than any building
		if score < best_score:
			best_score = score
			best = {"id": int(o.id), "x": float(o.x), "y": float(o.y)}
	return best


func _order(group: Dictionary, target: Dictionary) -> void:
	if target.is_empty() or group.ids.is_empty():
		return
	group.target = target.id
	world.order_move(group.ids, target.x, target.y, {"type": "attack"})


## drops the dead, sends a group at a new target when its target died, and re-orders members that stopped short of it
func _steer() -> void:
	var local_ids := {}
	for o in world.get_player_objects(str(context().get("local_name", ""))):
		local_ids[int(o.id)] = Vector2(o.x, o.y)
	for group in _groups.duplicate():
		var alive: Array = []
		var positions := {}
		for id in group.ids:
			var o: Dictionary = world.get_object(int(id))
			if o.get("ok", false) and not (float(o.get("max_health", 0.0)) > 0.0 and float(o.get("health", 0.0)) <= 0.0):
				alive.append(id)
				positions[int(id)] = Vector2(o.x, o.y)
		group.ids = alive
		if alive.is_empty():
			_groups.erase(group)
			continue
		var centre := Vector2.ZERO
		for p in positions.values():
			centre += p
		centre /= positions.size()
		if not local_ids.has(int(group.target)):
			_order(group, _target(centre))
		else:
			var goal: Vector2 = local_ids[int(group.target)]
			var idle: Array = []
			for id in alive:
				var p: Vector2 = positions[int(id)]
				if group.seen.has(int(id)) and p.distance_to(group.seen[int(id)]) < 10.0 and p.distance_to(goal) > 300.0:
					idle.append(id)
			if not idle.is_empty():
				world.order_move(idle, goal.x, goal.y, {"type": "attack"})
		group.seen = positions
