## MACFIX-1 (issue #46): the Game files download's progress in the real window (scripts/main.gd), driven the way a player does it (the
## Game files screen's Download, the window's worker thread, its own progress display), against tools/release/test_aio.py's stand-in
## service serving large made-up files at loopback speed (far faster than the 30-60 MB/s of the issue's Mac):
##
##   godot --headless --path launcher --script res://tests/aio_progress_test.gd -- --offline --aio-base=<stand-in> --aio-pins=<pins>
##       --progress-target=<folder> --progress-big=<the big files, "Folder/name" comma-separated>
##
## Every frame while the download runs it samples the bottom bar and the Game files panel. The window must keep drawing frames (no gap
## over MAX_GAP_MS: issue #46's main thread never left Godot's message queue, so the display froze on "Checking File 2 of 612"), both
## bars must only move forward and reach the total, and each big file must be shown mid-download (a "<name>: n%" line with 0 < n < 100,
## several distinct values) before the next one. Prints "AIO PROGRESS fail: ..." lines and "AIO PROGRESS OK" when every check passed;
## exit 0 only then. --progress-expect=paused: the download is paused by --aio-stop-after=<file>:pause (a partial run against the real
## service): it must end paused, the bars need not reach the total. Every 2 s an "AIO PROGRESS sample" line says what the window shows.
extends SceneTree

const MainScene := preload("res://scenes/main.tscn")

const MAX_GAP_MS := 1000
const JOB_TIMEOUT_MS := 240000

var _failures := PackedStringArray()
var _checks := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures.append(what)
		print("AIO PROGRESS fail: " + what)


func _frames(n: int) -> void:
	for i in n:
		await process_frame


func _run() -> void:
	var target := ""
	var big := PackedStringArray()
	var paused := false
	for a in OS.get_cmdline_user_args():
		if a == "--progress-expect=paused":
			paused = true
		if a.begins_with("--progress-target="):
			target = a.trim_prefix("--progress-target=")
		elif a.begins_with("--progress-big="):
			big = a.trim_prefix("--progress-big=").split(",", false)
	if target == "" or big.is_empty() or not "--offline" in OS.get_cmdline_user_args():
		_check(false, "start it with -- --offline --aio-base=... --aio-pins=... --progress-target=<folder> --progress-big=<files>")
		_end()
		return
	var main = MainScene.instantiate()
	root.add_child(main)
	await _frames(3)
	if main._games_screen == null or main.aio == null or main.aio.chosen().size() != 2:
		_check(false, "the window did not start with the test pins (two base packages)")
		_end()
		return
	main._show_screen("games")
	main._games_screen.set_folder(target)
	await _frames(2)
	main._games_screen._go.pressed.emit()

	var t0 := Time.get_ticks_msec()
	var last := t0
	var max_gap := 0
	var frames := 0
	var bar_last := -1.0
	var panel_last := -1.0
	var bar_back := ""
	var panel_back := ""
	var bar_max := 0.0
	var total := 0.0
	var seen := {}        # big file -> the distinct mid-download percentages shown for it
	var first_seen := {}  # big file -> the frame it was first shown
	var started := false
	var next_sample := t0
	while Time.get_ticks_msec() - t0 < JOB_TIMEOUT_MS:
		await process_frame
		var now := Time.get_ticks_msec()
		if started:
			max_gap = maxi(max_gap, now - last)
		last = now
		if not main._busy() and main._job_kind == "":
			break
		started = true
		frames += 1
		var title: String = main._bar_title.text
		var bar: float = main._bar.value
		var panel: float = main._games_screen._bar.value
		if main._bar.max_value > 1.0:
			total = main._bar.max_value
		if bar < bar_last and bar_back == "":
			bar_back = "%d after %d (%s)" % [bar, bar_last, title]
		if panel < panel_last and panel_back == "":
			panel_back = "%d after %d" % [panel, panel_last]
		bar_last = maxf(bar_last, bar)
		panel_last = maxf(panel_last, panel)
		bar_max = maxf(bar_max, bar)
		if now >= next_sample:
			next_sample = now + 2000
			print("AIO PROGRESS sample %5.1f s: %s  ·  %s" % [(now - t0) / 1000.0, title, main._bar_label.text])
		for name in big:
			var at := title.find(", %s: " % name)
			if at < 0:
				continue
			var pct := int(title.substr(at + name.length() + 4).trim_suffix("%"))
			if pct > 0 and pct < 100:
				if not seen.has(name):
					seen[name] = {}
					first_seen[name] = frames
				seen[name][pct] = true
	var finished: bool = not main._busy() and main._job_kind == ""
	_check(finished, "the download ended within %d s" % (JOB_TIMEOUT_MS / 1000))
	print("AIO PROGRESS frames %d, longest gap %d ms, bar %d of %d, files shown mid-download: %s" % [frames, max_gap, bar_max, total,
		str(seen.keys().map(func(k): return "%s x%d" % [k, seen[k].size()]))])
	_check(frames >= 20, "the window drew only %d frames during the download" % frames)
	_check(max_gap <= MAX_GAP_MS, "the window drew no frame for %d ms during the download (at most %d)" % [max_gap, MAX_GAP_MS])
	_check(bar_back == "", "the bottom bar moved back: " + bar_back)
	_check(panel_back == "", "the Game files bar moved back: " + panel_back)
	if not paused:
		_check(total > 0.0 and bar_max >= total, "the bottom bar reached %d of %d" % [bar_max, total])
	var order := []
	for name in big:
		var n: int = seen.get(name, {}).size()
		_check(n >= 3, "%s was shown mid-download at %d distinct percentages (at least 3)" % [name, n])
		if first_seen.has(name):
			order.append(first_seen[name])
	var sorted := order.duplicate()
	sorted.sort()
	_check(order == sorted, "the big files were shown in download order (first frames %s)" % str(order))
	if paused:
		_check(main._paused == "aio", "the download ended paused (%s): %s" % [main._paused, main._status.text])
	else:
		_check(main._status.text.contains("The games are downloaded and checked"), "status: " + main._status.text)
	main.queue_free()
	await _frames(2)
	_end()


func _end() -> void:
	if _checks == 0:
		_failures.append("no check ran")
	print("AIO PROGRESS OK (%d checks)" % _checks if _failures.is_empty() else "AIO PROGRESS FAILED: %d of %d checks" % [_failures.size(), _checks])
	quit(0 if _failures.is_empty() else 1)
