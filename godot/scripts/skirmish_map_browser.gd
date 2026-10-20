extends CanvasLayer
## Local skirmish map browser. Selection is committed only with Use map or Play.
var install_button: Button
var favorite_button: Button
var snow_filter: CheckButton
var favorites_filter: CheckButton
var favorites := {}
var favorites_path := "user://map-favorites.cfg"
var install_root := "user://installed-maps"
var source_tabs: TabBar
var loaded_catalog: Array = []
var library_catalog: Array = []
var library_queue: Array = []
var library_error := ""
var metadata: RefCounted
var metadata_cache_root := "user://map-library-metadata"
var library_path := OS.get_environment("HOME").path_join("Library/Application Support/BFMELauncher/Maps/Library/rotwk") if OS.get_name()=="macOS" else "user://map-library/rotwk"
var use_button: Button
var play_button: Button
var tabs: TabContainer
var players_page: VBoxContainer
var rules_page: VBoxContainer
var draft := {}
var rule_edits := {}
var shell: Node
var fs: RefCounted
var root: Control
var panel: PanelContainer
var search: LineEdit
var players_filter: OptionButton
var feature_filter: OptionButton
var definitions: RefCounted
var definition_queue: Array = []
var definition_scan_running := false
var definition_frame := 0
var type_filter: OptionButton
var sort: OptionButton
var maps: ItemList
var description: RichTextLabel
var stats: Label
var title: Label
var message: Label
var slot: OptionButton
var preview_container: SubViewportContainer
var viewport: SubViewport
var scene: Node3D
var camera: Camera3D
var starts_layer: Control
var builder: RefCounted
var resolver: Node
var fortress: Node3D
var fortress_model := ''
var terrain: Node3D
var loaded_key := ''
var scene_info := {}
var objects_builder: RefCounted
var objects: Node3D
var environment: WorldEnvironment
var sun: DirectionalLight3D
var catalog: Array = []
var filtered: Array = []
var selected: Dictionary = {}
var start_buttons: Array[Dictionary] = []
var assignments := {}
var previous_shell_processing := true
var previous_shell_visible := true
var last_preview_size := Vector2.ZERO
var previous_input := true
var revision := 0
var center := Vector3.ZERO
var distance := 3000.0
var yaw := 0.0
var pitch := .85
var open := false

