-- The bfme.* scripting console (docs/BFME_LUA.md): a Command view (output and a multiline editor) and a Log view, inspired by BFME2Lua's
-- Command / Log editor. Written in Lua as a lua-gdextension script class (a CanvasLayer); game.gd and tests/lua_console_test.gd use:
--   execute (Callable: code -> {ok, output}), history_path, signal toggled(open), toggle(), is_open(), reload_scripts(), is_toggle_event(event)
-- The console UI runs in lua-gdextension's script state; the commands it sends go to the bfme.* state through `execute`.
-- Keys: Ctrl+Shift+C toggles it (backtick stays the game's), Escape closes it, Ctrl+Shift+F5 reloads the scripts of the Lua paths.
local LuaConsole = {
	extends = CanvasLayer,
	toggled = signal("open"),
	execute = property { type = Callable },
	history_path = "user://console_history.cfg",
}

local GOLD = Color("c4a76a")
local INK = Color("10191e")
local TEXT = Color("e3dfcf")
local MUTED = Color("99aaa9")
local ERROR_TEXT = Color("e8a18b")
local UI_SCALE = 2.0
local HISTORY_LIMIT = 256

-- Lua strings are bytes; the text editors count characters
local function ulen(s)
	local _, n = s:gsub("[^\128-\191]", "")
	return n
end

local function trim(s)
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function box(color, border, padding)
	local b = StyleBoxFlat:new()
	b.bg_color = color
	b.border_color = border
	b:set_border_width_all(1)
	b:set_content_margin_all(padding or 10)
	b:set_corner_radius_all(2)
	return b
end

local function label(text, color, font_size)
	local l = Label:new()
	l.text = text
	l:add_theme_color_override("font_color", color or TEXT)
	l:add_theme_font_size_override("font_size", font_size or 14)
	return l
end

local function text_view()
	local view = TextEdit:new()
	view.editable = false
	view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	view.custom_minimum_size = Vector2(0, 85)
	view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view:add_theme_font_size_override("font_size", 14)
	return view
end

local function set_min_height(control, height)
	control.custom_minimum_size = Vector2(control.custom_minimum_size.x, height)
end

-- the gold corner ornaments of the frame (the panel's draw signal)
local function draw_ornaments(panel)
	local size = panel.size
	for _, corner in ipairs { Vector2(9, 9), Vector2(size.x - 9, 9), Vector2(9, size.y - 9), Vector2(size.x - 9, size.y - 9) } do
		local direction = Vector2(corner.x < size.x / 2 and 1 or -1, corner.y < size.y / 2 and 1 or -1)
		panel:draw_line(corner, corner + Vector2(25 * direction.x, 0), GOLD, 2)
		panel:draw_line(corner, corner + Vector2(0, 25 * direction.y), GOLD, 2)
		local center = corner + direction * 5
		panel:draw_colored_polygon(PackedVector2Array(Array {
			center + Vector2(0, -3), center + Vector2(3, 0), center + Vector2(0, 3), center + Vector2(-3, 0) }), GOLD)
	end
end

local function console_theme()
	local theme = Theme:new()
	local serif = SystemFont:new()
	serif.font_names = PackedStringArray(Array { "Palatino", "Georgia", "DejaVu Serif" })
	serif.oversampling = UI_SCALE
	theme.default_font = serif
	theme.default_font_size = 15
	theme:set_color("font_color", "Label", TEXT)
	for _, state in ipairs { "normal", "hover", "pressed", "focus" } do
		theme:set_stylebox(state, "Button", box(state == "hover" and Color("28383c") or Color("19272d"), state ~= "normal" and GOLD or Color("655f48"), 8))
		theme:set_color(state == "normal" and "font_color" or ("font_" .. state .. "_color"), "Button", TEXT)
	end
	local mono = SystemFont:new()
	mono.font_names = PackedStringArray(Array { "Menlo", "Consolas", "DejaVu Sans Mono" })
	mono.oversampling = UI_SCALE
	for _, type in ipairs { "TextEdit", "CodeEdit" } do
		theme:set_font("font", type, mono)
		theme:set_color("font_color", type, TEXT)
		theme:set_color("font_readonly_color", type, TEXT)
		theme:set_color("caret_color", type, GOLD)
		theme:set_color("selection_color", type, Color("415c65"))
		theme:set_stylebox("normal", type, box(Color("0c1418"), Color("425252")))
		theme:set_stylebox("read_only", type, box(Color("101b20"), Color("354648")))
		theme:set_stylebox("focus", type, box(Color(0, 0, 0, 0), GOLD, 0))
	end
	theme:set_stylebox("panel", "TabContainer", box(Color("142126"), Color("6d674e")))
	theme:set_stylebox("tab_selected", "TabContainer", box(Color("26383b"), GOLD, 9))
	theme:set_stylebox("tab_unselected", "TabContainer", box(Color("142126"), Color("465352"), 9))
	theme:set_color("font_selected_color", "TabContainer", GOLD)
	theme:set_color("font_unselected_color", "TabContainer", MUTED)
	return theme
end

local function lua_highlighter()
	local syntax = CodeHighlighter:new()
	syntax.number_color = Color("d5b875")
	syntax.symbol_color = Color("b2c4c5")
	syntax.function_color = Color("a3c6ce")
	syntax.member_variable_color = TEXT
	for _, keyword in ipairs { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
		"nil", "not", "or", "repeat", "return", "then", "true", "until", "while" } do
		syntax:add_keyword_color(keyword, Color("d1ad73"))
	end
	syntax:add_color_region('"', '"', Color("9cc599"))
	syntax:add_color_region("'", "'", Color("9cc599"))
	syntax:add_color_region("--[[", "]]", Color("748c89"))
	syntax:add_color_region("--", "", Color("748c89"), true)
	return syntax
end

function LuaConsole:_ready()
	self._history = Array()
	self._history_index = 0
	self._draft = ""
	self._layout_queued = false
	self:_load_history()
	self.layer = 100
	local root = Control:new()
	root:set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	self:add_child(root)
	self._root = root
	local veil = ColorRect:new()
	veil.color = Color(0.015, 0.025, 0.03, 0.66)
	veil:set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root:add_child(veil)
	local panel = PanelContainer:new()
	root:add_child(panel)
	self._panel = panel
	panel.draw:connect(Callable(function() draw_ornaments(panel) end))
	panel.minimum_size_changed:connect(Callable(self, "_request_layout"))
	panel.theme = console_theme()
	local frame = box(INK, GOLD, 22)
	frame.shadow_color = Color(0, 0, 0, 0.7)
	frame.shadow_size = 20
	panel:add_theme_stylebox_override("panel", frame)

	local chrome = VBoxContainer:new()
	chrome:add_theme_constant_override("separation", 10)
	panel:add_child(chrome)
	local scroll = ScrollContainer:new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	chrome:add_child(scroll)
	local rows = VBoxContainer:new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows:add_theme_constant_override("separation", 10)
	scroll:add_child(rows)
	local header = HBoxContainer:new()
	rows:add_child(header)
	local title = label("LUA CONSOLE", GOLD, 25)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header:add_child(title)
	rows:add_child(label("P A L A N T Í R   /   S C R I P T I N G", MUTED, 11))

	local tabs = TabContainer:new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows:add_child(tabs)
	self._tabs = tabs
	local command = VBoxContainer:new()
	command.name = "Command"
	command:add_theme_constant_override("separation", 8)
	tabs:add_child(command)
	self._output = text_view()
	command:add_child(self._output)
	local editor_heading = HBoxContainer:new()
	editor_heading:add_child(label("SCRIPT", GOLD, 12))
	local editor_hint = label("Enter to run · Shift+Enter for a new line", MUTED, 12)
	editor_hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	editor_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	editor_heading:add_child(editor_hint)
	command:add_child(editor_heading)
	local entry = CodeEdit:new()
	entry.placeholder_text = "bfme.players.localPlayer.money = 10000"
	entry.custom_minimum_size = Vector2(0, 110)
	entry.size_flags_vertical = Control.SIZE_EXPAND_FILL
	entry.size_flags_stretch_ratio = 0.5
	entry.gutters_draw_line_numbers = true
	entry.indent_size = 4
	entry.indent_use_spaces = true
	entry:add_theme_font_size_override("font_size", 14)
	entry:add_theme_color_override("font_color", TEXT)
	entry.syntax_highlighter = lua_highlighter()
	command:add_child(entry)
	entry.gui_input:connect(Callable(self, "_entry_input"))
	self._entry = entry
	local log = text_view()
	log.name = "Log"
	tabs:add_child(log)
	self._log = log
	tabs.tab_changed:connect(Callable(function(_index)
		if tabs.current_tab == 0 then entry:grab_focus() end
	end))

	local toolbar = HFlowContainer:new()
	toolbar:add_theme_constant_override("separation", 8)
	chrome:add_child(toolbar)
	self._status = label("READY · LuaJIT", MUTED, 12)
	self._status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar:add_child(self._status)
	local footer = label("Ctrl+Shift+C toggle · Esc close · ↑ / ↓ history · clear empties the view · Ctrl+Shift+F5 reloads the Lua scripts", MUTED, 12)
	footer.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	chrome:add_child(footer)
	self:_append("BFME scripting console\nInspect the realm, shape the battle.\n\nTry  bfme.match.getStatus()  or  bfme.players.localPlayer.money\n")
	self:get_viewport().size_changed:connect(Callable(self, "_layout"))
	self:_layout()
	root:hide()
end

function LuaConsole:_layout()
	local viewport_size = self:get_viewport():get_visible_rect().size
	local logical = viewport_size / UI_SCALE
	local compact = logical.y < 600
	set_min_height(self._entry, compact and 70 or 110)
	set_min_height(self._output, compact and 40 or 85)
	set_min_height(self._log, compact and 40 or 85)
	local panel = self._panel
	panel.scale = Vector2(UI_SCALE, UI_SCALE)
	panel.size = Vector2(math.min(1060, logical.x - 16), math.min(740, logical.y - 16))
	panel.position = (viewport_size - panel.size * UI_SCALE) / 2
	panel:queue_redraw()
end

function LuaConsole:_request_layout()
	if not self._layout_queued then
		self._layout_queued = true
		self:call_deferred("_settle_layout")
	end
end

function LuaConsole:_settle_layout()
	self._layout_queued = false
	self:_layout()
end

-- Ctrl+Shift+C (backtick is left to the game: RotWK's powers menu)
function LuaConsole:is_toggle_event(event)
	if event == nil or not event:is_class("InputEventKey") then
		return false
	end
	return event.keycode == KEY_C and event.ctrl_pressed and event.shift_pressed and not event.alt_pressed and not event.meta_pressed
end

function LuaConsole:_input(event)
	local key = event:is_class("InputEventKey")
	if key and event.keycode == KEY_F5 and event.ctrl_pressed and event.shift_pressed then
		if event.pressed and not event.echo then self:reload_scripts() end
		self:get_viewport():set_input_as_handled()
	elseif self:is_toggle_event(event) then
		if event.pressed and not event.echo then self:toggle() end
		self:get_viewport():set_input_as_handled()
	elseif self:is_open() and key and event.keycode == KEY_ESCAPE then
		if event.pressed and not event.echo then self:toggle() end
		self:get_viewport():set_input_as_handled()
	end
end

function LuaConsole:is_open()
	return self._root ~= nil and self._root.visible
end

function LuaConsole:toggle()
	local root = self._root
	root.visible = not root.visible
	if root.visible then
		self:_request_layout()
		if self._tabs.current_tab == 0 then self._entry:grab_focus() end
	else
		self._entry:release_focus()
	end
	self.toggled:emit(root.visible)
end

-- Append through the editor API so selecting / copying old results stays undisturbed.
function LuaConsole:_write(view, text)
	local last = view:get_line_count() - 1
	view:insert_text(text, last, ulen(view:get_line(last)))
	local all = view.text
	if #all > 200000 then
		local start = #all - 160000 + 1
		while start <= #all and all:byte(start) >= 128 and all:byte(start) < 192 do start = start + 1 end -- a whole UTF-8 character
		view.text = all:sub(start)
	end
	-- Wrapped text updates the scroll range after insertion: read the new range once that update has run, even if the user had
	-- scrolled up before the append
	Callable(function()
		if is_instance_valid(view) then view:set_v_scroll(view:get_v_scroll_bar().max_value) end
	end):call_deferred()
end

function LuaConsole:_append(text)
	self:_write(self._output, text)
	self:_write(self._log, text)
end

function LuaConsole:_clear_visible()
	if self._tabs.current_tab == 0 then
		self._output.text = ""
		self._entry:grab_focus()
	else
		self._log.text = ""
		self._log:grab_focus()
	end
end

function LuaConsole:_load_history()
	local config = ConfigFile:new()
	local err = config:load(self.history_path)
	if err ~= OK and err ~= ERR_FILE_NOT_FOUND then
		push_warning("Could not load console history: " .. error_string(err))
	end
	local history = Array()
	if err == OK then
		local commands = config:get_value("console", "history", Array())
		if type(commands) == "userdata" and commands:is(Array) then
			for i = 0, #commands - 1 do
				local command = commands[i]
				if type(command) == "string" and trim(command) ~= "" then history:append(command) end
			end
			if #history > HISTORY_LIMIT then history = history:slice(#history - HISTORY_LIMIT) end
		end
	end
	self._history = history
	self._history_index = #history
end

function LuaConsole:_save_history()
	local config = ConfigFile:new()
	config:set_value("console", "history", self._history)
	local err = config:save(self.history_path)
	if err ~= OK then push_warning("Could not save console history: " .. error_string(err)) end
end

function LuaConsole:_set_status(text, ok)
	self._status.text = text
	self._status:add_theme_color_override("font_color", ok and MUTED or ERROR_TEXT)
end

function LuaConsole:_submit(code)
	if trim(code) == "" then return end
	local history = self._history
	if #history == 0 or history[#history - 1] ~= code then
		history:append(code)
		if #history > HISTORY_LIMIT then history:pop_front() end
		self:_save_history()
	end
	self._history_index = #history
	self._draft = ""
	self._entry:clear()
	self:_execute_command(code)
end

-- UI actions execute normally but never alter entered history or the draft.
function LuaConsole:_execute_command(code)
	if trim(code) == "" then return end
	if trim(code) == "clear" then
		self:_clear_visible()
		return
	end
	self:_append("\n› " .. code .. "\n")
	local result = self.execute:call(code)
	local output = tostring(result:get("output", ""))
	local ok = result:get("ok", false) == true
	if output ~= "" then
		self:_append(output .. (output:sub(-1) == "\n" and "" or "\n"))
	elseif not ok then
		self:_append("Command failed.\n")
	end
	self:_set_status(ok and "READY · command complete" or "ERROR · see output", ok)
	if self._tabs.current_tab == 0 then self._entry:grab_focus() end
end

-- Release callbacks into GDScript while the scene tree and its script language are
-- still alive. Lua may otherwise retain them until its later shutdown collection.
function LuaConsole:_exit_tree()
 self.execute = Callable()
 collectgarbage("collect")
 collectgarbage("collect")
end

function LuaConsole:reload_scripts()
	self:_append("\n› Reload scripts\n")
	local result = self.execute:call("bfme.console.reload()")
	local ok = result:get("ok", false) == true
	local output = tostring(result:get("output", ""))
	self:_append(output .. (output:sub(-1) == "\n" and "" or "\n"))
	self:_set_status(ok and "READY · scripts reloaded" or "RELOAD ERROR · see output", ok)
end

function LuaConsole:_entry_input(event)
	if not event:is_class("InputEventKey") or not event.pressed then return end
	local entry = self._entry
	if event.keycode == KEY_ENTER or event.keycode == KEY_KP_ENTER then
		if not event.shift_pressed then
			entry:accept_event()
			if not event.echo then self:_submit(entry.text) end
		end
		return
	end
	if event.keycode ~= KEY_UP and event.keycode ~= KEY_DOWN then return end
	-- Keep normal multiline cursor movement away from the first / last editor line.
	if event.keycode == KEY_UP and entry:get_caret_line() ~= 0 then return end
	if event.keycode == KEY_DOWN and entry:get_caret_line() ~= entry:get_line_count() - 1 then return end
	local history = self._history
	if self._history_index == #history then self._draft = entry.text end
	self._history_index = math.max(0, math.min(#history, self._history_index + (event.keycode == KEY_UP and -1 or 1)))
	entry.text = self._history_index == #history and self._draft or history[self._history_index]
	local last = entry:get_line_count() - 1
	entry:set_caret_line(last)
	entry:set_caret_column(ulen(entry:get_line(last)))
	entry:accept_event()
end

return LuaConsole
