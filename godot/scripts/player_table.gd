## Text player picker. Numeric values remain private engine IDs or start positions.
extends VBoxContainer
signal value_changed(value: float)
var value: float = 0:
 set(next):
  value = next
  _select_value()
var editable := true:
 set(next):
  editable = next
  if table != null:
   table.mouse_filter = Control.MOUSE_FILTER_STOP if next else Control.MOUSE_FILTER_IGNORE
   table.focus_mode = Control.FOCUS_ALL if next else Control.FOCUS_NONE
var table: Tree
var rows: Array = []
var _items := {}

func setup(parent: Node, caption: String, players: Array, selected := 0) -> void:
 parent.add_child(self)
 var label := Label.new()
 label.text = caption
 parent.add_child(label)
 parent.move_child(label, get_index())
 set_meta('label', label)
 table = Tree.new()
 table.columns = 3
 table.column_titles_visible = true
 table.hide_root = true
 table.select_mode = Tree.SELECT_ROW
 table.set_column_title(0, 'Player / faction')
 table.set_column_title(1, 'Name')
 table.set_column_title(2, 'Control')
 table.set_column_custom_minimum_width(0, 170)
 table.set_column_custom_minimum_width(1, 70)
 table.set_column_custom_minimum_width(2, 55)
 table.set_column_expand_ratio(0, 3)
 table.set_column_expand_ratio(1, 2)
 table.set_column_expand_ratio(2, 1)
 table.custom_minimum_size = Vector2(320, 175)
 table.size_flags_horizontal = Control.SIZE_EXPAND_FILL
 add_child(table)
 table.item_selected.connect(func():
  if not editable:
   _select_value()
   return
  value = float(table.get_selected().get_metadata(0))
  value_changed.emit(value))
 value = selected
 set_rows(players)

func set_rows(players: Array) -> void:
 rows = players.duplicate(true)
 if table == null: return
 table.clear()
 _items.clear()
 var root := table.create_item()
 for row in rows:
  var item := table.create_item(root)
  var id := int(row.get('value', row.id))
  item.set_metadata(0, id)
  item.set_text(0, str(row.label))
  item.set_tooltip_text(0, str(row.label))
  item.set_text(1, str(row.get('displayName', '')))
  item.set_text(2, 'AI' if row.get('isAI', false) else ('—' if str(row.key) in ['CreepFaction','NeutralFaction'] else 'Player'))
  _items[id] = item
 _select_value()

func _select_value() -> void:
 if table == null: return
 table.set_block_signals(true)
 table.deselect_all()
 if _items.has(int(value)): _items[int(value)].select(0)
 table.set_block_signals(false)

func label_for(id: int) -> String:
 for row in rows:
  if int(row.get('value', row.id)) == id: return str(row.label)
 return 'Unavailable player'

## Wave ownership is stored as a start position; the visible picker still names players.
static func attach_start_picker(world: Node, spinner: SpinBox) -> void:
 var picker = load('res://scripts/player_table.gd').new()
 var rows: Array = []
 var players: Dictionary = {}
 for player in world.get_player_table(): players[int(player.id)] = player
 for start in world.get_start_positions().get('starts', []):
  var player: Dictionary = players.get(int(start.player), {})
  var row := player.duplicate()
  row['id'] = int(start.start)
  row['value'] = int(start.start)
  row['label'] = player.get('label', 'Unassigned start ' + str(start.start))
  row['key'] = player.get('key', '')
  rows.append(row)
 picker.setup(spinner.get_parent(), 'Wave owner player', rows, int(spinner.value))
 spinner.get_meta('label').hide()
 spinner.hide()
 picker.value_changed.connect(func(next): spinner.value = next)
 spinner.value_changed.connect(func(next): picker.value = next)
 picker.set_meta('backing_spinner', spinner)
 picker.set_process(true)

func _process(_delta: float) -> void:
 if has_meta('backing_spinner'):
  editable = get_meta('backing_spinner').editable