func _ready():
 layer=30
 root=Control.new();root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);add_child(root)
 var shade=ColorRect.new();shade.color=Color(0,0,0,.8);shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);root.add_child(shade)
 panel=PanelContainer.new();root.add_child(panel)
 var theme=Theme.new();theme.default_font_size=22;panel.theme=theme
 var style=StyleBoxFlat.new();style.bg_color=Color('#10191e');style.border_color=Color('#c4a76a');style.set_border_width_all(2);style.set_corner_radius_all(10);style.content_margin_left=18;style.content_margin_right=18;style.content_margin_top=14;style.content_margin_bottom=14;panel.add_theme_stylebox_override('panel',style)
 var column=VBoxContainer.new();panel.add_child(column)
 var header=HBoxContainer.new();column.add_child(header)
 title=Label.new();title.text='SKIRMISH · MAP EXPLORER';title.size_flags_horizontal=Control.SIZE_EXPAND_FILL;header.add_child(title)
 favorite_button=Button.new();favorite_button.text='☆ Favorite';favorite_button.pressed.connect(_toggle_favorite);header.add_child(favorite_button)
 var close=Button.new();close.text='Cancel';close.pressed.connect(close_browser);header.add_child(close)
 var top=HBoxContainer.new();top.custom_minimum_size.y=280;top.size_flags_vertical=Control.SIZE_EXPAND_FILL;top.size_flags_stretch_ratio=.8;
 tabs=TabContainer.new();tabs.size_flags_vertical=Control.SIZE_EXPAND_FILL;tabs.size_flags_stretch_ratio=.8;column.add_child(tabs)
 var map_page=VBoxContainer.new();map_page.name='Map selection';tabs.add_child(map_page)
 source_tabs=TabBar.new();source_tabs.add_tab('Loaded maps');source_tabs.add_tab('Map Library');map_page.add_child(source_tabs)
 source_tabs.tab_changed.connect(_change_source)
 map_page.add_child(top)
 players_page=VBoxContainer.new();players_page.name='Players';tabs.add_child(players_page)
 rules_page=VBoxContainer.new();rules_page.name='Rules';tabs.add_child(rules_page)
 tabs.tab_changed.connect(func(_i):_update_starts())
 var left=VBoxContainer.new();left.size_flags_horizontal=Control.SIZE_EXPAND_FILL;left.size_flags_stretch_ratio=1.1;top.add_child(left)
 search=LineEdit.new();search.placeholder_text='Search maps…';search.clear_button_enabled=true;search.text_changed.connect(func(_t):_filter());left.add_child(search)
 var options=GridContainer.new();options.columns=2;left.add_child(options)
 players_filter=OptionButton.new();players_filter.tooltip_text='Filter by player count';players_filter.size_flags_horizontal=Control.SIZE_EXPAND_FILL;players_filter.item_selected.connect(func(_i):_filter());options.add_child(players_filter)
 type_filter=OptionButton.new();type_filter.tooltip_text='Official or custom maps';type_filter.size_flags_horizontal=Control.SIZE_EXPAND_FILL;type_filter.item_selected.connect(func(_i):_filter());options.add_child(type_filter)
 for label in ['All maps','Official','Custom']:type_filter.add_item(label)
 feature_filter=OptionButton.new();feature_filter.tooltip_text='Filter by scanned map features';feature_filter.size_flags_horizontal=Control.SIZE_EXPAND_FILL;feature_filter.item_selected.connect(func(_i):_filter());options.add_child(feature_filter)
 for label in ['Any layout','Fortress maps','Standard maps','With outposts','With inns','With signal fires']:feature_filter.add_item(label)
 sort=OptionButton.new();sort.tooltip_text='Sort the map list';sort.size_flags_horizontal=Control.SIZE_EXPAND_FILL;sort.item_selected.connect(func(_i):_filter());options.add_child(sort)
 for label in ['Name A–Z','Name Z–A','Fewest players','Most players','Official first']:sort.add_item(label)
 var toggles=VBoxContainer.new();left.add_child(toggles)
 snow_filter=CheckButton.new();snow_filter.text='Snow maps only';snow_filter.tooltip_text='Maps with snowy weather in their map data';snow_filter.toggled.connect(func(_on):_filter());toggles.add_child(snow_filter)
 favorites_filter=CheckButton.new();favorites_filter.text='Favorites only';favorites_filter.toggled.connect(func(_on):_filter());toggles.add_child(favorites_filter)
 for control in [players_filter,type_filter,feature_filter,sort]:control.fit_to_longest_item=false;control.custom_minimum_size.x=140
 maps=ItemList.new();maps.size_flags_vertical=Control.SIZE_EXPAND_FILL;maps.item_selected.connect(_select);left.add_child(maps)
 var middle=VBoxContainer.new();middle.size_flags_horizontal=Control.SIZE_EXPAND_FILL;middle.size_flags_stretch_ratio=1.4;top.add_child(middle)
 var caption=Label.new();caption.text='DESCRIPTION';middle.add_child(caption)
 description=RichTextLabel.new();description.size_flags_vertical=Control.SIZE_EXPAND_FILL;middle.add_child(description)
 var right=VBoxContainer.new();right.size_flags_horizontal=Control.SIZE_EXPAND_FILL;top.add_child(right)
 caption=Label.new();caption.text='MAP DATA';right.add_child(caption)
 stats=Label.new();stats.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;right.add_child(stats)
 preview_container=SubViewportContainer.new();preview_container.stretch=true;preview_container.size_flags_vertical=Control.SIZE_EXPAND_FILL;preview_container.size_flags_stretch_ratio=1.2;preview_container.custom_minimum_size.y=260;preview_container.gui_input.connect(_preview_input);column.add_child(preview_container)
 viewport=SubViewport.new();viewport.own_world_3d=true;viewport.render_target_update_mode=SubViewport.UPDATE_DISABLED;preview_container.add_child(viewport)
 scene=Node3D.new();viewport.add_child(scene)
 camera=Camera3D.new();camera.current=true;camera.near=5;camera.far=100000;camera.fov=45;scene.add_child(camera)
 environment=WorldEnvironment.new();environment.environment=Environment.new();environment.environment.background_mode=Environment.BG_COLOR;environment.environment.background_color=Color('#0b1114');environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;scene.add_child(environment)
 sun=DirectionalLight3D.new();scene.add_child(sun)
 starts_layer=Control.new();starts_layer.mouse_filter=Control.MOUSE_FILTER_IGNORE;starts_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);preview_container.add_child(starts_layer)
 var footer=HBoxContainer.new();column.add_child(footer)
 slot=OptionButton.new();slot.fit_to_longest_item=false;slot.custom_minimum_size.x=240;slot.item_selected.connect(func(_i):_update_starts());footer.add_child(slot)
 var random=Button.new();random.text='Random start';random.pressed.connect(func():_assign(slot.get_item_id(slot.selected),-1));footer.add_child(random)
 message=Label.new();message.size_flags_horizontal=Control.SIZE_EXPAND_FILL;message.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;footer.add_child(message)
 install_button=Button.new();install_button.text='Install map';install_button.pressed.connect(_install_selected);footer.add_child(install_button)
 var use=Button.new();use_button=use;use.text='Use map';use.custom_minimum_size=Vector2(150,48);use.pressed.connect(_apply);footer.add_child(use)
 var play=Button.new();play_button=play;play.name='Play';play.text='Play';play.tooltip_text='Use this map and setup, then start the game';play.custom_minimum_size=Vector2(150,48)
 var play_style=StyleBoxFlat.new();play_style.bg_color=Color('#4a3a14');play_style.border_color=Color('#e8c36d');play_style.set_border_width_all(2);play_style.set_corner_radius_all(4)
 play.add_theme_stylebox_override('normal',play_style)
 var play_hover:StyleBoxFlat=play_style.duplicate();play_hover.bg_color=Color('#6a5320');play.add_theme_stylebox_override('hover',play_hover);play.add_theme_stylebox_override('pressed',play_hover)
 play.add_theme_color_override('font_color',Color('#fff2c8'));play.pressed.connect(_play);footer.add_child(play)
 builder=ClassDB.instantiate('MapTerrainBuilder');root.hide();set_process(false)

func show_browser():
 _load_favorites()
 loaded_catalog=shell.lobby_map_catalog();catalog=loaded_catalog
 if fs.is_bfme1():
  var default_library=OS.get_environment("HOME").path_join("Library/Application Support/BFMELauncher/Maps/Library/rotwk")
  if library_path==default_library:library_path=default_library.get_base_dir().path_join('bfme1')
  if library_path=='user://map-library/rotwk':library_path='user://map-library/bfme1'
  if install_root=='user://installed-maps':install_root='user://bfme1-installed-maps'
 source_tabs.set_tab_disabled(1,false)
 source_tabs.set_current_tab(0)
 _discover_library()
 _prepare_definitions()
 previous_input=shell.is_processing_input();shell.set_process_input(false)
 previous_shell_processing=shell.is_processing();previous_shell_visible=shell.visible
 shell.set_process(false);shell.hide()
 var state:Dictionary=shell.lobby_state();draft=state.duplicate(true);rule_edits={};slot.clear();assignments={};tabs.current_tab=0
 # Sixteen editable local slots, while the retail lobby keeps eight until committed.
 while draft.slots.size()<16:
  var extra:Dictionary=state.slots[1].duplicate(true)
  extra.state=1;extra.name='';extra.start=-1;extra.color=-1;extra.team=-1
  draft.slots.append(extra)
 _setup_tabs()
 for i in state.get('slots',[]).size():
  var entry=state.slots[i]
  slot.add_item(str(entry.name) if not str(entry.name).is_empty() else 'Player %d'%(i+1),i)
 if slot.item_count==0:slot.add_item('Player 1',0)
 players_filter.clear();players_filter.add_item('Any players',0)
 var counts={}
 for entry in catalog:
  if int(entry.players)>0:counts[int(entry.players)]=true
 var sorted_counts=counts.keys();sorted_counts.sort()
 for n in sorted_counts:players_filter.add_item('%d starts (16 players max)'%n if int(n)>16 else '%d players'%n,n)
 search.text='';open=true;root.show();set_process(true);_filter()
 for i in filtered.size():
  if filtered[i].key==state.get('map',''):maps.select(i);_select(i);return
 if not filtered.is_empty():maps.select(0);_select(0)
 else:_empty_selection('No skirmish maps available.')

