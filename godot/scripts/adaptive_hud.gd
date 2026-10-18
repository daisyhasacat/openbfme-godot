extends CanvasLayer
## Enhanced HUD. Native APIs retain orders, availability, visibility and selection.
var hud: Callable
var gameplay: Callable
var execute: Callable
var open_menu: Callable
var open_console: Callable
var bridge: Node
var mode := 'classic'
var section := ''
var _top: PanelContainer
var _dock: PanelContainer
var _tabs: GridContainer
var _panel: PanelContainer
var _contents: VBoxContainer
var _grid: GridContainer
var _scroll: ScrollContainer
var _status: Label
var _title: Label
var _notice: Label
var _commands: PanelContainer
var _construction: PanelContainer
var _command_grid: GridContainer
var _construction_grid: GridContainer
var _command_scroll: ScrollContainer
var _construction_scroll: ScrollContainer
var _command_entries: Array = []
var _construction_entries: Array = []
var _command_tiles: Array[Button] = []
var _construction_tiles: Array[Button] = []
var _command_signature := ''
var _construction_signature := ''
var _map: Control
var _tiles: Array[Button] = []
var _entries: Array = []
var _selected := 0
var _elapsed := 0.0
var _signature := ''
var _icons := {}
var _command_hover_button: Button
var _live: Node
var _store_owned := false
var _map_large := false
var _rotation := 0.0
var _pad_open := false
var _config := ConfigFile.new()
var layout_config=preload('res://scripts/adaptive_hud_layout.gd').new()
var _sizes:Dictionary={}
var _reload_elapsed:=0.0
const SECTIONS=['Commands','Construction','Powers','Heroes','Builders','Map','System']

func _ready():
 layer=38
 if not layout_config.initialize():return
 _sizes=layout_config.values
 var loaded=_config.load('user://adaptive-hud.cfg')
 if loaded!=OK and loaded!=ERR_FILE_NOT_FOUND:push_error('Cannot load HUD preference: '+error_string(loaded))
 mode=str(_config.get_value('hud','mode','modern' if OS.has_feature('ios') else 'classic'))
 if mode not in ['classic','modern','hidden']:mode='classic'
 _top=_panel_box();add_child(_top)
 var top=HBoxContainer.new();_top.add_child(top)
 _status=Label.new();_status.text='Battle controls';_status.custom_minimum_size.x=_sizes.status_width;top.add_child(_status)
 var toggle=_button('HUD','toggle');toggle.tooltip_text='F10: HUD mode · Shift+F10: reload sizes · '+ProjectSettings.globalize_path(layout_config.path);toggle.pressed.connect(cycle_mode);top.add_child(toggle)
 _dock=_panel_box();add_child(_dock)
 var dock=GridContainer.new();_tabs=dock;dock.columns=int(_sizes.tab_columns);dock.add_theme_constant_override('h_separation',int(_sizes.tab_gap));dock.add_theme_constant_override('v_separation',int(_sizes.tab_gap));_dock.add_child(dock)
 for name in SECTIONS:
  if name in ['Commands','Construction']:continue
  var button=_button(name,'tab');button.pressed.connect(open_section.bind(name));dock.add_child(button)
 _panel=_panel_box();add_child(_panel)
 _contents=VBoxContainer.new();_contents.add_theme_constant_override('separation',int(_sizes.content_gap));_panel.add_child(_contents)
 var heading=HBoxContainer.new();_contents.add_child(heading)
 _title=Label.new();_title.size_flags_horizontal=Control.SIZE_EXPAND_FILL;heading.add_child(_title)
 var close=_button('Close','close');close.pressed.connect(func():open_section(''));heading.add_child(close)
 _notice=Label.new();_notice.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;_contents.add_child(_notice)
 _scroll=ScrollContainer.new();_scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;_scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;_contents.add_child(_scroll)
 _grid=GridContainer.new();_grid.columns=int(_sizes.command_columns);_grid.add_theme_constant_override('h_separation',int(_sizes.grid_gap));_grid.add_theme_constant_override('v_separation',int(_sizes.grid_gap));_scroll.add_child(_grid)
 _map=preload('res://scripts/adaptive_minimap.gd').new();_map.hud=hud;_map.sizing=_sizes;add_child(_map)
 _commands=_panel_box();add_child(_commands)
 _command_grid=_persistent_grid(_commands,'Commands')
 _command_scroll=_command_grid.get_parent()
 _construction=_panel_box();add_child(_construction)
 _construction_grid=_persistent_grid(_construction,'Construction')
 _construction_scroll=_construction_grid.get_parent()
 if is_instance_valid(bridge):
  bridge.palette_toggled.connect(_palette)
  bridge.command_action.connect(_controller_action)
  bridge.controller_disconnected.connect(func():_pad_open=false)
 _apply_mode();_apply_layout_settings()

