extends SceneTree

class WaveWorld extends Node:
 func get_player_table() -> Array:
  return [{'id':4,'key':'Player_4','label':'Player_4 (Men)','displayName':'Hailey','isAI':true}]
 func get_start_positions() -> Dictionary:
  return {'starts':[{'start':2,'player':4},{'start':5,'player':-1}]}

func _initialize() -> void:
 call_deferred('_run')

func _run() -> void:
 var screen := PanelContainer.new()
 screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 root.add_child(screen)
 var box := VBoxContainer.new()
 screen.add_child(box)
 var picker = load('res://scripts/player_table.gd').new()
 var rows: Array = [
  {'id':0,'key':'NeutralFaction','label':'NeutralFaction','faction':'','displayName':'','isAI':false},
  {'id':4,'key':'Player_4','label':'Player_4 (Men)','faction':'Men','displayName':'Hailey','isAI':false},
  {'id':9,'key':'CreepFaction','label':'CreepFaction','faction':'Creeps','displayName':'','isAI':true}]
 picker.setup(box,'Owner player',rows,4)
 assert(picker.value == 4)
 assert(picker.table.get_selected().get_text(0) == 'Player_4 (Men)')
 picker.value = 9
 assert(picker.table.get_selected().get_metadata(0) == 9)
 assert(picker.label_for(0) == 'NeutralFaction')
 assert(picker.label_for(99) == 'Unavailable player')
 var changed: Array = []
 picker.value_changed.connect(func(value):changed.append(value))
 picker.table.get_root().get_child(0).select(0)
 assert(picker.value == 0 and changed == [0.0])
 picker.set_rows(rows.slice(0,2))
 assert(picker.table.get_selected().get_text(0) == 'NeutralFaction')
 var backing := SpinBox.new()
 backing.min_value=1;backing.max_value=16;backing.value=2
 box.add_child(backing)
 var caption := Label.new();box.add_child(caption);backing.set_meta('label',caption)
 var wave_world := WaveWorld.new()
 load('res://scripts/player_table.gd').attach_start_picker(wave_world,backing)
 var wave_picker = box.get_child(box.get_child_count()-1)
 assert(wave_picker.label_for(2)=='Player_4 (Men)')
 assert(wave_picker.label_for(5)=='Unassigned start 5')
 wave_picker.table.get_root().get_child(1).select(0)
 assert(backing.value==5)
 assert(not backing.visible)
 wave_world.free()
 assert(world_method_available())
 assert(load('res://scripts/map_editor.gd').can_instantiate())
 if OS.get_cmdline_user_args().has('--screenshot'):
  root.size=Vector2i(1000,450)
  await process_frame
  await process_frame
  root.get_texture().get_image().save_png('/tmp/openbfme-player-table.png')
 print('Player table: PASS (named rows, sparse IDs, selection and editor loading)')
 quit()

func world_method_available() -> bool:
 var world = ClassDB.instantiate('GameWorld')
 var available: bool = world.has_method('get_player_table') and world.get_player_table().is_empty()
 world.free()
 return available