func close_browser():
 revision+=1;open=false;root.hide();set_process(false);viewport.render_target_update_mode=SubViewport.UPDATE_DISABLED
 if is_instance_valid(shell):
  shell.set_process_input(previous_input);shell.set_process(previous_shell_processing);shell.visible=previous_shell_visible
 # The built preview stays: reopening on the same map shows it at once.

func _filter():
 var previous=filtered.duplicate();filtered=[]
 var query=search.text.strip_edges().to_lower()
 var players=players_filter.get_selected_id() if players_filter.selected>=0 else 0
 for entry in catalog:
  if favorites_filter.button_pressed and not favorites.has(_favorite_id(entry)):continue
  if not query.is_empty() and query not in (str(entry.name)+' '+str(entry.players)).to_lower():continue
  if players>0 and int(entry.players)!=players:continue
  if type_filter.selected==1 and not entry.official or type_filter.selected==2 and entry.official:continue
  var definition:Dictionary=entry.get('definition',{})
  if snow_filter.button_pressed and not entry.get('snowy',definition.get('snowy',false)):continue
  if feature_filter.selected>0:
   if not definition.get('ok',false):continue
   if feature_filter.selected==1 and not definition.is_fortress:continue
   if feature_filter.selected==2 and definition.is_fortress:continue
   var categories={3:'outpost',4:'inn',5:'signal_fire'}
   if categories.has(feature_filter.selected) and int(definition.counts.get(categories[feature_filter.selected],0))==0:continue
  filtered.append(entry)
 filtered.sort_custom(_sort_order)
 var same_order=previous.size()==filtered.size() and maps.item_count==filtered.size()
 if same_order:
  for i in filtered.size():
   if previous[i].key!=filtered[i].key:same_order=false;break
 if not same_order:maps.clear()
 for i in filtered.size():
  var label=_map_label(filtered[i])
  if same_order:
   if maps.get_item_text(i)!=label:maps.set_item_text(i,label)
  else:maps.add_item(label)
  maps.set_item_tooltip(i,str(filtered[i].get('path',filtered[i].key)))
 for i in filtered.size():
  if filtered[i].key==selected.get('key',''):maps.select(i);maps.ensure_current_is_visible();break

func _sort_order(a: Dictionary, b: Dictionary) -> bool:
 var na=_plain_name(a);var nb=_plain_name(b)
 match sort.selected:
  1:return na.naturalnocasecmp_to(nb)>0
  2:if a.players!=b.players:return a.players<b.players
  3:if a.players!=b.players:return a.players>b.players
  4:if a.official!=b.official:return a.official
 return na.naturalnocasecmp_to(nb)<0

# Retail marks some names with a leading '*' (the besiege maps); sorting ignores it.
func _plain_name(entry: Dictionary) -> String:
 return _display_name(entry).lstrip('* ')

func _select(index: int):
 var same=selected.get('key','')==filtered[index].key
 selected=filtered[index];revision+=1
 if selected.get('library',false):
  var inspected:Dictionary=fs.inspect_library_map(selected.path,true)
  if not inspected.get('ok',false):
   _empty_selection('Library map could not be loaded: '+str(inspected.get('error','')));return
  selected.merge(inspected,true)
  var cache_error=metadata.save(inspected)
  if not cache_error.is_empty():push_warning(cache_error)
  selected.definition=definitions.cached(selected)
 _update_actions()
 for i in definition_queue.size():
  if definition_queue[i].key==selected.key:
   var priority=definition_queue.pop_at(i);definition_queue.push_front(priority);break
 if not same:assignments={}
 if assignments.is_empty() and selected.key==draft.get('map',''):
  for i in draft.slots.size():assignments[i]=int(draft.slots[i].start)
 _setup_players()
 description.text=str(selected.description);title.text=_display_name(selected)
 stats.text='%d starts · up to %d players\n%s'%[selected.players,mini(16,int(selected.players)),'Official map' if selected.official else 'Custom map']
 _update_definition_stats()
 _clear_markers()
 if loaded_key==selected.key and is_instance_valid(terrain):_show_scene();_full_scan(revision);return
 message.text='Loading 3D map…'
 _load_preview.call_deferred(revision)

func _clear_markers():
 start_buttons.clear()
 for child in starts_layer.get_children():child.free()

func _clear_scene():
 _clear_markers()
 if is_instance_valid(objects):objects.free()
 if is_instance_valid(terrain):terrain.free()
 if is_instance_valid(fortress):fortress.free()
 terrain=null;objects=null;fortress=null;fortress_model='';loaded_key='';scene_info={}

func _setup_objects() -> bool:
 if objects_builder==null:
  objects_builder=ClassDB.instantiate('MapObjectBuilder')
  var object_setup:Dictionary=objects_builder.setup(fs)
  if not object_setup.get('ok',false):message.text='Object templates failed to load: '+str(object_setup.get('errors',[]));objects_builder=null
 return objects_builder!=null

func _load_preview(token: int):
 # A short pause so scrolling through the list does not build every map it passes.
 await get_tree().create_timer(.12).timeout
 if token!=revision or not open:return
 _build_scene(selected)
 if token!=revision or not open:return
 _show_scene()
 if selected.get('library',false) and selected.get('definition',{}).is_empty():
  selected.definition=definitions.generate(selected,objects_builder);_update_definition_stats()
 _full_scan(token)