func _exit_tree():
 _clear_command_hover()
 if is_instance_valid(_live):_live.set_native_hud_visible(true)
 if _store_owned and is_instance_valid(_live):_live.close_spell_store()
 Engine.remove_meta('adaptive_hud_rects');Engine.remove_meta('adaptive_hud_active');Engine.remove_meta('adaptive_hud_replaces_native');Engine.remove_meta('touch_build_controls')

func cycle_mode():
 var modes=['classic','modern','hidden'];mode=modes[(modes.find(mode)+1)%3]
 _config.set_value('hud','mode',mode)
 var result=_config.save('user://adaptive-hud.cfg')
 if result!=OK:push_error('Cannot save HUD preference: '+error_string(result))
 open_section('');_apply_mode()

func _apply_mode():
 _clear_command_hover()
 Engine.set_meta('adaptive_hud_active',mode=='modern')
 Engine.set_meta('adaptive_hud_replaces_native',mode!='classic')
 if is_instance_valid(_live):_live.set_native_hud_visible(mode=='classic')
 _signature=''

func _unhandled_key_input(event):
 if event is InputEventKey and event.pressed and not event.echo and event.keycode==KEY_F10:
  if event.shift_pressed:reload_layout()
  else:cycle_mode()
  get_viewport().set_input_as_handled()

func open_section(value:String):
 var next_section='' if value==section else value
 if _store_owned and next_section!='Powers' and is_instance_valid(_live):_live.close_spell_store();_store_owned=false
 section=next_section;_signature='';_selected=0

func _palette(open:bool):
 if mode!='modern':return
 _pad_open=open
 if open:
  section='Commands';_selected=0;_signature=''
 else:
  open_section('')

func _process(delta):
 _reload_elapsed+=delta
 if _reload_elapsed>=float(_sizes.reload_interval_seconds):
  _reload_elapsed=0
  if layout_config.reload():_apply_layout_settings()
 var active:bool=gameplay.is_valid() and gameplay.call()
 var live:Node=hud.call() if hud.is_valid() else null
 if live!=_live:
  _clear_command_hover()
  if is_instance_valid(_live):_live.set_native_hud_visible(true)
  _live=live;_icons.clear();_apply_mode()
 _top.visible=active and is_instance_valid(_live)
 _status.visible=mode!='hidden'
 _dock.visible=_top.visible and mode=='modern'
 _panel.visible=_dock.visible and section not in ['','Commands','Construction']
 _commands.visible=_dock.visible
 _construction.visible=_dock.visible and not _construction_entries.is_empty()
 _map.visible=_dock.visible
 if not active:
  _clear_command_hover()
  if _store_owned and is_instance_valid(_live):_live.close_spell_store();_store_owned=false
  Engine.set_meta('adaptive_hud_rects',[]);return
 if not is_instance_valid(_live):return
 Engine.set_meta('touch_build_controls',mode=='modern')
 _elapsed+=delta
 if _elapsed>=.2:
  _elapsed=0
  if mode=='modern':_refresh()
 _layout()
 var rects=[]
 for control in [_top,_dock,_panel,_commands,_construction,_map]:
  if control.visible:rects.append(control.get_global_rect())
 Engine.set_meta('adaptive_hud_rects',rects)

