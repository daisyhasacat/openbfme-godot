## OpenBFME. GPL-3.0. Invoked by tools/net/test_match_statistics.py.
extends SceneTree
const Reports = preload("res://scripts/match_statistics.gd")
var share := true
var failures := 0
func _init() -> void:
	_run.call_deferred()
func check(ok: bool, label: String) -> void:
	print("REPORT ", "PASS " if ok else "FAIL ", label)
	if not ok: failures += 1
func wait_for(test: Callable) -> bool:
	for i in 600:
		if test.call(): return true
		await create_timer(0.01).timeout
	return false
func _run() -> void:
	var base := OS.get_environment("REPORT_TEST_URL")
	var folder := OS.get_environment("REPORT_TEST_DIR")
	var node = Reports.new()
	node.path = folder.path_join("outbox.json")
	node.sharing = func(): return share
	node.retry_seconds = 0.01
	root.add_child(node)
	var context := {"id":"0123456789abcdef", "url":base+"/retry", "token":"test-secret"}
	check(node.enqueue(context,{"v":1}), "saved before upload")
	check(node.enqueue(context,{"v":1}) and node.pending.size()==1,"idempotent queue")
	check(not node.enqueue(context,{"v":2}),"different duplicate rejected")
	check(await wait_for(func(): return node.pending.is_empty()),"transient failure retried")
	check(node.requests_sent==2,"two attempts")
	context.url=base+"/redirect"
	check(node.enqueue(context,{"v":1}),"redirect queued")
	check(await wait_for(func(): return node._blocked.size()==1),"redirect refused")
	check(node.pending.size()==1,"failed report retained")
	node.free()
	var restored=Reports.new()
	restored.path=folder.path_join("outbox.json")
	restored.sharing=func(): return share
	root.add_child(restored)
	check(restored.pending.size()==1,"pending report survives restart")
	share=false
	check(await wait_for(func(): return restored.pending.is_empty()),"opt-out clears queued credentials")
	restored.free()
	quit(0 if failures==0 else 1)