## A map whose own map.ini defines the objects it places is scanned once it is selected and its preview is up (the background pass skips it:
## its scan loads a second template world and the map.ini, a pause of seconds).
func _full_scan(token: int):
 if not selected.get('definition',{}).get('needs_map_ini',false):return
 message.text='Scanning map features…'
 await get_tree().process_frame
 if token!=revision or not open:return
 selected.definition=definitions.generate(selected);_update_definition_stats()
 if feature_filter.selected>0 or snow_filter.button_pressed:_filter()
 message.text='Drag to orbit · wheel to zoom · set each fortress owner on its card'

## Builds terrain, objects and fortresses of one catalog entry into the preview scene (no UI).
func _build_scene(entry: Dictionary):
 print('MAP PREVIEW ',entry.get('path',entry.key))
 _clear_scene()
 var key=str(entry.get('preview_name',str(entry.key).replace('\\','/').get_file().get_basename()))
 var probes:=PackedVector2Array()
 for start in entry.starts:probes.append(Vector2(start.x,start.y))
 terrain=builder.build_map(fs,key,{'markers':false,'fog':false,'height_probes':probes})
 var report:Dictionary=builder.get_report()
 if not terrain:scene_info={'error':'Preview unavailable: '+str(report.get('errors',[]))};return
 scene.add_child(terrain)
 var info:Dictionary=report.get('map_info',{});var view:Dictionary=report.get('camera',{})
 var sx=float(view.get('size_x',1000));var sy=float(view.get('size_y',1000))
 # Only the playable area: the border outside the map's camera boundary is cut away from terrain, water and roads, and its objects are left out.
 _clip_materials(terrain,Vector4(0,0,sx,sy))
 var object_error=''
 if _setup_objects():
  objects=objects_builder.build_objects(fs,key,{'clip_rect':Rect2(0,0,sx,sy),'skybox':false,'map_ini':'objects','animations':false,'texture_animation':false})
  if objects:terrain.add_child(objects);_light(objects_builder.get_report().get('lighting',{}))
  else:object_error='Objects unavailable: '+str(objects_builder.get_report().get('errors',[]))
 fortress=ClassDB.instantiate('W3DInstancer');scene.add_child(fortress);fortress.setup(fs);fortress.set_auto_update(false)
 if not is_instance_valid(resolver):
  resolver=ClassDB.instantiate('GameWorld');add_child(resolver)
  if not resolver.setup(fs).get('ok',false):resolver.free();resolver=null
 var heights:PackedFloat32Array=info.get('probe_heights',PackedFloat32Array())
 var points=[]
 for n in entry.starts.size():points.append(Vector3(entry.starts[n].x,heights[n],-entry.starts[n].y))
 loaded_key=entry.key
 scene_info={'info':info,'center':view.get('center',Vector3.ZERO),'sx':sx,'sy':sy,'points':points,'object_error':object_error}
 _refresh_fortresses(entry)

## Shows the built scene for `selected`: camera, statistics and the clickable start positions.
func _show_scene():
 if scene_info.has('error'):message.text=scene_info.error;return
 _clear_markers()
 _refresh_fortresses(selected)
 var info:Dictionary=scene_info.info;var sx:float=scene_info.sx;var sy:float=scene_info.sy
 center=scene_info.center;distance=maxf(sx,sy)*1.15;pitch=.85;yaw=0;_camera_pose()
 stats.text='%d starts · %s\n%.0f × %.0f map units\n%d × %d height samples\nElevation %.0f–%.0f\n%d objects · %d roads\n%d terrain textures'%[selected.players,'Official' if selected.official else 'Custom',sx,sy,info.get('width',0),info.get('height',0),info.get('elevation_min',0),info.get('elevation_max',0),info.get('objects',0),info.get('road_segments',0),info.get('texture_classes',0)]
 _update_definition_stats()
 for n in selected.starts.size():
  var start=selected.starts[n]
  var button=Button.new();button.text='Start %d'%(int(start.index)+1);button.custom_minimum_size=Vector2(90,42)
  button.pressed.connect(func():_assign(slot.get_item_id(slot.selected),int(start.index)));starts_layer.add_child(button)
  var anchor=PanelContainer.new();starts_layer.add_child(anchor)
  var card_style=StyleBoxFlat.new();card_style.bg_color=Color('#10191ef5');card_style.border_color=Color('#736344');card_style.set_border_width_all(1);card_style.set_corner_radius_all(4);card_style.content_margin_left=6;card_style.content_margin_right=6;card_style.content_margin_top=4;card_style.content_margin_bottom=4;anchor.add_theme_stylebox_override('panel',card_style)
  var card=VBoxContainer.new();card.custom_minimum_size.x=220;anchor.add_child(card)
  _fortress_controls(card,int(start.index))
  start_buttons.append({'anchor':anchor,'card':card,'button':button,'point':scene_info.points[n]+Vector3(0,260,0),'index':int(start.index)})
 viewport.render_target_update_mode=SubViewport.UPDATE_ONCE;_update_starts()
 message.text=scene_info.object_error if not str(scene_info.object_error).is_empty() else 'Drag to orbit · wheel to zoom · set each fortress owner on its card'

# The map's GlobalLighting sun and ambient, as map_viewer.gd lights its objects.
func _light(lighting: Dictionary):
 environment.environment.ambient_light_color=lighting.get('ambient',Color(.6,.6,.6))
 sun.light_color=lighting.get('diffuse',Color.WHITE)
 var dir:Vector3=lighting.get('direction',Vector3(-1,-1,-1)).normalized()
 sun.transform=Transform3D(Basis.looking_at(dir,Vector3.UP if absf(dir.y)<.99 else Vector3.RIGHT),Vector3.ZERO)