func _safe_rect()->Rect2:
 var view=get_viewport().get_visible_rect()
 var safe=DisplayServer.get_display_safe_area()
 var window=DisplayServer.window_get_size()
 if OS.has_feature('ios') and safe.size.x>0 and window.x>0 and window.y>0:
  var factor=view.size/Vector2(window)
  return Rect2(Vector2(safe.position)*factor,Vector2(safe.size)*factor).intersection(view)
 return view

func _layout():
 var safe=_safe_rect().grow(-float(_sizes.viewport_margin))
 var gap=float(_sizes.gap)
 var padding=float(_sizes.panel_padding)*2
 var scale_factor=clampf(minf(safe.size.x,safe.size.y)/float(_sizes.scale_reference),float(_sizes.minimum_scale),float(_sizes.maximum_scale))
 var columns=int(_sizes.command_columns)
 var widest=maxi(columns,int(_sizes.construction_columns))
 var grid_width=widest*float(_sizes.tile_width)+(widest-1)*float(_sizes.grid_gap)+padding+gap*2
 # Five full tiles include room for padding and the vertical scrollbar.
 scale_factor=minf(scale_factor,safe.size.x/grid_width)
 var compact=safe.size.x<float(_sizes.compact_width)
 _tabs.columns=int(_sizes.compact_tab_columns if compact else _sizes.tab_columns)
 for p in [_top,_dock,_panel,_commands,_construction]:p.scale=Vector2.ONE*scale_factor
 _top.size=_top.get_combined_minimum_size();_top.position=safe.position
 _dock.size=_dock.get_combined_minimum_size()
 _dock.position=Vector2(safe.end.x-_dock.size.x*scale_factor,safe.position.y+(_top.size.y*scale_factor+gap if compact else 0))
 var top_end=maxf(_top.position.y+_top.size.y*scale_factor,_dock.position.y+_dock.size.y*scale_factor)+gap
 var height_budget=maxf(1,safe.end.y-top_end)
 var width=minf(grid_width*scale_factor,safe.size.x)
 var build_height=minf(float(_sizes.construction_height)*scale_factor,height_budget*float(_sizes.construction_height_fraction)) if _construction.visible else 0.0
 var map_reserve=maxf(float(_sizes.auxiliary_minimum_height)*scale_factor,height_budget*float(_sizes.compact_minimap_height_fraction)) if compact else 0.0
 var command_height=minf(float(_sizes.command_height)*scale_factor,height_budget-build_height-gap-map_reserve-gap)
 _commands.size=Vector2(width,command_height)/scale_factor
 _commands.position=safe.end-Vector2(width,command_height)
 _construction.size=Vector2(width,maxf(1,build_height))/scale_factor
 _construction.position=_commands.position-Vector2(0,build_height+gap)
 _command_grid.columns=columns;_construction_grid.columns=int(_sizes.construction_columns)
 var desired_map=float(_sizes.expanded_minimap_side if _map_large else _sizes.minimap_side)
 var map_width=safe.size.x-width-gap
 var map_height=maxf(1,height_budget-(float(_sizes.auxiliary_minimum_height)*scale_factor+gap if _panel.visible else 0))
 var map_bottom=safe.end.y
 if map_width<desired_map and compact:
  map_width=maxf(1,safe.size.x-float(_sizes.auxiliary_minimum_width)*scale_factor-gap)
  map_bottom=(_construction.position.y if _construction.visible else _commands.position.y)-gap
  map_height=maxf(1,map_bottom-top_end)
 var map_side=minf(desired_map,minf(maxf(1,map_width),map_height))
 _map.custom_minimum_size=Vector2.ZERO
 _map.size=Vector2.ONE*map_side;_map.position=Vector2(safe.position.x,map_bottom-map_side)
 # Auxiliary panels share the space above the minimap on wide screens;
 # on portrait screens they sit beside the map above the construction panel.
 var panel_top=top_end
 var panel_bottom=map_bottom if compact else _map.position.y-gap
 var panel_width=minf(float(_sizes.auxiliary_width)*scale_factor,maxf(1,safe.size.x-_map.size.x-gap)) if compact else minf(float(_sizes.auxiliary_width)*scale_factor,maxf(1,map_width))
 var available=Vector2(panel_width,minf(float(_sizes.auxiliary_maximum_height)*scale_factor,maxf(1,panel_bottom-panel_top)))
 _panel.size=available/scale_factor
 _panel.position=Vector2(safe.end.x-available.x if compact else safe.position.x,panel_top)
 _grid.columns=maxi(1,int((_panel.size.x-padding)/(float(_sizes.tile_width)+float(_sizes.grid_gap))))

