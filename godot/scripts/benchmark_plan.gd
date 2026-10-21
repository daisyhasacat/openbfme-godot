## Repeatable workload planning; CP costs always come from the mounted templates.
extends RefCounted

static func army(target: int, templates: Array) -> Dictionary:
	if target < 0 or target > 20000 or templates.is_empty():
		return {"ok": false, "error": "Invalid CP target or empty unit roster."}
	# Reachability makes each round-robin choice leave an exactly fillable remainder.
	var reachable := PackedByteArray()
	reachable.resize(target + 1)
	reachable[0] = 1
	for entry in templates:
		if int(entry.get("cp", 0)) <= 0:
			return {"ok": false, "error": "Unit %s has no positive CommandPoints value." % entry.get("name", "?")}
	for cp in range(1, target + 1):
		for entry in templates:
			if cp >= int(entry.cp) and reachable[cp - int(entry.cp)]:
				reachable[cp] = 1
				break
	if not reachable[target]:
		return {"ok": false, "error": "The mounted Men units cannot total exactly %d CP." % target}
	var remaining := target
	var units: Array[String] = []
	var counts := {}
	var next := 0
	while remaining > 0:
		for offset in templates.size():
			var index := (next + offset) % templates.size()
			var entry: Dictionary = templates[index]
			if remaining >= int(entry.cp) and reachable[remaining - int(entry.cp)]:
				units.append(entry.name)
				counts[entry.name] = int(counts.get(entry.name, 0)) + 1
				remaining -= int(entry.cp)
				next = (index + 1) % templates.size()
				break
	return {"ok": true, "cp": target, "units": units, "counts": counts}

static func frame_summary(samples: Array) -> Dictionary:
	if samples.is_empty(): return {}
	var intervals: Array[float] = []
	var total := 0.0
	for sample in samples:
		intervals.append(float(sample.frame))
		total += float(sample.frame)
	intervals.sort()
	return {"fps": 1000.0 * samples.size() / total if total > 0 else 0.0,
		"frame_p95_ms": intervals[ceili(samples.size() * 0.95) - 1],
		"frame_p99_ms": intervals[ceili(samples.size() * 0.99) - 1],
		"seconds": total / 1000.0, "frames": samples.size()}
