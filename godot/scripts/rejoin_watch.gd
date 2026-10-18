## OpenBFME. GPL-3.0.
## Lane ONLINEUX-1 r2 (Claude F3, Sol 2): the watchdog of a rejoin's catch-up (game.gd _net_pause_tick feeds it net_status's "rejoin"). It says when the
## catch-up began, when it went live, when it failed (net_status "failed") and when it stalled (no new frame for stall_ms: the relay has no log to catch up
## from any more). Each attempt has its own state: net_status's "attempt" (NetGameSession::RejoinReport::startedMs, new for every beginRejoin) starts it
## afresh, so a second rejoin in one run is watched as the first was (before r2 a failed or live first attempt left the watch spent: a second failure
## hung on the map). OpenBFME's: retail has no rejoin.
extends RefCounted

var stall_ms := 45000
var since := 0       ## when the catch-up began (ms); 0 = not yet; -1 = this attempt is over (live or failed)
var frame_seen := -1 ## the catch-up's last frame, and when it last moved
var moved_ms := 0
var attempt := -1    ## net_status's "attempt" of the attempt watched


func reset() -> void:
	since = 0
	frame_seen = -1
	moved_ms = 0
	attempt = -1


## one status: "" (nothing to do), "began", "live", "failed" or "stalled" (the last two: the caller gives the rejoin up, once per attempt)
func step(rejoin: Dictionary, now_ms: int) -> String:
	if rejoin.is_empty():
		return ""
	var a := int(rejoin.get("attempt", -1))
	if a != attempt:
		reset()
		attempt = a
	var state := str(rejoin.get("state", ""))
	if state == "catching_up":
		var began := false
		if since == 0:
			since = maxi(now_ms, 1) # (0 means "not yet")
			began = true
		elif since < 0:
			return ""
		if int(rejoin.get("frame", 0)) != frame_seen:
			frame_seen = int(rejoin.get("frame", 0))
			moved_ms = now_ms
		elif now_ms - moved_ms > stall_ms:
			since = -1
			return "stalled"
		return "began" if began else ""
	if state == "failed" and since >= 0:
		since = -1
		return "failed"
	if state == "live" and since > 0:
		since = -1
		return "live"
	return ""