func reload_layout()->bool:
 if not layout_config.reload(true):return false
 _apply_layout_settings()
 return true

func _apply_layout_settings():
 _sizes=layout_config.values
 _map.sizing=_sizes;_map.queue_redraw()
 _status.custom_minimum_size.x=float(_sizes.status_width)
 _restyle(self)
 _signature='';_command_signature='';_construction_signature=''
 _layout()

func _restyle(node:Node):
 if node is PanelContainer:_style_panel(node)
 if node is Label:node.add_theme_font_size_override('font_size',int(_sizes.font_size))
 if node is Button:_style_button(node,str(node.get_meta('layout_kind','command')))
 if node is GridContainer:
  var gap=int(_sizes.tab_gap if node==_tabs else _sizes.grid_gap)
  node.add_theme_constant_override('h_separation',gap);node.add_theme_constant_override('v_separation',gap)
 if node is ProgressBar:node.offset_top=-float(_sizes.progress_height)
 if node==_contents:node.add_theme_constant_override('separation',int(_sizes.content_gap))
 for child in node.get_children():_restyle(child)

func _persistent_grid(panel:PanelContainer,title:String)->GridContainer:
 var column=VBoxContainer.new();panel.add_child(column)
 var heading=Label.new();heading.text=title;column.add_child(heading)
 var scroll=ScrollContainer.new();scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;column.add_child(scroll)
 var grid=GridContainer.new();grid.columns=int(_sizes.construction_columns);grid.add_theme_constant_override('h_separation',int(_sizes.grid_gap));grid.add_theme_constant_override('v_separation',int(_sizes.grid_gap));scroll.add_child(grid)
 return grid

static func split_commands(buttons:Array)->Dictionary:
 var commands=[];var construction=[];var seen={}
 for entry in buttons:
  if int(entry.get('state',0))==0:continue
  # A command button may be exposed in both native bars. Its INI name is
  # its identity; distinct buttons using the same order must remain distinct.
  var key=str(entry.get('name',''))
  if key.is_empty():key=JSON.stringify([entry.get('command',''),entry.get('template',''),entry.get('upgrade',''),entry.get('power',''),entry.get('slot',-1)])
  if seen.has(key):continue
  seen[key]=true
  if entry.get('command','') in ['DOZER_CONSTRUCT','FOUNDATION_CONSTRUCT','CASTLE_UNPACK_EXPLICIT_OBJECT']:
   construction.append(entry)
  else:commands.append(entry)
 return {'commands':commands,'construction':construction}

