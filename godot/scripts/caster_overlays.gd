## Read-only broadcast panels. Values are captured by the simulation owner, not estimated from rendering.
extends Control

signal clean_changed(enabled: bool)

class Graph extends Control:
	var samples: Dictionary = {}
	var players: Array = []
	var frame := 1
	var metric := "army_value"

	func _draw() -> void:
		var frames: Array = samples.keys().filter(func(f): return f <= frame)
		frames.sort()
		var highest := 1.0
		for f in frames:
			for p in samples[f]:
				if valid(p):
					highest = maxf(highest, p[metric])
		var area := Rect2(56, 22, maxf(1, size.x - 72), maxf(1, size.y - 60))
		draw_rect(area, Color("273840"), false, 1)
		var font := get_theme_default_font()
		var font_size := get_theme_default_font_size()
		draw_string(font, Vector2(0, 20), str(roundi(highest)), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		draw_string(font, Vector2(8, area.end.y), "0", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		draw_string(font, Vector2(area.position.x, size.y - 6), "0:00", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		var elapsed := maxi(0, frame - 1)
		draw_string(font, Vector2(area.end.x - 65, size.y - 6), "%d:%02d" % [elapsed / 300, (elapsed / 5) % 60], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		for player in players:
			var previous := Vector2.ZERO
			var previous_frame := -10
			for f in frames:
				var rows: Array = samples[f].filter(func(p): return p.index == player.index)
				if rows.is_empty() or not valid(rows[0]):
					previous_frame = -10
					continue
				var point := area.position + Vector2(float(f - 1) / maxf(1, frame - 1) * area.size.x, (1 - float(rows[0][metric]) / highest) * area.size.y)
				var tint := Color.hex((int(player.color) << 8) | 255).lightened(0.3)
				if f - previous_frame <= 5:
					draw_line(previous, point, tint, 2, true)
				draw_circle(point, 1.5, tint)
				previous = point
				previous_frame = f

	func valid(row: Dictionary) -> bool:
		return (metric != "army_value" or row.army_complete) and (metric != "income" or row.income_complete)

const TITLES := {"players": "Players [F1]", "production": "Production [F2]", "army": "Army [F3]",
	"graphs": "Graphs [F4]", "heroes": "Heroes [F5]", "events": "Events [F6]"}
const GOLD := Color("d7bd7c")
var enabled := {"players": true, "production": false, "army": false, "graphs": false, "heroes": false, "events": true}
var clean := false
var panels: Dictionary = {}
var buttons: Dictionary = {}
var labels: Dictionary = {}
var cards: Dictionary = {}
var toolbar: HBoxContainer
var player_grid: GridContainer
var graph: Graph
var graph_selector: OptionButton
var graph_history: Dictionary = {}
var next_sample := 0
var shell: Node
var players: Array = []
var metrics: Dictionary = {}
var _texts: Dictionary = {}
var _top := 0.0
var pip: Control


func configure(game_shell: Node, shared_theme: Theme) -> void:
	shell = game_shell
	theme = shared_theme
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	toolbar = HBoxContainer.new()
	add_child(toolbar)
	for key in TITLES:
		var button := Button.new()
		button.text = TITLES[key]
		button.toggle_mode = true
		button.focus_mode = Control.FOCUS_NONE
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.set_pressed_no_signal(enabled[key])
		button.toggled.connect(func(value): set_panel(key, value))
		toolbar.add_child(button)
		buttons[key] = button
	var clean_button := Button.new()
	clean_button.text = "Clean [F9]"
	clean_button.focus_mode = Control.FOCUS_NONE
	clean_button.pressed.connect(func(): set_clean(true))
	toolbar.add_child(clean_button)
	var body := make_panel("players", "")
	player_grid = GridContainer.new()
	body.add_child(player_grid)
	for key in ["production", "army", "heroes", "events"]:
		body = make_panel(key, {"production": "PRODUCTION / UPGRADES", "army": "ARMY COMPOSITION · battalions / units",
			"heroes": "HERO STATUS", "events": "EVENTS · completed simulation"}[key])
		var text := RichTextLabel.new()
		text.bbcode_enabled = true
		text.size_flags_vertical = Control.SIZE_EXPAND_FILL
		text.custom_minimum_size.y = 40
		body.add_child(text)
		labels[key] = text
	body = make_panel("graphs", "HISTORY · one sample per simulation second")
	graph_selector = OptionButton.new()
	for title in ["Army base build cost", "Resource income / last 60s", "Units lost · retail score counter"]:
		graph_selector.add_item(title)
	graph_selector.item_selected.connect(func(index):
		graph.metric = ["army_value", "income", "units_lost"][index]
		graph.queue_redraw())
	body.add_child(graph_selector)
	graph = Graph.new()
	graph.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(graph)
	var note := Label.new()
	note.text = "Gaps mean unvisited or unavailable data."
	body.add_child(note)
	labels.legend = RichTextLabel.new()
	labels.legend.bbcode_enabled = true
	labels.legend.fit_content = true
	labels.legend.scroll_active = false
	body.add_child(labels.legend)
	for key in panels:
		panels[key].visible = enabled[key]


func make_panel(key: String, title: String) -> VBoxContainer:
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color("10191eef")
	style.border_color = GOLD.darkened(0.3)
	style.set_border_width_all(1)
	style.set_content_margin_all(12)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	panels[key] = panel
	var body := VBoxContainer.new()
	panel.add_child(body)
	if not title.is_empty():
		var heading := Label.new()
		heading.text = title
		heading.add_theme_color_override("font_color", GOLD)
		body.add_child(heading)
	return body


func set_panel(key: String, value: bool) -> void:
	enabled[key] = value
	panels[key].visible = value
	buttons[key].set_pressed_no_signal(value)
	layout(_top)


func set_clean(value: bool) -> void:
	clean = value
	toolbar.visible = not clean
	clean_changed.emit(clean)
	if pip != null:
		pip.set_clean(clean)
	layout(_top)


func preferences() -> Dictionary:
	return {"enabled": enabled.duplicate(), "clean": clean, "metric": graph_selector.selected, "history": graph_history.duplicate(true)}


func restore(values: Dictionary) -> void:
	for key in values.get("enabled", {}):
		set_panel(key, values.enabled[key])
	graph_history = values.get("history", {})
	graph_selector.select(values.get("metric", 0))
	graph.metric = ["army_value", "income", "units_lost"][graph_selector.selected]
	next_sample = 0
	set_clean(values.get("clean", false))


func escape_bbcode(value: String) -> String:
	return value.replace("[", "[lb]")


func display_name(name: String, tag: String = "") -> String:
	if tag.is_empty() or shell == null:
		return escape_bbcode(name)
	if not _texts.has(tag):
		var result: Dictionary = shell.fetch_text(tag)
		_texts[tag] = result.text if result.found else name + " (missing text: " + tag + ")"
	return escape_bbcode(_texts[tag])


func update_data(data: Dictionary, roster: Array, events: Array) -> void:
	if not data.get("active", false):
		return
	metrics = data
	players = roster
	for sample in data.history:
		graph_history[int(sample.frame)] = sample.players
		next_sample = maxi(next_sample, int(sample.frame) + 1)
	graph.samples = graph_history
	graph.frame = data.frame
	graph.players = players
	graph.queue_redraw()
	player_grid.columns = maxi(1, mini(4, players.size()))
	var content := {"production": "", "army": "", "heroes": ""}
	var legend: PackedStringArray = []
	for player in players:
		var found: Array = data.players.filter(func(p): return p.index == player.index)
		if found.is_empty():
			continue
		var p: Dictionary = found[0]
		var color := Color.hex((int(player.color) << 8) | 255).lightened(0.4)
		if not cards.has(p.index):
			var card := VBoxContainer.new()
			card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			player_grid.add_child(card)
			var rows: Array[Label] = []
			for i in 3:
				var row := Label.new()
				row.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
				row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
				card.add_child(row)
				rows.append(row)
			cards[p.index] = rows
		cards[p.index][0].text = "%s · %s%s" % [player.name, str(player.faction).trim_prefix("Faction"), " · defeated" if player.defeated else ""]
		cards[p.index][0].add_theme_color_override("font_color", color)
		cards[p.index][1].text = "$%d  ·  CP %d/%d  ·  PP %d" % [p.money, p.cp_used, p.cp_limit, p.power_points]
		cards[p.index][2].text = "Army %s  ·  Income/min %s" % [str(p.army_value) if p.army_complete else "unavailable", str(p.income) if p.income_complete else "incomplete"]
		cards[p.index][2].tooltip_text = "Army: base BuildCost, no battalion-member double counting or upgrade valuation.\nIncome: actual resource payments in the last 60 simulation seconds."
		var heading := "[color=#%s]%s[/color]\n" % [color.to_html(false), escape_bbcode(player.name)]
		for key in content:
			content[key] += heading
		for q in p.production:
			var quantity := "%d left · " % q.remaining if q.kind == "unit" else ""
			content.production += "%s · %s · %.0f%% · %s#%d\n" % [display_name(q.name) if not str(q.name).is_empty() else "Unresolved entry", q.kind, clampf(q.percent, 0, 100), quantity, q.producer]
		if p.production.is_empty():
			content.production += "No queued production\n"
		for a in p.army:
			content.army += "%d × %s\n" % [a.count, display_name(a.name, a.label)]
		if p.army.is_empty():
			content.army += "No combat units\n"
		for h in p.heroes:
			content.heroes += "%s · L%d · %s%s\n" % [display_name(h.name), h.rank, h.state, " · %.0f/%.0f HP" % [h.health, h.max_health] if h.state == "alive" else ""]
		if p.heroes.is_empty():
			content.heroes += "No recruited heroes\n"
		for key in content:
			content[key] += "\n"
		legend.append("[color=#%s]%s[/color]" % [color.to_html(false), escape_bbcode(player.name)])
	for key in content:
		labels[key].text = content[key]
	labels.legend.text = " / ".join(legend)
	var recent: Array = events.filter(func(e): return e.frame <= data.frame and e.kind != 0 and (e.kind != 1 or e.hero or e.structure))
	recent.sort_custom(func(a, b): return a.frame > b.frame)
	var ticker := ""
	for e in recent.slice(0, 3):
		var owners: Array = roster.filter(func(p): return p.index == e.player)
		var owner := str(owners[0].name) if not owners.is_empty() else "Player %d" % e.player
		var action := "Power used" if e.kind == 2 else ("Base ownership changed" if e.kind == 3 and e.base else ("Ownership changed" if e.kind == 3 else ("Hero killed" if e.hero else "Building lost")))
		var elapsed := maxi(0, int(e.frame) - 1)
		ticker += "%d:%02d · %s · %s · %s\n" % [elapsed / 300, (elapsed / 5) % 60, action, escape_bbcode(owner), display_name(e.name)]
	labels.events.text = ticker.strip_edges() if not ticker.is_empty() else "No recorded hero deaths, building losses, ownership changes or powers yet."
	layout(_top)


var transport_top := -1.0


func layout(top: float) -> void:
	_top = top
	var viewport_size := get_viewport_rect().size
	var f := minf(viewport_size.x / 1920.0, viewport_size.y / 1080.0)
	size = viewport_size
	var margin := 48 * f
	var gap := 12 * f
	var width := viewport_size.x - 2 * margin
	toolbar.position = Vector2(margin, top + gap)
	toolbar.size = Vector2(width, 42 * f)
	var y := 24 * f if clean else toolbar.position.y + toolbar.size.y + gap
	panels.players.position = Vector2(margin, y)
	panels.players.size = Vector2(width, 0)
	if enabled.players:
		y += maxf(panels.players.size.y, (88 * ceili(float(players.size()) / 4) + 24) * f) + gap
	panels.events.size = Vector2(width if clean else viewport_size.x * 0.635, 116 * f)
	var event_bottom := viewport_size.y - 24 * f if clean else (transport_top - gap if transport_top >= 0 else viewport_size.y - 130 * f)
	panels.events.position = Vector2(margin if clean else viewport_size.x * 0.34, event_bottom - panels.events.size.y)
	var bottom := viewport_size.y - (260 if not clean else 160) * f
	if enabled.events:
		bottom = minf(bottom, panels.events.position.y - gap)
	var side := 490 * f
	var available := maxf(150 * f, bottom - y)
	var left_count := int(enabled.production) + int(enabled.army)
	var left_y := y
	for key in ["production", "army"]:
		panels[key].position = Vector2(margin, left_y)
		panels[key].size = Vector2(side, maxf(130 * f, (minf(available, viewport_size.y - 390 * f - y) if not clean else available) / maxi(1, left_count) - gap))
		if enabled[key]:
			left_y += panels[key].size.y + gap
	panels.heroes.position = Vector2(viewport_size.x - margin - side, y)
	var pip_height := 0.0
	if pip != null and pip.enabled:
		pip_height = minf(340 * f, available - (130 * f + gap if enabled.heroes else 0))
		pip.position = Vector2(viewport_size.x - margin - side, bottom - pip_height)
		pip.size = Vector2(side, pip_height)
	panels.heroes.size = Vector2(side, available - (pip_height + gap if pip_height > 0 else 0))
	panels.graphs.position = Vector2(margin + side + gap, y)
	panels.graphs.size = Vector2(width - 2 * (side + gap), available)
	graph.custom_minimum_size.y = 160 * f


func over_panel(point: Vector2) -> bool:
	if toolbar.visible and toolbar.get_global_rect().has_point(point):
		return true
	for panel in panels.values():
		if panel.visible and panel.get_global_rect().has_point(point):
			return true
	return false
