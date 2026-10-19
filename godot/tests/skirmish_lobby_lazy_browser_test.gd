## No retail data needed: opening the lobby must not prepare a hidden 3D map.
extends SceneTree

class FakeShell extends Node2D:
	var active := true
	var applied := {}
	func shell_stack() -> PackedStringArray:
		return PackedStringArray(["Skirmish.apt" if active else "MainMenu.apt"])
	func lobby_gadget_rect(_name: String) -> Dictionary:
		return {"found": true, "enabled": true, "x": 20, "y": 200, "w": 400, "h": 300}
	func lobby_state() -> Dictionary:
		return {"map_search": ""}
	func lobby_apply(spec: Dictionary) -> Dictionary:
		applied = spec
		return {"ok": true}

class FakeBrowser extends CanvasLayer:
	var open := false
	var shown := 0
	var prepared := 0
	func show_browser() -> void:
		shown += 1
		open = true
	func prepare() -> void:
		prepared += 1

class SearchSpy extends "res://scripts/skirmish_map_search.gd":
	var creations := 0
	func _make_browser() -> void:
		if not is_instance_valid(browser):
			creations += 1
			browser = FakeBrowser.new()
			add_child(browser)

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok" if ok else "FAIL", what])
	if not ok:
		failures += 1

func _run() -> void:
	var shell := FakeShell.new()
	root.add_child(shell)
	var search := SearchSpy.new()
	search.shell = shell
	root.add_child(search)
	search.set_process(false)
	for _frame in 60:
		search._process(1.0 / 60.0)
	_check(search.creations == 0, "lobby animation and idle create no hidden browser")
	_check(search.browser == null, "no terrain or object builder exists before Explore maps")
	_check(search.field.visible and search.browse.visible, "search and Explore maps remain available")
	shell.active = false
	search._process(1.0 / 60.0)
	_check(search.field.visible == false, "leaving the lobby hides its search")
	shell.active = true
	for _frame in 60:
		search._process(1.0 / 60.0)
	_check(search.creations == 0, "re-entering the lobby does not schedule a hidden preload")
	search._search("fords")
	_check(shell.applied == {"map_search": "fords"}, "native map filtering still receives the search")
	search._open_browser()
	_check(search.creations == 1 and search.browser.shown == 1, "Explore maps creates and opens the browser on demand")
	for _frame in 60:
		search._process(1.0 / 60.0)
	_check(search.browser.prepared == 0, "idle lobby frames never call preview preparation")
	search.browser.open = false
	search._open_browser()
	_check(search.creations == 1 and search.browser.shown == 2, "reopening reuses the existing browser")
	search.queue_free()
	shell.queue_free()
	print("SKIRMISH LAZY BROWSER %s" % ["PASS" if failures == 0 else "FAIL"])
	quit(0 if failures == 0 else 1)