func _refresh():
 var resource:Dictionary=_live.get_adaptive_resources()
 if not resource.is_empty():_status.text='Gold %s   Army %s/%s   Power %s'%[resource.money,resource.used,resource.limit,resource.power]
 var groups=split_commands(_live.get_command_buttons())
 _command_entries=groups.commands;_construction_entries=groups.construction
 var command_signature=JSON.stringify(_command_entries)
 if command_signature!=_command_signature:
  _command_signature=command_signature;_command_tiles=_render_tiles(_command_entries,_command_grid)
 var construction_signature=JSON.stringify(_construction_entries)
 if construction_signature!=_construction_signature:
  _construction_signature=construction_signature;_construction_tiles=_render_tiles(_construction_entries,_construction_grid)
 _map.refresh()
 var placing:Dictionary=_live.get_placement()
 if placing.get('placing',false) and section!='Placement':section='Placement';_signature=''
 elif not placing.get('placing',false) and section=='Placement':section='Commands';_signature=''
 _entries=[]
 _title.text=section
 _scroll.visible=true
 _notice.text='Hold RT (or Tab) · D-pad moves · A activates · B cancels' if _pad_open else ''
 match section:
  'Commands':_entries=_command_entries
  'Construction':_entries=_construction_entries
  'Heroes','Builders':
   for entry in _live.get_controller_objects(section.to_lower()):_entries.append({'title':entry.name,'id':entry.id})
  'Powers':
   var spells:Dictionary=_live.get_spellbook_state()
   if _store_owned and spells.get('store',{}).get('open',false):
    var store:Dictionary=spells.store
    _notice.text='Power points: %s'%store.get('points',0)
    _entries.append_array(store.get('purchase_entries',[]))
    _entries.append({'title':'Back to powers','back_store':true})
   else:
    _entries.append({'title':'Unlock powers','store':true})
    _entries.append_array(spells.get('power_entries',[]))
  'Placement':
   _notice.text='Drag on terrain to position. Lock, then rotate and confirm.'
   for title in ['Reposition','Lock','Rotate left','Rotate right','Build','Cancel']:
    _entries.append({'title':title,'placement':title,'state':1 if title!='Build' or (placing.get('anchored',false) and placing.get('legal',-1)==0) else 2})
  'Map':
   _map.refresh();_entries=[{'title':'Look / orders','map_orders':true,'detail':'Orders' if _map.order_mode else 'Look'},{'title':'Resize map','map_size':true}]
   _scroll.visible=true;_notice.text='Tap / drag to look. Orders sends selected units. Controller: D-pad aims, A looks, B orders.'
  'System':
   for title in ['Pause / options','Deselect','Select army','Console','Reload HUD layout','Classic HUD','Hide HUD']:_entries.append({'title':title,'system':title})
 var signature=JSON.stringify(_entries)
 if section in ['Commands','Construction']:
  _tiles=_command_tiles if section=='Commands' else _construction_tiles
  _selected=clampi(_selected,0,maxi(0,_entries.size()-1));_highlight()
 elif signature!=_signature:
  _signature=signature;_render()

func _render():
 _selected=clampi(_selected,0,maxi(0,_entries.size()-1))
 _tiles=_render_tiles(_entries,_grid)
 _highlight()

func _command_hover(button:Button,entry:Dictionary):
 _command_hover_button=button
 if is_instance_valid(_live):_live.set_controller_hover(int(entry.slot),bool(entry.in_palantir),0,-1)

func _clear_command_hover(button:Button=null):
 if button!=null and button!=_command_hover_button:return
 if _command_hover_button!=null and is_instance_valid(_live):_live.set_controller_hover(-1,true,0,-1)
 _command_hover_button=null

