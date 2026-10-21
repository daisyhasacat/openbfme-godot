extends SceneTree
const Barrier = preload("res://scripts/multiplayer_load_barrier.gd")
class Peer:
	extends RefCounted
	var loaded := false
	var pumps := 0
	var simulation_seconds := 0.0
	func advance(seconds: float) -> void:
		pumps += 1
		simulation_seconds += seconds
	func net_status() -> Dictionary:
		return {"loaded": loaded}
func _initialize() -> void:
	run.call_deferred()
func run() -> void:
	var peer := Peer.new()
	release_late_peer(peer)
	var result: Dictionary = await Barrier.wait(self, peer)
	assert(result == {"ok": true})
	assert(peer.pumps == 4)
	assert(peer.simulation_seconds == 0.0)
	var missing := Peer.new()
	var failure: Dictionary = await Barrier.wait(self, missing, 0)
	assert(failure == {"ok": false, "error": "Timed out waiting for all players to finish loading."})
	assert(missing.simulation_seconds == 0.0)
	print("MULTIPLAYER LOAD BARRIER PASS: delayed peer, zero simulation time, timeout")
	quit()
func release_late_peer(peer: Peer) -> void:
	for i in 3:
		await process_frame
		assert(peer.loaded == false)
		assert(peer.simulation_seconds == 0.0)
	peer.loaded = true
