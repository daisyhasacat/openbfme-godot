## OpenBFME. GPL-3.0.
## Player reports go only to the lobby origin. No install/device identifiers enter the roster or request.
extends CanvasLayer

signal received(report_id: String)

var lobby_url := ""
var _token := ""
var _game := ""
var _players: Array[Dictionary] = []
var _button: Button
var _dialog: ConfirmationDialog
var _target: OptionButton
var _reason: TextEdit
var _status: Label
var _client: Node
var _key := ""
var _payload := ""
var _busy := false
const RESULT_SUCCESS := 0 ## HTTPRequest.RESULT_SUCCESS (named here only: this file uses no networking class)


func _ready() -> void:
	layer = 105
	_button = Button.new()
	_button.name = "ReportPlayer"
	_button.text = "Report player"
	_button.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	_button.offset_left = -152
	_button.offset_right = -12
	_button.offset_top = 40
	_button.offset_bottom = 76
	_button.pressed.connect(open_dialog)
	add_child(_button)
	_button.hide()
	_dialog = ConfirmationDialog.new()
	_dialog.name = "PlayerReportDialog"
	_dialog.title = "Report player"
	_dialog.ok_button_text = "Send report"
	_dialog.min_size = Vector2i(440, 360)
	_dialog.exclusive = true
	_dialog.dialog_hide_on_ok = false
	_dialog.confirmed.connect(submit)
	add_child(_dialog)
	var content := VBoxContainer.new()
	content.custom_minimum_size.x = 480
	content.add_theme_constant_override("separation", 10)
	_dialog.add_child(content)
	var intro := Label.new()
	intro.custom_minimum_size.x = 460
	intro.text = "Choose a player and describe what happened.\nReports are reviewed; they do not apply automatic penalties."
	intro.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	content.add_child(intro)
	_target = OptionButton.new()
	_target.name = "ReportTarget"
	_target.item_selected.connect(func(_index: int): _reset_submission())
	content.add_child(_target)
	_reason = TextEdit.new()
	_reason.name = "ReportReason"
	_reason.placeholder_text = "Reason (1–1,000 characters)"
	_reason.custom_minimum_size = Vector2(400, 140)
	_reason.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_reason.text_changed.connect(_reset_submission)
	content.add_child(_reason)
	_status = Label.new()
	_status.custom_minimum_size.x = 460
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	content.add_child(_status)
	# NETGUARD-1: this file opens no connection; the report goes through the lobby client's one HTTP path (online_lobby.gd post_oneshot)
	_client = load("res://scripts/online_lobby.gd").new()
	_client.oneshot_done.connect(func(result: int, code: int, body: PackedByteArray): _completed(result, code, PackedStringArray(), body))
	add_child(_client)


func set_context(context: Dictionary) -> void:
	var game: String = str(context.get("game_id", ""))
	var token: String = str(context.get("session_token", ""))
	if RegEx.create_from_string("^[0-9a-f]{16}$").search(game) == null or token.is_empty():
		return
	var players: Array[Dictionary] = []
	for row: Variant in context.get("report_players", []):
		if not row is Dictionary or row.get("you", false):
			continue
		var id: String = str(row.get("id", ""))
		var nick: String = str(row.get("name", ""))
		if id.is_empty() or id.to_utf8_buffer().size() > 128 or nick.to_utf8_buffer().size() > 24:
			continue
		players.append({"id": id, "name": nick})
	if game == _game:
		for previous in _players:
			if not players.any(func(player: Dictionary): return player.id == previous.id):
				players.append(previous)
	if _dialog.visible and (game != _game or players != _players):
		if _busy:
			return
		_dialog.hide()
	_game = game
	_token = token
	_players = players


func set_available(available: bool) -> void:
	_button.visible = available and not _players.is_empty() and not _token.is_empty()
	if not available:
		_dialog.hide()


func open_dialog() -> void:
	if _busy or _players.is_empty():
		return
	_target.clear()
	for player in _players:
		_target.add_item(player.name)
	_reason.text = ""
	_reset_submission()
	_status.text = "Game: " + _game
	_dialog.get_cancel_button().disabled = false
	_dialog.get_cancel_button().text = "Cancel"
	_dialog.popup_centered(Vector2i(520, 410))
	_reason.grab_focus()


func _reset_submission() -> void:
	if _busy:
		return
	_key = ""
	_payload = ""
	if _dialog != null:
		_dialog.get_ok_button().disabled = _reason.text.strip_edges().is_empty() or _reason.text.length() > 1000


func submit() -> void:
	if _busy or _target.selected < 0 or _target.selected >= _players.size():
		return
	if _reason.text.strip_edges().is_empty() or _reason.text.length() > 1000:
		_status.text = "Enter a reason of 1–1,000 characters."
		return
	var problem: String = load("res://scripts/online_lobby.gd").lobby_url_problem(lobby_url)
	if not problem.is_empty():
		_status.text = "The lobby address is invalid."
		return
	if _key.is_empty():
		var random := Crypto.new().generate_random_bytes(16)
		if random.size() != 16:
			_status.text = "Cannot prepare the report. Please try again."
			return
		_key = random.hex_encode()
		_payload = JSON.stringify({"v": 1, "game_id": _game, "target": _players[_target.selected].id, "reason": _reason.text})
	_busy = true
	_dialog.get_ok_button().disabled = true
	_dialog.get_cancel_button().disabled = true
	_target.disabled = true
	_reason.editable = false
	_status.text = "Sending…"
	_client.lobby_url = lobby_url
	_client.token = _token
	_client.post_oneshot("/v1/reports", _payload, PackedStringArray(["idempotency-key: " + _key]))


func _unlock() -> void:
	_busy = false
	_dialog.get_ok_button().disabled = false
	_dialog.get_cancel_button().disabled = false
	_target.disabled = false
	_reason.editable = true


func _completed(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_unlock()
	if result == -1:
		_status.text = "Cannot reach the lobby. Try again to resend this report safely."
		return
	if result != RESULT_SUCCESS or code >= 300 and code < 400:
		_status.text = "The lobby did not confirm receipt. Try again to resend this report safely."
		return
	var data: Variant = JSON.parse_string(body.get_string_from_utf8())
	if not data is Dictionary or data.get("v") != 1:
		_status.text = "The lobby returned an invalid report receipt."
		return
	var error_code: Variant = data.get("error", "")
	if error_code is Dictionary:
		error_code = error_code.get("code", "")
	if code == 201 and data.get("report_id") is String and RegEx.create_from_string("^R-[0-9a-f]{24}$").search(data.report_id) != null and data.get("received_at") is float and data.received_at >= 0 and data.received_at == floor(data.received_at):
		_status.text = "Report received. Thank you."
		_dialog.get_ok_button().disabled = true
		_dialog.get_cancel_button().text = "Close"
		received.emit(data.report_id)
	elif code == 429:
		_status.text = "The report limit has been reached. Please try again later."
	elif code == 409 and error_code == "already_reported":
		_status.text = "You have already reported this player in this game."
	elif code == 401:
		_status.text = "Sign in to the lobby again before sending a report."
	elif code == 403 or code == 404:
		_status.text = "This game is not available for reporting with your current session."
	else:
		_status.text = "The lobby did not accept the report. Check the reason and try again."