func _render_tiles(entries:Array,grid:GridContainer)->Array[Button]:
 for child in grid.get_children():grid.remove_child(child);child.queue_free()
 var tiles:Array[Button]=[]
 for i in entries.size():
  var entry:Dictionary=entries[i]
  var title=str(entry.get('title',entry.get('template',entry.get('name','Command'))))
  if title.is_empty():title=str(entry.get('command','Command'))
  if entry.get('command','')=='PUSH_VISIBLE_COMMAND_RANGE':title+=' ›'
  elif entry.get('command','')=='POP_VISIBLE_COMMAND_RANGE':title='‹ '+title
  var suffix=''
  if int(entry.get('cost',0))>0:suffix+='\n%d gold'%int(entry.cost)
  if int(entry.get('queued',0))>0:suffix+=' · ×%d'%int(entry.queued)
  if entry.has('detail'):suffix+='\n'+str(entry.detail)
  var tile_kind='command' if grid==_command_grid else ('map_control' if grid==_grid and section=='Map' else 'construction')
  var button=_button(title+suffix,tile_kind)
  if entry.has('slot'):
   button.mouse_entered.connect(_command_hover.bind(button,entry))
   button.mouse_exited.connect(_clear_command_hover.bind(button))
   button.tree_exiting.connect(_clear_command_hover.bind(button))
  button.tooltip_text=title+'\n'+str(entry.get('description',''))
  var icon_name=str(entry.get('image',''))
  if not icon_name.is_empty():
   if not _icons.has(icon_name):_icons[icon_name]=_live.get_command_icon(icon_name)
   button.icon=_icons[icon_name];button.expand_icon=true;button.icon_alignment=HORIZONTAL_ALIGNMENT_CENTER;button.vertical_icon_alignment=VERTICAL_ALIGNMENT_TOP;button.add_theme_constant_override('icon_max_width',int(_sizes.icon_width))
  button.disabled=int(entry.get('state',1)) not in [1,5]
  button.pressed.connect(_activate_entry.bind(entry,false));grid.add_child(button);tiles.append(button)
  button.gui_input.connect(func(event):
   if event is InputEventMouseButton and event.pressed and event.button_index==MOUSE_BUTTON_RIGHT:
    _activate_entry(entry,true);button.accept_event())
  if float(entry.get('timer',-1))>=0:
   var progress=ProgressBar.new();progress.mouse_filter=Control.MOUSE_FILTER_IGNORE;progress.show_percentage=false;progress.max_value=1;progress.value=float(entry.timer);progress.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE);progress.offset_top=-float(_sizes.progress_height);button.add_child(progress)
 return tiles

func _activate(index:int,cancel:bool):
 if index<0 or index>=_entries.size() or not is_instance_valid(_live):return
 _activate_entry(_entries[index],cancel)

func _activate_entry(entry:Dictionary,cancel:bool):
 if not is_instance_valid(_live):return
 if entry.has('slot'):_live.activate_command(entry.slot,entry.in_palantir,cancel)
 elif entry.has('id'):_live.select_controller_group(PackedInt64Array([entry.id]));open_section('Commands')
 elif entry.has('spell'):_live.press_spell_slot(entry.spell)
 elif entry.has('store'):_store_owned=_live.open_spell_store()
 elif entry.has('purchase'):_live.spell_store_click(entry.purchase)
 elif entry.has('back_store'):_live.close_spell_store();_store_owned=false
 elif entry.has('map_orders'):_map.order_mode=not _map.order_mode
 elif entry.has('map_size'):_map_large=not _map_large
 elif entry.has('placement'):
  var actions={'Reposition':'reposition','Lock':'lock','Build':'confirm','Cancel':'cancel'}
  if actions.has(entry.placement):_live.touch_build(actions[entry.placement],Vector2.ZERO)
  else:
   _rotation+=-PI/8 if entry.placement=='Rotate left' else PI/8
   _live.touch_build('rotate',Vector2.from_angle(_rotation))
 elif entry.has('system'):
  match entry.system:
   'Pause / options':open_menu.call()
   'Deselect':_live.deselect_all()
   'Stop':_live.inject_key(31,true);_live.inject_key(31,false)
   'Select army':
    var ids=PackedInt64Array()
    for unit in _live.get_controller_objects('units'):ids.append(unit.id)
    _live.select_controller_group(ids)
   'Console':if open_console.is_valid():open_console.call()
   'Reload HUD layout':reload_layout()
   'Classic HUD':mode='classic';_config.set_value('hud','mode',mode);_config.save('user://adaptive-hud.cfg');open_section('');_apply_mode()
   'Hide HUD':mode='hidden';_config.set_value('hud','mode',mode);_config.save('user://adaptive-hud.cfg');open_section('');_apply_mode()
 _signature='';_refresh()