func _clip_materials(node: Node, rect: Vector4):
 if node is MeshInstance3D:
  var materials=[node.material_override]
  if node.mesh:
   for i in node.mesh.get_surface_count():materials.append(node.mesh.surface_get_material(i));materials.append(node.get_surface_override_material(i))
  for material in materials:
   if material is ShaderMaterial:material.set_shader_parameter('clip_rect',rect)
 for child in node.get_children():_clip_materials(child,rect)

func _update_starts():
 var chosen=int(assignments.get(slot.get_selected_id(),-1)) if slot.selected>=0 else -1
 for item in start_buttons:
  item.button.modulate=Color('#e8c36d') if item.index==chosen else Color.WHITE
  # The fortress cards (owner, AI, faction, team) show on every tab; their owner choice also assigns the start, so the plain buttons stay hidden.
  item.button.visible=false

func _camera_pose():
 camera.position=center+Vector3(sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch))*distance;camera.look_at(center)
 _request_preview_draw()

func _request_preview_draw():
 if open:viewport.render_target_update_mode=SubViewport.UPDATE_ONCE

func _preview_input(event: InputEvent):
 if event is InputEventMouseMotion and event.button_mask&MOUSE_BUTTON_MASK_LEFT:
  yaw-=event.relative.x*.006;pitch=clampf(pitch+event.relative.y*.004,.25,1.5);_camera_pose();preview_container.accept_event()
 elif event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP,MOUSE_BUTTON_WHEEL_DOWN]:
  distance=clampf(distance*(.9 if event.button_index==MOUSE_BUTTON_WHEEL_UP else 1.1),200,30000);_camera_pose();preview_container.accept_event()

func _process(_delta):
 # One map scan (about 20 ms) every other frame while the browser is open; the selected map is queued first.
 if not library_queue.is_empty() and source_tabs.current_tab==1:
  _scan_library()
 if definition_scan_running:
  definition_frame+=1
  if definition_frame%2==0:_scan_definitions()
 if preview_container.size!=last_preview_size:
  last_preview_size=preview_container.size;_request_preview_draw()
 var size=get_viewport().get_visible_rect().size
 panel.position=size*.035;panel.size=size*.93
 for item in start_buttons:
  var visible_point=not camera.is_position_behind(item.point)
  item.button.visible=false
  item.anchor.visible=visible_point
  var projected=camera.unproject_position(item.point)
  item.button.position=projected-item.button.size*.5
  item.anchor.position=(projected-Vector2(item.anchor.size.x*.5,item.anchor.size.y)).clamp(Vector2.ZERO,(preview_container.size-item.anchor.size).max(Vector2.ZERO))

func _input(event: InputEvent):
 if open and event is InputEventKey and event.pressed and event.keycode==KEY_ESCAPE:close_browser();get_viewport().set_input_as_handled()

func _apply():
 if _commit():close_browser()

## Play: commit like Use map, then press the lobby's own start (the movie's AptSkirmish::StartGame command: the lobby validates the setup on its next
## update and shows its refusal message itself).
func _play():
 if not _commit():return
 close_browser()
 if not shell.shell_fscommand('AptSkirmish::StartGame',''):message.text='The lobby could not start the game.'

## Writes the browser's map, slots and rules into the lobby; false (with the errors shown) when the lobby refuses them.
func _commit() -> bool:
 if selected.is_empty():return false
 if selected.get('library',false):
  message.text='Install/add this map to the main list, then select it under Loaded maps to play.';return false
 var slots=[]
 for i in mini(mini(int(selected.players),16),draft.slots.size()):
  var entry=draft.slots[i]
  var change={'slot':i,'start':int(assignments.get(i,-1))}
  if i>0:change.state=entry.state
  if int(entry.state)>=2:
   change.team=entry.team
   for faction in draft.get('factions',[]):
    if int(faction.value)==int(entry.template):change.faction=faction.template
  slots.append(change)
 var rules=[]
 for key in rule_edits:rules.append({'id':int(key),'value':rule_edits[key]})
 var result:Dictionary=shell.lobby_apply({'browser_slot_count':16 if int(selected.players)>8 else 8,'map_search':'','map':selected.key,'slots':slots,'browser_rules':rules,'browser_fortress_construction':draft.get('fortress_construction',false),'browser_legendary':draft.get('legendary_heroes',1),'browser_peace_time':draft.get('peace_time_minutes',0)})
 if not result.get('ok',false):message.text=str(result.get('errors',[]))
 return result.get('ok',false)

func _exit_tree():
 if open and is_instance_valid(shell):
  shell.set_process_input(previous_input);shell.set_process(previous_shell_processing);shell.visible=previous_shell_visible

# Definitions come from the same source as the APT lobby; adding a rule there adds a row here.
func _choice(parent: Node, choices: Array, value: int, changed: Callable) -> OptionButton:
 var control=OptionButton.new();control.size_flags_horizontal=Control.SIZE_EXPAND_FILL
 control.add_theme_font_size_override('font_size',16)
 for entry in choices:
  control.add_item(str(entry.label) if not str(entry.label).strip_edges().is_empty() else 'Human player')
  control.set_item_metadata(control.item_count-1,int(entry.value))
  if int(entry.value)==value:control.select(control.item_count-1)
 control.item_selected.connect(func(i):changed.call(int(control.get_item_metadata(i))))
 parent.add_child(control)
 return control

