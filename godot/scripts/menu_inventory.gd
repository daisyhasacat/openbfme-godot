## Lane MENUWORK-1: the menu inventory. Every clickable control of every menu screen the main menu leads to, pressed with real mouse events, and what
## the press did, as evidence for tools/menus/menu_inventory.py (which adds retail's behaviour and writes the table).
## game.gd runs it with --menu-inventory=DIR: DIR/inventory.json (one record per control) and, in a windowed run, DIR/<screen>.png per screen.
##
## The walk is breadth first over screens: a screen is a route (presses from the main menu) and is entered fresh from the main menu (_reset). Every
## hittable Apt button and every shown, enabled gadget window of the screen is pressed in turn; the effect is read from what the press changed:
##   screen     the shell's stack changed (a screen opened or closed)
##   box        a message box came up (its text is kept: "not available" boxes are the labelled unavailable entries)
##   request    the menu asked the host for something (GAME shell request; the requests that start a game or quit are held, TEST HOOK)
##   page       the screen's hittable buttons changed (a tab, a pop-up, a nav opened)
##   state      a gadget's value, a drawn text or a picture changed
##   nothing    none of these: the defect the inventory is for
## A press that leads somewhere new (screen or page) adds that place to the queue (depth <= MAX_DEPTH; the online pages deeper), so sub-pages are
## walked too. Each record keeps the evidence the tool checks the control's intended effect against: the stack before / after, the buttons that came
## and went, the pressed gadget's own state before / after (value, opened list, selected row, first row), the top screen's engine state before / after
## (AptMenuPlayer.screen_state, the lobby, the online status, Create-a-Hero's view and camera, the quit menu) and the commands the movie ran.
## With OPENBFME_LOBBY set (tools/menus/menu_inventory.py starts a local lobby) the online pages past the login are walked: a nickname is typed on
## the login page before its Log In.
## The in-game half (the quit menu and the score screen) runs after the front end: a skirmish is started from the lobby's Start.
## Lines "INVENTORY ..." per press; "INVENTORY RESULT <n> controls, <m> nothing" at the end; exit 0 when the walk finished (verdicts are the tool's).
extends RefCounted

const MAX_DEPTH := 3
const MAX_ONLINE_DEPTH := 7 # the online pages lie deeper (login, home, open play, the staging room and its tabs)
const MAX_SCREENS := 64
const NICKNAME := "Walker"
const REST := Vector2(512, 200) # the pointer's resting place between presses (no control there on any front-end screen walked)
const SETTLE := 90 # frames after a press (1.5 s at 60 Hz: the shell's open / close animations end in about 1 s)
# requests the inventory records but the host does not carry out: they quit or start a game (each is covered by its own test: the campaign, LAN,
# online and replay starts by their lanes' tests); the effect is recorded as the request
const HOLD := ["ExitGame", "Expansion1Campaign", "BonusCampaign", "ContinueCampaign", "Tutorial", "LanGameStart", "OnlineGameStart",
	"LoadGameFile", "LoadReplayFile"]
# buttons not pressed in the front-end walk, by path part: the lobby's Start is the in-game half's entry
const NOT_PRESSED := {"Skirmish.apt": ["Start", "StartGame"], "MainMenu.apt": ["QuitMainMenu"]}
# the way back a player takes from a screen, by button name (tried before the shell is popped)
const BACK_NAMES := ["Cancel", "Back", "MainMenu", "Exit", "BackButton", "BackBttn", "Done"]

var game: Node
var shell: Object
var dir := ""
var records: Array = []
var queue: Array = [] # [{"route": Array, "name": String, "depth": int}]
var seen := {} # signature -> screen name
var errors: Array = []
var places: Array = [] # every place walked: {name, stack, online_page}
var logins := 0 # every online entry signs in with a name of its own (a kept session of one name would offer its game's rejoin)


func run(p_game: Node, p_dir: String) -> void:
	game = p_game
	shell = game._shell
	dir = p_dir if not p_dir.is_empty() else "user://menu_inventory"
	DirAccess.make_dir_recursive_absolute(dir)
	for h in HOLD:
		game.shell_request_hold.push_back(h)
	await game._step(game.LEVEL_MAIN_MENU_FRAMES + 90)
	await game._wait_main_ready()
	var dump := OS.get_environment("MENU_INVENTORY_DUMP") # a route of button paths ("a;b"): entered, its tree and gadgets printed, then quit
	if not dump.is_empty():
		var route: Array = []
		for id in dump.split(";"):
			route.push_back({"id": id, "kind": "hover" if id.contains("Nav.instance") else "button"})
		print("INVENTORY DUMP entered: ", await _enter(route))
		await game._step(120)
		await _shot("dump")
		print(shell.dump_tree(shell.shell_top_level(), 12))
		for g in shell.list_gadgets():
			print("INVENTORY DUMP gadget ", g)
		for t in shell.drawn_texts(false):
			print("INVENTORY DUMP text ", t)
		for p in OS.get_environment("MENU_INVENTORY_DUMP_TEXTS").split(";", false):
			print("INVENTORY DUMP field %s text=%s variable=%s" % [p, shell.get_member(shell.shell_top_level(), p, "text"), shell.get_member(shell.shell_top_level(), p, "variable")])
		for b in shell.list_buttons(shell.shell_top_level()):
			print("INVENTORY DUMP button ", b)
		var click := OS.get_environment("MENU_INVENTORY_DUMP_CLICK") # a gadget name: clicked, then the gadgets and commands printed again
		if not click.is_empty():
			shell.take_commands()
			for g in shell.list_gadgets():
				if g.name == click and not g.hidden:
					print("INVENTORY DUMP click target ", g)
					for fy in [0.03, 0.1, 0.2, 0.3, 0.5]:
						await _click_at(Vector2(g.x + g.w * 0.5, g.y + g.h * fy))
						await game._step(40)
						for h in shell.list_gadgets():
							if h.name == click:
								print("INVENTORY DUMP at %.2f: selected %s open %s, commands %s" % [fy, h.get("selected"), h.get("open"), shell.take_commands()])
						for b in shell.list_buttons(shell.shell_top_level()):
							if b.hittable and absf(b.y - (g.y + g.h * fy)) < 25:
								print("INVENTORY DUMP   button near: ", b)
					print("INVENTORY DUMP clicked %s at %s: commands %s" % [click, Vector2(g.x + g.w * 0.5, g.y + g.h * 0.5), shell.take_commands()])
					for h in shell.list_gadgets():
						if h.name == click:
							print("INVENTORY DUMP after ", h)
					break
		game.get_tree().quit(0)
		return
	queue.push_back({"route": [], "name": "MainMenu", "depth": 0})
	var only := OS.get_environment("MENU_INVENTORY_ONLY") # one place by its name (and the places on its way): a short run (tests, debugging)
	while not queue.is_empty() and seen.size() <= MAX_SCREENS and only != "game":
		var s: Dictionary = queue.pop_front()
		if not only.is_empty() and s.depth > 0 and only != String(s.name) and not only.begins_with(String(s.name) + " > "):
			continue
		await _walk_screen(s)
	if OS.get_environment("MENU_INVENTORY_NO_GAME").is_empty() and (only.is_empty() or only == "game"):
		await _walk_in_game()
	# the main menu's Quit last: it ends the run (its request is held, the record is written first)
	if only.is_empty() or only == "game":
		await _reset()
		var quit := _find_named("QuitMainMenu")
		if quit.is_empty():
			errors.push_back("the main menu's Quit was not found at the end")
		else:
			await _press({"name": "MainMenu", "route": [], "depth": MAX_DEPTH}, {"id": quit.path, "kind": "button", "skip": false})
	_write()
	game.get_tree().quit(0)