func _controller_action(action:String):
 if mode!='modern' or not _pad_open:return
 if section=='Placement' and action=='rightPressCommand':
  _live.touch_build('cancel',Vector2.ZERO);_refresh();return
 if section=='Map' and action in ['previousCommand','nextCommand','shortcutUp','shortcutDown','pressCommand','rightPressCommand']:
  var offsets={'previousCommand':Vector2(-.05,0),'nextCommand':Vector2(.05,0),'shortcutUp':Vector2(0,-.05),'shortcutDown':Vector2(0,.05)}
  if offsets.has(action):_map.cursor=(_map.cursor+offsets[action]).clamp(Vector2.ZERO,Vector2.ONE);_map.queue_redraw()
  else:_map.activate(_map.cursor,action=='rightPressCommand')
  return
 match action:
  'previousCommand':_selected=maxi(0,_selected-1)
  'nextCommand':_selected=mini(_entries.size()-1,_selected+1)
  'shortcutUp','shortcutDown':
   var current=SECTIONS.find(section);section=SECTIONS[posmod(current+(-1 if action=='shortcutUp' else 1),SECTIONS.size())];_selected=0;_signature='';_refresh()
  'pressCommand','rightPressCommand':_activate(_selected,action=='rightPressCommand')
  'backPalette':open_section('')
 _highlight()

func _highlight():
 for i in _tiles.size():_tiles[i].modulate=Color('#ffe2a3') if _pad_open and i==_selected else Color.WHITE
 if _pad_open and _selected>=0 and _selected<_tiles.size():
  var scroll=_command_scroll if section=='Commands' else (_construction_scroll if section=='Construction' else _scroll)
  scroll.ensure_control_visible(_tiles[_selected])

func _panel_box()->PanelContainer:
 var panel=PanelContainer.new();_style_panel(panel);return panel

func _style_panel(panel:PanelContainer):
 var style=StyleBoxFlat.new();style.bg_color=Color('#101c2af5');style.border_color=Color('#52667d');style.set_border_width_all(int(_sizes.border_width));style.set_corner_radius_all(int(_sizes.panel_corner_radius))
 for edge in ['left','right','top','bottom']:style.set('content_margin_'+edge,float(_sizes.panel_padding))
 panel.add_theme_stylebox_override('panel',style)
 panel.add_theme_font_size_override('font_size',int(_sizes.font_size))

func _button(title:String,kind:String)->Button:
 var button=Button.new();button.text=title;button.set_meta('layout_kind',kind);button.focus_mode=Control.FOCUS_NONE;button.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART
 _style_button(button,kind);return button

func _style_button(button:Button,kind:String):
 var width=float(_sizes.get(kind+'_width',_sizes.tile_width))
 var height=float(_sizes.get(kind+'_height',_sizes.construction_tile_height))
 if kind in ['command','construction']:height=float(_sizes[kind+'_tile_height'])
 button.custom_minimum_size=Vector2(width,height)
 button.add_theme_font_size_override('font_size',int(_sizes.font_size))
 button.add_theme_constant_override('icon_max_width',int(_sizes.icon_width))
 for state in ['normal','hover','pressed','disabled']:
  var style=StyleBoxFlat.new();style.bg_color=Color('#283b50') if state!='pressed' else Color('#53694f');style.border_color=Color('#6a7f97') if state!='disabled' else Color('#33404e');style.set_border_width_all(int(_sizes.border_width));style.set_corner_radius_all(int(_sizes.button_corner_radius))
  for edge in ['left','right','top','bottom']:style.set('content_margin_'+edge,float(_sizes.button_padding))
  button.add_theme_stylebox_override(state,style)