func _setup_tabs():
 for page in [players_page,rules_page]:
  for child in page.get_children():page.remove_child(child);child.queue_free()
 _setup_players()
 var scroll=ScrollContainer.new();scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;rules_page.add_child(scroll)
 var rows=VBoxContainer.new();rows.size_flags_horizontal=Control.SIZE_EXPAND_FILL;scroll.add_child(rows)
 for definition in draft.get('rule_definitions',[]):
  var row=HBoxContainer.new();rows.add_child(row)
  var label=Label.new();label.text=definition.label;label.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(label)
  var id=int(definition.id)
  if definition.choices.is_empty():
   var check=CheckButton.new();check.button_pressed=int(rule_edits.get(id,definition.value))!=0;row.add_child(check)
   check.toggled.connect(func(on):rule_edits[id]=int(on))
  else:_choice(row,definition.choices,int(rule_edits.get(id,definition.value)),func(value):rule_edits[id]=value)
 var reset=Button.new();reset.text='Reset %s rules' % draft.get('rules_profile','vanilla');rows.add_child(reset)
 reset.pressed.connect(func():
  for definition in draft.get('rule_definitions',[]):rule_edits[int(definition.id)]=int(definition.default)
  _setup_tabs())
 var extension=Label.new();extension.text='OpenBFME extensions';rows.add_child(extension)
 var row=HBoxContainer.new();rows.add_child(row)
 var label=Label.new();label.text='Legendary Heroes';label.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(label)
 _choice(row,[{'label':'Off','value':1},{'label':'2×','value':2},{'label':'3×','value':3},{'label':'4×','value':4}],int(draft.get('legendary_heroes',1)),func(value):draft.legendary_heroes=value)
 var peace_row=HBoxContainer.new();rows.add_child(peace_row)
 var peace_label=Label.new();peace_label.text='Peace Time';peace_label.size_flags_horizontal=Control.SIZE_EXPAND_FILL;peace_row.add_child(peace_label)
 _choice(peace_row,draft.get('peace_time_choices',[]),int(draft.get('peace_time_minutes',0)),func(value):draft.peace_time_minutes=value)

 # The fortress offers the faction builder's menu and starts paid foundations immediately.
 var fortress=CheckButton.new();fortress.text='Fortress construction';fortress.button_pressed=bool(draft.get('fortress_construction',false));rows.add_child(fortress)
 fortress.tooltip_text='Select a fortress to place buildings without a builder. Normal prices and build times apply.'
 fortress.toggled.connect(func(on):draft.fortress_construction=on)

func _owner(start: int) -> int:
 for key in assignments:
  if int(assignments[key])==start:return int(key)
 return -1

func _assign(player: int, start: int):
 var old=int(assignments.get(player,-1))
 var occupant=_owner(start) if start>=0 else -1
 if occupant>=0 and occupant!=player:assignments[occupant]=old
 assignments[player]=start
 _refresh_cards()

func _refresh_cards():
 _setup_players()
 for item in start_buttons:
  for child in item.card.get_children():item.card.remove_child(child);child.queue_free()
  _fortress_controls(item.card,item.index)
 _refresh_fortresses(selected)
 _update_starts()

func _fortress_controls(card: VBoxContainer, start: int):
 var choices=[{'label':'Unassigned · Start %d'%(start+1),'value':-1}]
 for i in mini(int(selected.get('players',0)),draft.get('slots',[]).size()):
  var name=str(draft.slots[i].name)
  choices.append({'label':name if not name.is_empty() else 'Player %d'%(i+1),'value':i})
 var owner=_owner(start)
 _choice(card,choices,owner,func(value):
  if value>=0:_assign(value,start)
  elif owner>=0:assignments[owner]=-1;_refresh_cards())
 if owner<0:return
 var entry:Dictionary=draft.slots[owner]
 var states:Array=entry.get('player_choices',[])
 var state_control=_choice(card,states,int(entry.state),func(value):entry.state=value;_refresh_cards())
 state_control.tooltip_text='Player type / AI difficulty';state_control.disabled=owner==0
 var faction_control=_choice(card,draft.get('factions',[]),int(entry.template),func(value):entry.template=value;_refresh_cards())
 faction_control.tooltip_text='Faction'
 _choice(card,draft.get('teams',[]),int(entry.team),func(value):entry.team=value;_refresh_cards()).tooltip_text='Team'

func _refresh_fortresses(entry: Dictionary):
 if not is_instance_valid(resolver) or not scene_info.has('points'):return
 if is_instance_valid(fortress):fortress.free()
 fortress=ClassDB.instantiate('W3DInstancer');scene.add_child(fortress);fortress.setup(fs);fortress.set_auto_update(false);fortress_model=''
 for n in entry.starts.size():
  var owner=_owner(int(entry.starts[n].index));var building='';var side=''
  if owner>=0 and int(draft.slots[owner].state) not in [0,1]:
   for faction in draft.get('factions',[]):
    if int(faction.value)==int(draft.slots[owner].template):building=str(faction.building);side=str(faction.get('side',''))
  # Unassigned, open/closed slots, and unresolved random factions use the retail
  # neutral castle flag at the actual start point, through the same model path.
  if building.is_empty():building='CastleFlag';side=''

  var look:Dictionary=resolver.get_placement_ghost(building,side)
  if str(look.get('model','')).is_empty() and not look.get('castle',false):message.text='Preview model unavailable: '+building;continue
  if not str(look.get('castle_error','')).is_empty():message.text=look.castle_error;continue
  var pieces:Array=look.get('pieces',[]) if look.get('castle',false) else [{'model':look.get('model',''),'x':0,'y':0,'z':0,'angle':0,'hidden':look.get('hidden',[])}]
  for piece in pieces:
   var model=str(piece.model)
   if model.is_empty():continue
   fortress_model+=model+';'
   var model_id=fortress.add_model(model)
   var p:Vector3=scene_info.points[n]+Vector3(float(piece.x),float(piece.z),-float(piece.y))
   if model_id>=0:
    var instance=fortress.add_instance(model_id,Transform3D(Basis(Vector3.UP,float(piece.angle)),p),'',0,0)
    fortress.set_instance_hidden_subobjects(instance,PackedStringArray(piece.get('hidden',[])))
 fortress.update_now()
 _request_preview_draw()