func _write() -> void:
	var out := {"records": records, "errors": errors, "screens": seen.values(), "places": places, "finished": true,
		"online_lobby": OS.get_environment("OPENBFME_LOBBY")}
	var f := FileAccess.open(dir.path_join("inventory.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(out, "\t"))
	f.close()
	var nothing := 0
	for r in records:
		nothing += 1 if r.effect == "nothing" else 0
	print("INVENTORY RESULT %d controls, %d nothing, %d errors -> %s" % [records.size(), nothing, errors.size(), dir.path_join("inventory.json")])


# ---- the screens -----------------------------------------------------------------------------------------------------------------------------------

func _walk_screen(s: Dictionary) -> void:
	if not await _enter(s.route):
		errors.push_back("%s: the route %s could not be replayed" % [s.name, str(s.route)])
		print("INVENTORY ERROR %s: the route could not be replayed" % s.name)
		await _reset()
		return
	await _settle()
	var before := _fingerprint()
	var sig := _signature(before)
	if seen.has(sig):
		print("INVENTORY %s is %s (already walked)" % [s.name, seen[sig]])
		return
	seen[sig] = s.name
	places.push_back({"name": s.name, "stack": before.stack, "online_page": str(before.probe.get("online_page", "")),
		"online_state": int(before.probe.get("online_state", -1))})
	print("INVENTORY screen %s: stack %s, route %s" % [s.name, str(before.stack), str(s.route)])
	await _shot(s.name)
	var controls := _controls(before)
	# the ways out of the place after its other controls (a re-entry then finds the place as the walk found it, not as an earlier press left it),
	# and the controls that change what the screen shows next time (the Create-a-Hero promo's "don't show again") last
	controls.sort_custom(func(x, y): return _press_rank(x.id) < _press_rank(y.id))
	var dirty := false
	for c in controls:
		if dirty:
			if not await _enter(s.route):
				errors.push_back("%s: the route could not be replayed before %s" % [s.name, c.id])
				break
			await _settle()
		dirty = await _press(s, c)
		if records[-1].effect == "gone" and await _enter(s.route):
			# once more from a fresh entry (a press before it may have left the place in another state)
			records.pop_back()
			if not _hittable(c.id) and String(c.id).contains("DescreetSliderButton"):
				# the step holding the slider's current value is no button (the movie's DescreetSlider): another step first, as a player does
				await _press_sibling_step(c.id)
			dirty = await _press(s, c)
	await _reset()


# the controls of the place: the hittable Apt buttons of every level (the top screen's and its boxes') and the shown, enabled gadgets
func _controls(fp: Dictionary) -> Array:
	var out: Array = []
	var top := String(fp.stack[-1]) if fp.stack.size() > 0 else ""
	for path in fp.buttons:
		var skip := false
		for word in NOT_PRESSED.get(top, []):
			skip = skip or _leaf(path) == word or path.contains("." + word + ".")
		out.push_back({"id": path, "kind": "button", "skip": skip})
	for name in fp.gadget_rects:
		out.push_back({"id": name, "kind": fp.gadget_kinds[name], "skip": false})
	return out


func _press(s: Dictionary, c: Dictionary) -> bool:
	var rec := {"screen": s.name, "route": s.route, "control": c.id, "kind": c.kind, "effect": "", "detail": "", "commands": [], "requests": [],
		"box": "", "new_buttons": [], "gadgets": [], "hover": ""}
	if c.skip:
		rec.effect = "not-pressed"
		rec.detail = "quits the game: pressed at the end of the walk" if c.id.contains("Quit") else "starts the game: pressed by the in-game half"
		records.push_back(rec)
		return false
	var at := _control_point(c)
	for wait in 12:
		# a place still filling (the staging room's rows come as the lobby answers)
		if at.x >= 0:
			break
		await game._step(30)
		at = _control_point(c)
	if at.x < 0:
		rec.effect = "gone"
		rec.detail = "the control was not found again"
		var where := PackedStringArray()
		for level in 16:
			if shell.has_level(level):
				for b in shell.list_buttons(level):
					where.push_back("%s%s" % [b.path, "" if b.hittable else " (covered)"])
		print("INVENTORY %s | %s | gone; the buttons now: %s; stack %s" % [s.name, c.id, ", ".join(where), str(shell.shell_stack())])
		records.push_back(rec)
		return true
	if _resets(c.id):
		# a reset to the defaults proves itself only when something differs from them: one value is changed first, as a player would
		await _change_one_value()
		at = _control_point(c)
	shell.take_commands()
	shell.take_events()
	var requests_before: int = game.shell_requests.size()
	# the pointer onto the control first (hover effects are not the press's), then the press
	var h := _fingerprint()
	game._mouse_event(at, false, false)
	await game._step(40)
	var a := _fingerprint()
	rec.hover_commands = Array(shell.take_commands()) # what the pointer alone ran (a power's description ...): not the press's
	rec.stack_top = String(a.stack[-1]) if a.stack.size() > 0 else ""
	rec.online_page = str(a.probe.get("online_page", ""))
	rec.online_state = int(a.probe.get("online_state", -1))
	rec.entries = a.gadget_entries.get(c.id, -1)
	rec.own_before = a.gadget_full.get(c.id, {})
	if c.kind == "button" and str(a.buttons) != str(h.buttons) and str(a.stack) == str(h.stack) and s.depth < MAX_DEPTH:
		# a place opened by the pointer alone (the main menu's navs): walked as a place of its own, its route ends with the pointer there
		var hsig := _signature(a)
		var opened := 0
		var owner: String = String(c.id).substr(0, String(c.id).rfind(".") + 1) # the nav clip ("_level2.SoloPlayNav.") of its open button
		for p in a.buttons:
			opened += 0 if h.buttons.has(p) or not String(p).begins_with(owner) else 1
		print("INVENTORY %s | %s | the pointer opens %d buttons" % [s.name, c.id, opened])
		rec.hover = "opens %d buttons under the pointer" % opened
		if opened > 0 and not seen.has(hsig) and not _queued(hsig):
			var hroute: Array = s.route.duplicate()
			hroute.push_back({"id": c.id, "kind": "hover"})
			queue.push_back({"route": hroute, "name": "%s > %s (pointer)" % [s.name, _label(c.id)], "depth": s.depth + 1, "sig": hsig})
	if c.kind == "list":
		at = _list_row_point(c.id, a)
	game._mouse_event(at, true, true)
	await game._step(2)
	game._mouse_event(at, false, true)
	await game._step(SETTLE)
	if c.kind == "entry" or c.kind == "combo":
		# a text field: two letters typed into it
		_type_text("ab")
		await game._step(10)
	var b := _fingerprint()
	if _signature(b) == _signature(a) and str(b.probe) == str(a.probe) and str(b.gadget_full.get(c.id, {})) == str(rec.own_before):
		# nothing yet: an answer that takes a while (the online pages' requests to the lobby, a screen's opening animation) is waited for
		for wait in 8:
			await game._step(15)
			b = _fingerprint()
			if _signature(b) != _signature(a) or str(b.probe) != str(a.probe):
				break
	if game.shell_requests.size() > requests_before and str(a.stack) == str(b.stack):
		# the menu asked the host for something and the screen is still the same: the host's answer (a screen, a box) may take longer on a
		# fast or a busy machine; the press is judged once it had another two seconds
		for wait in 8:
			await game._step(15)
			b = _fingerprint()
			if str(a.stack) != str(b.stack):
				break
	rec.commands = Array(shell.take_commands())
	rec.stack_before = a.stack
	rec.stack_after = b.stack
	rec.own_after = b.gadget_full.get(c.id, {})
	var removed_buttons: Array = []
	for p in a.buttons:
		if not b.buttons.has(p):
			removed_buttons.push_back(p)
	rec.removed_buttons = removed_buttons
	var probe_changes: Array = []
	for k in b.probe:
		if str(a.probe.get(k, "")) != str(b.probe[k]):
			probe_changes.push_back("%s: %s -> %s" % [k, str(a.probe.get(k, "(none)")).left(120), str(b.probe[k]).left(120)])
	for k in a.probe:
		if not b.probe.has(k):
			probe_changes.push_back("%s: %s -> (none)" % [k, str(a.probe[k]).left(120)])
	rec.probe_changes = probe_changes
	for i in range(requests_before, game.shell_requests.size()):
		rec.requests.push_back(game.shell_requests[i][0])
	for e in shell.take_events():
		if e.kind == "url":
			rec.commands.push_back("getURL " + str(e.a))
	if String(c.id).contains("SuppressPromo"):
		# the check mark becomes the preference when the introduction is left (Continue sends the SuppressCAHPromo extern): that is its effect
		var cont := _find_named("Continue")
		if not cont.is_empty():
			await _click_at(Vector2(cont.x, cont.y))
			await game._step(SETTLE)
			var after := _probe()
			for k in after:
				if str(b.probe.get(k, "")) != str(after[k]) and k == "cah_suppress_promo":
					rec.probe_changes.push_back("%s: %s -> %s (after Continue)" % [k, str(a.probe.get(k, "")), str(after[k])])
	_classify(rec, a, b)
	records.push_back(rec)
	print("INVENTORY %s | %s | %s | %s | %s" % [s.name, c.id, rec.effect, rec.detail, ", ".join(PackedStringArray(rec.commands))])
	# a place reached by the press is walked later; a "page" that only swapped a button's look (a slider step, a selected tab) is no new place
	var place: bool = rec.effect == "screen" or rec.effect == "box" or (rec.effect == "page" and rec.new_buttons.size() >= 3)
	place = place and not String(c.id).contains("DescreetSlider") # an advanced option's step only swaps which step is shown
	var online: bool = b.stack.size() > 0 and String(b.stack[-1]) == "OnlineShell.apt"
	if place and s.depth < (MAX_ONLINE_DEPTH if online else MAX_DEPTH):
		var sig := _signature(b)
		if not seen.has(sig) and not _queued(sig):
			var name := "%s > %s" % [s.name, _label(c.id)]
			var route: Array = s.route.duplicate()
			route.push_back({"id": c.id, "kind": c.kind})
			queue.push_back({"route": route, "name": name, "depth": s.depth + 1, "sig": sig})
	# the place is entered again for the next control when the press left it (another screen, page or box); a value change stays
	var dirty: bool = _signature(b) != _signature(h)
	# a nav the pointer opened stays open over the place (a pointer that jumps away does not cross its closing surround): the place is entered again
	dirty = dirty or not String(rec.get("hover", "")).is_empty()
	if not String(rec.get("hover", "")).is_empty():
		# the pointer leaves the nav the way a mouse does, crossing its surround (whose roll-out closes the nav and sends EnableComponents)
		for k in range(1, 9):
			game._mouse_event(at.lerp(REST, k / 8.0), false, false)
			await game._step(4)
		await game._step(20)
	if c.kind == "combo":
		# an opened list closes with a second press on the combo (else the next press lands on the list); Esc when that did not close it
		# (a second press on an open combo leaves it open: a player clicks elsewhere, as the walk does at its resting spot)
		for attempt in 3:
			var now: Dictionary = _fingerprint().gadget_full.get(c.id, {})
			if now.get("open", false) != true:
				break
			if attempt == 2:
				errors.push_back("%s: the drop-down %s did not close" % [s.name, c.id])
				break
			await _click_at(REST if attempt == 0 else Vector2(REST.x, REST.y + 300))
			await game._step(20)
	return dirty


# the place holds still (its open animation ended, the lobby's rows came): the same buttons, gadgets and engine state for a third of a second
func _settle() -> void:
	var last := ""
	for attempt in 20:
		var fp := _fingerprint()
		var now := _signature(fp) + str(fp.gadget_state) + str(fp.probe)
		if now == last:
			return
		last = now
		await game._step(20)


func _manager_shown() -> bool:
	for level in 16:
		if shell.has_level(level):
			for b in shell.list_buttons(level):
				if b.hittable and String(b.path).contains("mcManagerScreen"):
					return true
	return false


func _resets(id: String) -> bool:
	var leaf := _label(id)
	return leaf.contains("Default") or leaf.contains(".Reset.") or leaf.begins_with("Reset")


# one value of the place changed (a check box toggled, a slider moved, an advanced option's step chosen)
func _change_one_value() -> void:
	for kind in ["checkbox", "slider"]:
		for g in shell.list_gadgets():
			if g.kind == kind and g.enabled and not g.hidden and String(g.parent).is_empty() and not String(g.name).is_empty() and _on_stage(g.x, g.y):
				var x: float = g.x + g.w * (0.5 if kind == "checkbox" else 0.1)
				await _click_at(Vector2(x, g.y + g.h * 0.5))
				await game._step(30)
				return
	for level in 16:
		if shell.has_level(level):
			for b in shell.list_buttons(level):
				if b.hittable and String(b.path).contains("DescreetSliderButton"):
					await _click_at(Vector2(b.x, b.y))
					await game._step(60)
					return


func _hittable(id: String) -> bool:
	return _control_point({"id": id, "kind": "button"}).x >= 0


func _press_sibling_step(id: String) -> void:
	var parent := id.substr(0, id.rfind("DescreetSliderButton"))
	for level in 16:
		if not shell.has_level(level):
			continue
		for b in shell.list_buttons(level):
			if b.hittable and String(b.path).begins_with(parent) and String(b.path) != id:
				await _click_at(Vector2(b.x, b.y))
				await game._step(SETTLE)
				return


func _press_rank(id: String) -> int:
	if _persists(id):
		return 2
	var leaf := _leaf(id).to_lower()
	for word in BACK_NAMES:
		if leaf.begins_with(String(word).to_lower()):
			return 1
	return 0


func _persists(id: String) -> bool:
	var leaf := _leaf(id).to_lower()
	return leaf.contains("suppress") or leaf.contains("dontshow") or leaf.contains("remember")


func _queued(sig: String) -> bool:
	for q in queue:
		if q.get("sig", "") == sig:
			return true
	return false


func _classify(rec: Dictionary, a: Dictionary, b: Dictionary) -> void:
	var new_buttons: Array = []
	for p in b.buttons:
		if not a.buttons.has(p):
			new_buttons.push_back(p)
	rec.new_buttons = new_buttons
	var box := ""
	for p in new_buttons:
		if String(p).contains("MessageBox") and box.is_empty():
			# GuiFX's MessageBox clip shows the records APT:_level<n>.MessageBox_Title / _Text (AptMessageBox, RW 0xDE8A9C)
			var level := String(p).get_slice(".", 0)
			if shell.has_method("apt_text"):
				box = "%s: %s" % [shell.apt_text("APT:%s.MessageBox_Title" % level), shell.apt_text("APT:%s.MessageBox_Text" % level)]
			else:
				box = ": "
			if box == ": ":
				box = "(a message box) " + _new_texts(a, b)
	for name in b.gadget_state:
		if a.gadget_state.get(name, "") != b.gadget_state[name]:
			rec.gadgets.push_back("%s: %s -> %s" % [name, a.gadget_state.get(name, "(none)"), b.gadget_state[name]])
	var held := false
	for r in rec.requests:
		held = held or HOLD.has(r)
	var removed := 0
	for p in a.buttons:
		removed += 0 if b.buttons.has(p) else 1
	# the menu's buttons went (its DisableAllButtons ran) and nothing opened: the host did not answer the request (a soft-lock); the requests the
	# inventory holds itself (a campaign start, quitting) are not the host's fault
	var appeared := 0
	for p in new_buttons:
		appeared += 0 if a.all_buttons.has(p) else 1
	# (at most one old button kept and at most one uncovered, the shell's back layer: closing a pop-up or a menu removes buttons too, but uncovers
	# the screen under it)
	var kept := 0
	for p in b.buttons:
		kept += 1 if a.buttons.has(p) else 0
	var uncovered: int = new_buttons.size() - appeared
	var softlock: bool = str(a.stack) == str(b.stack) and box.is_empty() and appeared == 0 and removed >= 3 and kept <= 1 and uncovered <= 1 and not held
	if softlock:
		rec.effect = "nothing"
		rec.detail = "the screen's buttons went (%d) and nothing opened: a soft-lock" % removed
	elif str(a.stack) != str(b.stack):
		rec.effect = "screen"
		rec.detail = "%s -> %s" % [str(a.stack[-1]) if a.stack.size() > 0 else "-", str(b.stack[-1]) if b.stack.size() > 0 else "-"]
	elif not box.is_empty():
		rec.effect = "box"
		rec.box = box
		rec.detail = box
	elif not rec.requests.is_empty():
		rec.effect = "request"
		rec.detail = ", ".join(PackedStringArray(rec.requests)) + (" (held by the inventory: it starts a game or quits)" if held else "")
	elif not new_buttons.is_empty() or a.buttons.size() != b.buttons.size():
		rec.effect = "page"
		rec.detail = "%d buttons -> %d" % [a.buttons.size(), b.buttons.size()]
	elif not rec.gadgets.is_empty():
		rec.effect = "state"
		rec.detail = "; ".join(PackedStringArray(rec.gadgets.slice(0, 3)))
	elif not rec.probe_changes.is_empty():
		rec.effect = "state"
		rec.detail = "; ".join(PackedStringArray(rec.probe_changes.slice(0, 3)))
	elif a.texts_hash != b.texts_hash:
		rec.effect = "state"
		rec.detail = "texts: " + _new_texts(a, b)
	elif a.ops_hash != b.ops_hash:
		rec.effect = "state"
		rec.detail = "the picture changed"
	else:
		rec.effect = "nothing"
	for line in b.notes:
		if not a.notes.has(line):
			rec.detail += " [note " + String(line).left(160) + "]"


func _new_texts(a: Dictionary, b: Dictionary) -> String:
	var out := PackedStringArray()
	for t in b.texts:
		if not a.texts.has(t) and not String(t).strip_edges().is_empty():
			out.push_back(String(t).replace("\n", " ").left(140))
	return " / ".join(out.slice(0, 4))


# ---- the in-game half ------------------------------------------------------------------------------------------------------------------------------

func _walk_in_game() -> void:
	# the lobby (Skirmish), its profile set, Start: a skirmish against one computer player
	if not await _to_lobby():
		errors.push_back("in-game: the lobby could not be reached")
		return
	var start := _find_named("StartGame")
	if start.is_empty():
		start = _find_named("Start")
	if start.is_empty():
		errors.push_back("in-game: the lobby has no Start button")
		return
	var rec := {"screen": "Skirmish lobby", "route": [], "control": start.path, "kind": "button", "effect": "", "detail": "", "commands": [],
		"requests": [], "box": "", "new_buttons": [], "gadgets": [], "stack_top": "Skirmish.apt", "stack_before": Array(shell.shell_stack()),
		"probe_changes": []}
	await game._click_button(shell.shell_top_level(), String(start.path).substr(String(start.path).find(".") + 1).trim_suffix(".bttn"))
	rec.commands = Array(shell.take_commands())
	for attempt in 120:
		await game._step(30)
		if game._state == game.State.PLAYING:
			break
	if game._state != game.State.PLAYING:
		rec.effect = "nothing"
		rec.detail = "no game started in 60 s"
		records.push_back(rec)
		return
	rec.effect = "screen"
	rec.stack_after = Array(shell.shell_stack())
	rec.detail = "the game starts (loading screen, then the game): %s" % str(rec.stack_after)
	records.push_back(rec)
	await game._step(120)
	# the quit menu's buttons (Esc opens it): its own buttons are the ones its opening adds; each but Restart and Exit is pressed with the menu opened
	# again, then Restart (the same game again), Exit last (the score screen)
	var closed := _fingerprint()
	await _open_quit_menu()
	var ids := await _quit_menu_buttons(closed)
	await _shot("QuitMenu")
	if ids.is_empty():
		errors.push_back("in-game: the quit menu shows no buttons of its own")
	var quit_screen := {"name": "Quit menu (in game)", "route": [], "depth": MAX_DEPTH}
	for id in ids:
		if String(id).containsn("Exit") or String(id).containsn("Restart"):
			continue
		await _open_quit_menu()
		await _quit_menu_buttons(closed)
		await _press(quit_screen, {"id": id, "kind": "button", "skip": false})
		await _close_game_overlays()
	# Restart and Exit ask first (the popup's Yes / No): No is pressed once (the question goes, the game goes on), then Yes
	for word in ["Restart", "Exit"]:
		for id in ids:
			if not String(id).containsn(word):
				continue
			var confirm := {"name": "Quit menu (in game) > %s" % _label(id), "route": [], "depth": MAX_DEPTH}
			for answer in ["No", "Yes"]:
				await _open_quit_menu()
				await _quit_menu_buttons(closed)
				var before := _fingerprint()
				await _press(quit_screen, {"id": id, "kind": "button", "skip": false})
				if answer == "No":
					pass
				else:
					records.pop_back() # the question itself was recorded with No
				var popup := ""
				for attempt in 10:
					for p in _fingerprint().buttons:
						if not before.buttons.has(p) and String(p).contains("." + answer + "."):
							popup = p
					if not popup.is_empty():
						break
					await game._step(15)
				if popup.is_empty():
					break # no question: the press itself did it
				await _press(confirm, {"id": popup, "kind": "button", "skip": false})
				await _close_game_overlays()
			if word == "Restart":
				for attempt in 60:
					await game._step(30)
					if game._state == game.State.PLAYING and game._world != null:
						break
				await game._step(120)
				closed = _fingerprint()
	# the score screen
	for attempt in 40:
		await game._step(30)
		var st: PackedStringArray = shell.shell_stack()
		if st.size() > 0 and st[-1] == "TimeLine.apt":
			break
	var st2: PackedStringArray = shell.shell_stack()
	if st2.size() == 0 or st2[-1] != "TimeLine.apt":
		errors.push_back("in-game: no score screen after Exit: %s" % str(st2))
		return
	await game._step(120)
	await _shot("ScoreScreen")
	# the score screen's pages: Skip ends its timeline (the tabs and More come); the tabs and headers; More (Back and Continue come); Back; More
	# again; Continue last (to the main menu)
	var score_screen := {"name": "Score screen", "route": [], "depth": MAX_DEPTH}
	var pressed := {}
	for round in 2:
		var score := _fingerprint()
		var order: Array = score.buttons.duplicate()
		order.sort_custom(func(x, y): return _score_rank(x) < _score_rank(y))
		for p in order:
			if pressed.has(p) or _score_rank(p) >= 3:
				continue
			pressed[p] = true
			await _press(score_screen, {"id": p, "kind": "button", "skip": false})
		for name in score.gadget_rects:
			if not pressed.has(name):
				pressed[name] = true
				await _press(score_screen, {"id": name, "kind": score.gadget_kinds[name], "skip": false})
	for name in ["More", "Back", "More", "Continue"]:
		var b := _find_named(name)
		if b.is_empty():
			errors.push_back("in-game: the score screen has no %s" % name)
			return
		await _press(score_screen, {"id": b.path, "kind": "button", "skip": false})
	for attempt in 40:
		if _main_ready():
			break
		await game._step(30)


func _score_rank(path: String) -> int:
	var leaf := path.to_lower()
	if leaf.contains(".$"):
		return 0 # the timeline's own tabs (before Skip)
	if leaf.contains("skip"):
		return 1
	if leaf.contains("more") or leaf.contains("back") or leaf.contains("continue"):
		return 3
	return 2


# the quit menu's own buttons: the hittable ones its opening added (its open animation runs about a second)
func _quit_menu_buttons(closed: Dictionary) -> Array:
	# polled until the set holds still for half a second
	var out: Array = []
	var last := -1
	for attempt in 30:
		out.clear()
		for p in _fingerprint().buttons:
			if not closed.buttons.has(p):
				out.push_back(p)
		if out.size() >= 3 and out.size() == last:
			break
		last = out.size()
		await game._step(30)
	return out


func _open_quit_menu() -> void:
	if game._quit_open:
		return
	_type_key(KEY_ESCAPE)
	await game._step(60)
	if not game._quit_open:
		errors.push_back("in-game: Esc did not open the quit menu")
		game._toggle_quit_menu()
		await game._step(60)


func _close_game_overlays() -> void:
	# back to the plain game: the screens pushed over it (Options, SaveLoad) popped, the quit menu closed
	for i in 4:
		var st: PackedStringArray = shell.shell_stack()
		var box := _find_box_button()
		if not box.is_empty():
			await _click_at(Vector2(box.x, box.y))
			continue
		if st.size() > 0 and (st[-1] == "Options.apt" or st[-1] == "SaveLoad.apt"):
			shell.shell_pop()
			await game._step(60)
	if game._quit_open:
		game._close_quit_menu()
		await game._step(30)


# ---- routes ----------------------------------------------------------------------------------------------------------------------------------------

func _enter(route: Array) -> bool:
	if not await _reset():
		return false
	for step in route:
		var c := {"id": step.id, "kind": "button" if step.kind == "hover" else step.kind}
		var at := _control_point(c)
		for wait in 12:
			# a screen still opening (its buttons are hittable once its open animation ends)
			if at.x >= 0 or String(step.id).contains("Nav."):
				break
			await game._step(30)
			at = _control_point(c)
		if at.x < 0:
			# a nav entry is shown while the pointer is over its nav: the nav first
			if String(step.id).contains("Nav."):
				var nav := String(step.id).get_slice(".", 1)
				var nb: Dictionary = shell.find_button(shell.shell_top_level(), nav)
				if nb.get("found", false):
					game._mouse_event(Vector2(nb.x, nb.y), false, false)
					await game._step(40)
					at = _control_point(c)
		if at.x < 0 and String(step.id).contains("mcNewFeaturesScreen") and _manager_shown():
			# Create-a-Hero's introduction is skipped once "don't show this again" was set (its Continue pressed in this session): the hero list is
			# already the place this step led to
			continue
		if at.x < 0:
			print("INVENTORY route step %s not found" % step.id)
			return false
		game._mouse_event(at, false, false)
		await game._step(40)
		if step.kind == "hover":
			continue
		game._mouse_event(at, true, true)
		await game._step(2)
		game._mouse_event(at, false, true)
		await game._step(SETTLE)
		if step.id.ends_with("Skirmish.bttn"):
			await _skirmish_profile()
		if step.id.ends_with("MultiPlayNav.Online.bttn"):
			await _online_nickname()
	return true


# Skirmish's first visit asks for a profile (the add-profile box): one is made, as a player types it
func _skirmish_profile() -> void:
	for attempt in 30:
		if shell.lobby_apply({"profile": "Gimli"}).get("ok", false):
			break
		await game._step(10)
	await game._step(60)


# the online login page: a nickname typed into its combo (as a player does), so its Log In signs in (with a lobby to sign in to)
func _online_nickname() -> void:
	for attempt in 40:
		for g in shell.list_gadgets():
			if g.name == "OnlineLogin::Nickname" and not g.hidden and g.enabled:
				await _click_at(Vector2(g.x + g.w * 0.3, g.y + g.h * 0.5))
				await game._step(10)
				logins += 1
				_type_text("%s%d" % [NICKNAME, logins])
				await game._step(20)
				game._mouse_event(REST, false, false)
				await game._step(10)
				return
		await game._step(15)
	errors.push_back("online: the login page's nickname field did not come")


func _to_lobby() -> bool:
	await _reset()
	var level: int = shell.shell_top_level()
	var nb: Dictionary = shell.find_button(level, "SoloPlayNav")
	game._mouse_event(Vector2(nb.x, nb.y), false, false)
	await game._step(40)
	var sk: Dictionary = shell.find_button(level, "SoloPlayNav.Skirmish")
	if not sk.get("found", false):
		return false
	await _click_at(Vector2(sk.x, sk.y))
	await game._step(SETTLE)
	await _skirmish_profile()
	var st: PackedStringArray = shell.shell_stack()
	return st.size() > 0 and st[-1] == "Skirmish.apt"


# back to a usable main menu: boxes answered, screens popped, a main-menu page (Credits) left, the navs closed
func _reset() -> bool:
	for attempt in 10:
		var box := _find_box_button()
		if not box.is_empty():
			await _click_at(Vector2(box.x, box.y))
			await game._step(60)
			continue
		var st: PackedStringArray = shell.shell_stack()
		if st.size() > 1:
			# the screen's own way back first (Create-a-Hero's leaves its map mode that way), the shell popped when it has none
			var back := _find_back()
			if not back.is_empty() and attempt < 4:
				await _click_at(Vector2(back.x, back.y))
				await game._step(120)
				var now: PackedStringArray = shell.shell_stack()
				if str(now) != str(st):
					continue
			shell.shell_pop()
			await game._step(90)
			if st[-1] == "CreateAHero.apt" and game._cah != null:
				game._cah_end()
			continue
		# the pointer to a free spot of the stage (a jump to the corner would skip the navs' alpha 0 surround, which closes an open nav as a real
		# pointer crosses it: ui6_walk.gd)
		game._mouse_event(REST, false, false)
		await game._step(30)
		if _main_ready():
			break
		# a page of the main menu itself: the credits, the campaign's difficulty choice
		var exit := _find_named("ExitCredits")
		if exit.is_empty():
			exit = _find_back()
		if exit.is_empty():
			# a held request (a campaign start) left the menu disabled (the movie's DisableAllButtons waits for the game): the menu gets OnFocus("1")
			# as after a sub-screen's pop, which shows its buttons again (AptMainMenu's unavailable-box path does the same)
			shell.shell_invoke(shell.shell_top_level(), "OnFocus", PackedStringArray(["1"]))
			await game._step(90)
			continue
		await _click_at(Vector2(exit.x, exit.y))
		await game._step(120)
	game._mouse_event(REST, false, false)
	await game._step(10)
	await game._wait_main_ready()
	var st2: PackedStringArray = shell.shell_stack()
	return st2.size() == 1 and st2[0] == "MainMenu.apt" and _main_ready()


func _find_back() -> Dictionary:
	for name in BACK_NAMES:
		var b := _find_named(name)
		if not b.is_empty():
			return b
	# a back button named after the word (the campaign difficulty's cancelButton)
	for level in 16:
		if not shell.has_level(level):
			continue
		for b in shell.list_buttons(level):
			var leaf := _leaf(String(b.path)).to_lower()
			for name in BACK_NAMES:
				if b.hittable and leaf.begins_with(String(name).to_lower()):
					return b
	return {}


func _main_ready() -> bool:
	var st: PackedStringArray = shell.shell_stack()
	if st.size() != 1 or st[0] != "MainMenu.apt":
		return false
	var ready := 0
	for b in shell.list_buttons(shell.shell_top_level()):
		for nav in ["SoloPlayNav.instance108", "MultiPlayNav.instance80", "OptionsNav.instance80", "MyHeroes.bttn", "QuitMainMenu.bttn"]:
			if String(b.path).ends_with(nav) and b.hittable:
				ready += 1
	return ready == 5


# ---- reading the screen ----------------------------------------------------------------------------------------------------------------------------

func _fingerprint() -> Dictionary:
	var fp := {}
	fp.stack = Array(shell.shell_stack())
	var buttons: Array = []
	var all_buttons := {}
	for level in 16:
		if not shell.has_level(level):
			continue
		for b in shell.list_buttons(level):
			all_buttons[String(b.path)] = true
			if b.hittable and _on_stage(b.x, b.y):
				buttons.push_back(String(b.path))
	buttons.sort()
	fp.buttons = buttons
	fp.all_buttons = all_buttons # covered ones too (a back layer uncovered by a disabled menu is no new button)
	var rects := {}
	var kinds := {}
	var state := {}
	var entries := {}
	var full := {}
	for g in shell.list_gadgets():
		if g.hidden or g.w <= 0 or g.h <= 0:
			continue
		if not String(g.parent).is_empty() or String(g.name).is_empty() or not _on_stage(g.x + g.w * 0.5, g.y + g.h * 0.5):
			continue # a part of a gadget (a list's scroll bar, a combo's button; unnamed): the gadget itself is the control
		var name := _gadget_id(g)
		entries[name] = int(g.get("entries", -1))
		full[name] = {"kind": g.kind, "enabled": g.enabled, "text": g.get("text", ""), "checked": g.get("checked", null), "value": g.get("value", null),
			"selected": g.get("selected", null), "entries": g.get("entries", null), "open": g.get("open", null), "first": g.get("first", null)}
		state[name] = "%s|%s|%s|%s|%s|%s|%s|%s|%s" % [g.enabled, g.get("text", ""), g.get("checked", ""), g.get("value", ""), g.get("selected", ""),
			g.get("entries", ""), g.get("open", ""), g.get("first", ""), g.get("rows", "")]
		if g.enabled and g.kind in ["button", "radio", "checkbox", "slider", "combo", "list", "entry", "tabs"] and not _is_combo_part(g):
			rects[name] = Rect2(g.x, g.y, g.w, g.h)
			kinds[name] = g.kind
	fp.gadget_rects = rects
	fp.gadget_kinds = kinds
	fp.gadget_state = state
	fp.gadget_entries = entries
	fp.gadget_full = full
	fp.probe = _probe()
	var texts: Array = []
	for t in shell.drawn_texts(false):
		texts.push_back(String(t.text))
	texts.sort()
	fp.texts = texts
	fp.texts_hash = str(texts).hash()
	fp.ops_hash = str(shell.describe_ops()).hash()
	fp.notes = Array(shell.get_shell_report().get("note_lines", PackedStringArray())).slice(-40)
	return fp


# the engine state a press may change (not the picture): the top screen's (AptMenuPlayer.screen_state), the skirmish lobby's setup, the online
# client, Create-a-Hero's view, hero list and camera, the quit menu, the options
func _probe() -> Dictionary:
	var p: Dictionary = shell.screen_state() if shell.has_method("screen_state") else {}
	var lobby: Dictionary = shell.lobby_state()
	if lobby.get("found", false):
		p["lobby"] = JSON.stringify({"map": lobby.get("map"), "rules": lobby.get("rules"), "slots": lobby.get("slots")})
	var online: Dictionary = shell.online_status()
	if online.get("open", false):
		p["online"] = JSON.stringify({"signed_in": online.get("signed_in"), "in_game": online.get("in_game"), "host": online.get("host"),
			"ready": online.get("ready")})
	var cah: Dictionary = shell.get_create_a_hero_view()
	if cah.get("up", false):
		p["cah_page"] = cah.get("page")
		p["cah_hero"] = "%s/%s/%s" % [cah.get("class"), cah.get("subclass"), str(cah.get("record", PackedByteArray())).md5_text()]
		p["cah_heroes"] = shell.get_create_a_hero_heroes().size()
		if game._cah != null:
			p["cah_camera"] = "%.2f/%.2f" % [float(game._cah.get("_yaw")), float(game._cah.get("_t"))]
	p["quit_menu_open"] = game._quit_open
	p["options"] = str(shell.get_options()).md5_text()
	return p


func _type_text(text: String) -> void:
	for ch in text:
		for pressed in [true, false]:
			var e := InputEventKey.new()
			e.keycode = OS.find_keycode_from_string(ch.to_upper())
			e.physical_keycode = e.keycode
			e.unicode = ch.unicode_at(0)
			e.pressed = pressed
			Input.parse_input_event(e)


# a list row other than the selected one (a press on the list's empty space below its rows selects nothing, in retail too)
func _list_row_point(id: String, fp: Dictionary) -> Vector2:
	var r: Rect2 = fp.gadget_rects.get(id, Rect2())
	var g: Dictionary = fp.gadget_full.get(id, {})
	for h in shell.list_gadgets():
		if _gadget_id(h) == id and int(h.get("row_height", 0)) > 0 and int(h.get("entries", 0)) > 0:
			# the first visible row (the list may be scrolled to its selection: display_pos), or the second when the first is the selected one
			var row := 1 if int(h.get("selected", -1)) == int(h.get("display_pos", 0)) and int(h.entries) > int(h.get("display_pos", 0)) + 1 else 0
			return Vector2(r.position.x + r.size.x * 0.4, r.position.y + (row + 0.5) * float(h.row_height))
	return Vector2(r.position.x + r.size.x * 0.5, r.position.y + r.size.y * 0.5)


# a gadget's name in the records: its placeholder's name and kind; an engine-made gadget is named by its skin ("ComboBox.wnd:"), shared by every one of
# its kind, so those carry their place too
func _gadget_id(g: Dictionary) -> String:
	if String(g.name).is_empty():
		return "(unnamed %s at %d,%d)" % [g.kind, g.x, g.y]
	if String(g.name).ends_with(".wnd:"):
		return "%s#%s@%d,%d" % [g.name, g.kind, g.x, g.y]
	return "%s#%s" % [g.name, g.kind]


func _on_stage(x: float, y: float) -> bool:
	var size: Vector2 = shell.get_stage_size()
	return x >= 0 and y >= 0 and x <= size.x and y <= size.y


func _is_combo_part(g: Dictionary) -> bool:
	# a combo's own list box, its drop-down button and its edit field are the combo's
	for p in shell.list_gadgets():
		if p.name == g.parent and p.kind == "combo":
			return true
	return false


func _signature(fp: Dictionary) -> String:
	var parts := PackedStringArray([str(fp.stack)])
	for p in fp.buttons:
		parts.push_back(p)
	for n in fp.gadget_rects:
		parts.push_back(n)
	return "|".join(parts).md5_text()


func _control_point(c: Dictionary) -> Vector2:
	if c.kind == "button":
		for level in 16:
			if not shell.has_level(level):
				continue
			for b in shell.list_buttons(level):
				if String(b.path) == c.id and b.hittable:
					return Vector2(b.x, b.y)
		return Vector2(-1, -1)
	for g in shell.list_gadgets():
		var name := _gadget_id(g)
		if name == c.id and not g.hidden:
			if g.kind == "slider":
				return Vector2(g.x + g.w * 0.25, g.y + g.h * 0.5) # a quarter along: a value the default is not
			return Vector2(g.x + g.w * 0.5, g.y + g.h * 0.5)
	return Vector2(-1, -1)


func _find_box_button() -> Dictionary:
	for level in 16:
		if not shell.has_level(level):
			continue
		for b in shell.list_buttons(level):
			var p := String(b.path)
			if b.hittable and p.contains("MessageBox") and (p.containsn("Ok") or p.containsn("Cancel") or p.containsn("No")):
				return b
	return {}


func _find_named(name: String) -> Dictionary:
	for level in 16:
		if not shell.has_level(level):
			continue
		for b in shell.list_buttons(level):
			var p := String(b.path).to_lower()
			var n := name.to_lower()
			if b.hittable and (p.contains("." + n + ".") or p.ends_with("." + n)):
				return b
	return {}


func _click_at(p: Vector2) -> void:
	game._mouse_event(p, false, false)
	await game._step(2)
	game._mouse_event(p, true, true)
	await game._step(2)
	game._mouse_event(p, false, true)
	await game._step(10)


func _type_key(key: Key) -> void:
	for pressed in [true, false]:
		var e := InputEventKey.new()
		e.keycode = key
		e.physical_keycode = key
		e.unicode = 0x61 if key == KEY_A else 0
		e.pressed = pressed
		Input.parse_input_event(e)


func _leaf(path: String) -> String:
	var parts := path.trim_suffix(".bttn").split(".")
	return parts[-1] if parts.size() > 0 else path


func _label(id: String) -> String:
	var p := id.trim_suffix(".bttn")
	var at := p.find(".")
	return p.substr(at + 1) if at >= 0 else p


func _shot(name: String) -> void:
	if DisplayServer.get_name() == "headless" or dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	var image: Image = game._capture_image(name)
	if image == null:
		return
	var file := name.replace(" > ", "--").replace(" ", "_").replace("/", "_").replace(".", "_") + ".png"
	image.save_png(dir.path_join(file))
