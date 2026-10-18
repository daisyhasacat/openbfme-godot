## OpenBFME. GPL-3.0.
## Lane ONLINEUX-1 r2 (Claude F3, Sol 2): scripts/rejoin_watch.gd, the rejoin catch-up's watchdog game.gd feeds with net_status's "rejoin"
## (tools/net/test_rejoin_watchdog.py runs it). Two attempts in one run are each watched: a failure, a stall and going live are said once per attempt, and a
## new attempt (net_status "attempt") is watched afresh, also when it fails at once. One "WATCH ok|FAIL" line per check; exit 0 when all hold.
extends SceneTree

const RejoinWatch := preload("res://scripts/rejoin_watch.gd")

var _ok := true


func _init() -> void:
	_run.call_deferred()


func _check(what: String, cond: bool, detail := "") -> void:
	print("WATCH %s %s%s" % ["ok  " if cond else "FAIL", what, ": " + detail if not cond or detail != "" else ""])
	_ok = _ok and cond


func _st(state: String, attempt: int, frame := 0) -> Dictionary:
	return {"state": state, "attempt": attempt, "frame": frame, "failure": "x"}


func _run() -> void:
	var w = RejoinWatch.new()
	w.stall_ms = 1000
	# attempt 1: catches up, then fails (once)
	_check("attempt 1 begins", w.step(_st("catching_up", 100, 0), 0) == "began")
	_check("attempt 1 moves", w.step(_st("catching_up", 100, 5), 500) == "")
	_check("attempt 1 fails", w.step(_st("failed", 100, 5), 600) == "failed")
	_check("attempt 1's failure is said once", w.step(_st("failed", 100, 5), 700) == "")
	# attempt 2 in the same run fails at once (no catch-up seen): said (before r2 the spent watch said nothing: the game hung on the map)
	_check("attempt 2 fails at once", w.step(_st("failed", 200), 800) == "failed")
	# attempt 3: catches up, goes live; attempt 4 then stalls on its first frame
	_check("attempt 3 begins", w.step(_st("catching_up", 300, 0), 1000) == "began")
	_check("attempt 3 goes live", w.step(_st("live", 300), 1200) == "live")
	_check("attempt 4 begins", w.step(_st("catching_up", 400, 0), 2000) == "began")
	_check("attempt 4 within the stall time", w.step(_st("catching_up", 400, 0), 2900) == "")
	_check("attempt 4 stalls (the same first frame: before r2 a live first attempt's frame and time could trip at once or never)",
		w.step(_st("catching_up", 400, 0), 3100) == "stalled")
	_check("attempt 4's stall is said once", w.step(_st("catching_up", 400, 0), 5000) == "")
	# a frame that moves keeps the catch-up alive
	_check("attempt 5 begins", w.step(_st("catching_up", 500, 0), 6000) == "began")
	var alive := true
	for i in range(1, 10):
		alive = alive and w.step(_st("catching_up", 500, i * 10), 6000 + i * 900) == ""
	_check("attempt 5 moving every 0.9 s never stalls", alive)
	# reset (game.gd: every new game) starts afresh too, even for the same attempt value
	w.reset()
	_check("after a reset a failure is said", w.step(_st("failed", 500), 20000) == "failed")
	_check("no rejoin status, nothing", w.step({}, 21000) == "")
	quit(0 if _ok else 1)