func _setup_players():
 var current=slot.get_selected_id() if slot.selected>=0 else 0
 slot.clear()
 for i in mini(int(selected.get('players',0)),draft.get('slots',[]).size()):
  var name=str(draft.slots[i].name)
  slot.add_item(name if not name.is_empty() else 'Player %d'%(i+1),i)
  if i==current:slot.select(i)
 for child in players_page.get_children():players_page.remove_child(child);child.queue_free()
 var fill=Button.new();fill.text='Fill players & spread starts';fill.pressed.connect(_fill_players);players_page.add_child(fill)
 var scroll=ScrollContainer.new();scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;players_page.add_child(scroll)
 var rows=VBoxContainer.new();rows.size_flags_horizontal=Control.SIZE_EXPAND_FILL;scroll.add_child(rows)
 var headings=HBoxContainer.new();rows.add_child(headings)
 for text in ['Player','AI level','Faction','Team','Start']:
  var heading=Label.new();heading.text=text;heading.add_theme_font_size_override('font_size',16);heading.modulate=Color('#c4a76a');headings.add_child(heading)
  if text=='Player':heading.custom_minimum_size.x=150
  else:heading.size_flags_horizontal=Control.SIZE_EXPAND_FILL
 for i in mini(int(selected.get('players',0)),draft.get('slots',[]).size()):
  var entry:Dictionary=draft.slots[i]
  var row=HBoxContainer.new();rows.add_child(row)
  var label=Label.new();label.text=str(entry.name) if not str(entry.name).is_empty() else 'Player %d'%(i+1);label.custom_minimum_size.x=150;row.add_child(label)
  var state_control=_choice(row,entry.get('player_choices',[]),int(entry.state),func(value):entry.state=value;_refresh_cards())
  state_control.disabled=i==0;state_control.tooltip_text='Player type / AI difficulty'
  _choice(row,draft.get('factions',[]),int(entry.template),func(value):entry.template=value;_refresh_cards()).tooltip_text='Faction'
  _choice(row,draft.get('teams',[]),int(entry.team),func(value):entry.team=value;_refresh_cards()).tooltip_text='Team'
  var starts=[{'label':'Random start','value':-1}]
  for start in selected.get('starts',[]):starts.append({'label':'Start %d'%(int(start.index)+1),'value':int(start.index)})
  _choice(row,starts,int(assignments.get(i,-1)),func(value):_assign(i,value)).tooltip_text='Fortress position'

func _prepare_definitions():
 if definitions==null:
  definitions=preload('res://scripts/skirmish_map_definitions.gd').new();definitions.setup(fs)
 definition_queue=[]
 for entry in catalog:
  if entry.get('library',false):
   var cached=definitions.cached(entry) if entry.has('preview_name') else {}
   if not cached.is_empty():entry.definition=cached
   continue
  var definition=definitions.cached(entry)
  if definition.is_empty():definition_queue.append(entry)
  else:entry.definition=definition
 definition_scan_running=not definition_queue.is_empty()

func _scan_definitions():
 if not open or definition_queue.is_empty():
  definition_scan_running=false;return
 if not _setup_objects():
  definition_scan_running=false;return
 var entry:Dictionary=definition_queue.pop_front()
 # The preview's object world is only read by the scan, so the scanner needs no template load of its own.
 entry.definition=definitions.generate(entry,objects_builder,false)
 if selected.get('key','')==entry.key:
  selected.definition=entry.definition;_update_definition_stats()
  if loaded_key==entry.key:_full_scan(revision)
 if feature_filter.selected>0 or snow_filter.button_pressed:_filter()
 else:
  for row in filtered.size():
   if filtered[row].key==entry.key:
    maps.set_item_text(row,_map_label(entry));break
 feature_filter.tooltip_text='Map definitions: %d pending'%definition_queue.size() if not definition_queue.is_empty() else 'Filter by scanned map features'
 definition_scan_running=not definition_queue.is_empty()

func _update_definition_stats():
 # Keep the terrain statistics while replacing only the generated definition section.
 stats.text=stats.text.split('\n\nFeatures:')[0]
 var definition:Dictionary=selected.get('definition',{})
 if definition.is_empty():
  stats.text+='\n\nFeatures: scanning…';return
 if definition.get('needs_map_ini',false):
  stats.text+='\n\nFeatures: scanned when the map is shown';return
 if not definition.get('ok',false):
  stats.text+='\n\nFeatures: scan failed\n'+str(definition.get('errors',[]))+'\nUnresolved templates: '+str(definition.get('unresolved_templates',[]));return
 var counts:Dictionary=definition.counts
 stats.text+='\n\nFeatures: %s\n%d outposts · %d inns\n%d signal fires · %d other capturables'%['Fortress map' if definition.is_fortress else 'Standard map',int(counts.get('outpost',0)),int(counts.get('inn',0)),int(counts.get('signal_fire',0)),int(counts.get('other',0))]
 if definition.has('cache_error'):stats.text+='\n'+str(definition.cache_error)

func _update_actions():
 var blocked=selected.is_empty() or selected.get('library',false)
 use_button.disabled=blocked;play_button.disabled=blocked
 install_button.visible=selected.get('library',false)
 install_button.disabled=selected.is_empty() or not selected.has('content_id') or selected.get('installed',false) or int(selected.get('players',0))<2
 install_button.text='Installed' if selected.get('installed',false) else 'Install map'
 favorite_button.disabled=selected.is_empty()
 favorite_button.text='★ Favorited' if favorites.has(_favorite_id(selected)) else '☆ Favorite'
 use_button.tooltip_text='Install/add library maps to the main list first.' if blocked else 'Use this map and setup'
 play_button.tooltip_text=use_button.tooltip_text if blocked else 'Use this map and setup, then start the game'

func _empty_selection(reason: String):
 selected={};revision+=1;_clear_scene();description.text='';stats.text='';message.text=reason
 title.text='MAP LIBRARY' if source_tabs.current_tab==1 else 'SKIRMISH · MAP EXPLORER'
 viewport.render_target_update_mode=SubViewport.UPDATE_ONCE
 _update_actions()

