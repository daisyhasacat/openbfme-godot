## Keep simulation time at zero while servicing the existing LOADCOMPLETE handshake.
extends RefCounted

static func wait(tree: SceneTree, world, timeout_ms: int = 180000) -> Dictionary:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() <= deadline:
		world.advance(0.0)
		var status: Dictionary = world.net_status()
		if status.get("loaded", false):
			return {"ok": true}
		await tree.process_frame
	return {"ok": false, "error": "Timed out waiting for all players to finish loading."}