func _discover_library():
 library_catalog=[];library_queue=[];library_error=''
 if metadata==null:
  metadata=preload('res://scripts/skirmish_map_metadata.gd').new();metadata.setup(fs,metadata_cache_root)
 var downloads='user://map-library/bfme1' if fs.is_bfme1() else 'user://map-library/rotwk'
 var bundle_game='bfme1' if fs.is_bfme1() else 'rotwk'
 var bundles=DirAccess.get_directories_at('user://map-library/bundles') if DirAccess.dir_exists_absolute('user://map-library/bundles') else PackedStringArray()
 for bundle in bundles:
  if bundle.begins_with('.') or '.installing-' in bundle:continue
  var folder='user://map-library/bundles/'+bundle+'/'+bundle_game
  if DirAccess.dir_exists_absolute(folder):_discover_library_folder(ProjectSettings.globalize_path(folder))
 if library_path!=downloads and DirAccess.dir_exists_absolute(downloads):_discover_library_folder(ProjectSettings.globalize_path(downloads))
 if not DirAccess.dir_exists_absolute(library_path):
  var status=DirAccess.make_dir_recursive_absolute(library_path)
  if status!=OK:
   library_error='Cannot create Map Library: '+error_string(status);return
 _discover_library_folder(ProjectSettings.globalize_path(library_path))

func _discover_library_folder(path: String):
 var directory=DirAccess.open(path)
 if directory==null:
  library_error='Cannot read Map Library: '+path+' ('+error_string(DirAccess.get_open_error())+')';return
 for filename in directory.get_files():
  if filename.get_extension().to_lower()!='map':continue
  var disk=path.path_join(filename)
  var entry={'key':disk,'path':disk,'name':'','description':'','players':0,'official':false,'library':true,'starts':[]}
  var cached:Dictionary=metadata.cached(disk)
  if not cached.is_empty():entry.merge(cached,true)
  else:library_queue.append(entry)
  library_catalog.append(entry)
 for folder in directory.get_directories():
  if not folder.begins_with('.') and not directory.is_link(folder):_discover_library_folder(path.path_join(folder))

func _change_source(index: int):
 catalog=loaded_catalog if index==0 else library_catalog
 _empty_selection('Select a map to preview.' if index==0 else 'Browse library maps here. Install/add a map to the main list before playing.')
 players_filter.clear();players_filter.add_item('Any players',0)
 for count in range(1,33):players_filter.add_item('%d players'%count,count)
 _prepare_definitions();_filter()
 if not filtered.is_empty():maps.select(0);_select(0)
 elif index==1:message.text=library_error if not library_error.is_empty() else 'No maps in Map Library: '+library_path

func _scan_library():
 var entry:Dictionary=library_queue.pop_front()
 var inspected:Dictionary=fs.inspect_library_map(entry.path,true)
 if inspected.get('ok',false):
  entry.merge(inspected,true)
  var cache_error=metadata.save(inspected)
  if not cache_error.is_empty():push_warning(cache_error)
  var definition:Dictionary=definitions.cached(entry)
  if not definition.is_empty():entry.definition=definition
 else:entry['scan_error']=inspected.get('error','Map parse failed')
 _filter()
 source_tabs.set_tab_tooltip(1,'Scanning %d library maps…'%library_queue.size() if not library_queue.is_empty() else library_path)

func _map_label(entry: Dictionary) -> String:
 var count=str(entry.players) if int(entry.players)>0 else '?'
 return ('★ ' if favorites.has(_favorite_id(entry)) else '')+'%s (%s)%s'%[_display_name(entry),count,' · Unavailable' if entry.has('scan_error') else (' · Fortress' if entry.get('definition',{}).get('is_fortress',false) else '')]

func _display_name(entry: Dictionary) -> String:
 var name=str(entry.get('name','')).strip_edges()
 if not name.is_empty():return name
 if entry.has('scan_error'):return 'Map metadata unavailable'
 return 'Untitled map' if entry.has('content_id') else 'Reading map metadata…'

func _favorite_id(entry: Dictionary) -> String:
 return str(entry.get('content_id',entry.get('key','')))

func _load_favorites():
 var config=ConfigFile.new()
 var status=config.load(favorites_path)
 if status==OK:
  var saved=config.get_value('maps','favorites',{})
  if saved is Dictionary:favorites=saved
  else:message.text='Cannot read map favorites: invalid favorites data.'
 elif status!=ERR_FILE_NOT_FOUND:message.text='Cannot read map favorites: '+error_string(status)

func _toggle_favorite():
 if selected.is_empty():return
 var key=_favorite_id(selected)
 var previous=favorites.duplicate()
 if favorites.has(key):favorites.erase(key)
 else:favorites[key]=true
 var config=ConfigFile.new();config.set_value('maps','favorites',favorites)
 var status=config.save(favorites_path)
 if status!=OK:
  favorites=previous;message.text='Cannot save map favorites: '+error_string(status)
 _update_actions();_filter()

func _install_selected():
 if selected.is_empty() or not selected.get('library',false):return
 install_button.disabled=true;message.text='Installing map…'
 var result:Dictionary=fs.install_library_map(selected.path,install_root)
 if not result.get('ok',false):
  message.text='Install failed: '+str(result.get('error',''));_update_actions();return
 selected.installed=true
 loaded_catalog=shell.lobby_map_catalog()
 search.text='';players_filter.select(0);type_filter.select(0);feature_filter.select(0);favorites_filter.button_pressed=false;snow_filter.button_pressed=false
 source_tabs.current_tab=0
 for i in filtered.size():
  if filtered[i].key==result.key:maps.select(i);_select(i);break
 message.text='Map installed. Select Use map or Play to continue.'

# A sixteen-player draft can use every start marker on a larger composite map.
func _fill_players():
 var count=mini(16,int(selected.get('players',0)))
 assignments.clear()
 for i in draft.slots.size():
  if i>=count:draft.slots[i].state=1;continue
  if i>0:draft.slots[i].state=4
  assignments[i]=floori(float(i)*int(selected.players)/count)
 _setup_players();_refresh_cards()
 message.text='%d players across %d available starts. Matches above 8 players are local; recordings and saves are unavailable.'%[count,int(selected.players)]
