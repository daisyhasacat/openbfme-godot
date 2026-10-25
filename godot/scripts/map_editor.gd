extends CanvasLayer
## Local, paused-map editing. Saved JSON overlays never alter retail archives.
var world: Node
var hud: Node
var camera: Camera3D
var fs: RefCounted
signal active_changed(enabled: bool)
var active := false
var was_paused := false
var was_shroud := true
var was_free_camera := false
var exit_button: Button
var grid_rotation: CheckButton
var hovered_texture := ''
var terrain_cursor_cell := Vector2(INF,INF)
var hud_was_processing := true
var toolbar: PanelContainer
var lookup: PanelContainer
var lookup_title: Label
var mode_buttons: Array[Button] = []
var object_view: SubViewport
var object_image: TextureRect
var object_camera: Camera3D
var object_stage: Node3D
var object_renderer: Node3D
var object_view_name := ''
var panel: PanelContainer
var status: Label
var catalog := PackedStringArray()
var filtered := PackedStringArray()
var object_records: Dictionary = {}
var object_filters: HFlowContainer
var availability: OptionButton
var inspector: Window
var inspector_objects: ItemList
var inspector_details: RichTextLabel
var object_category: OptionButton
var filter_toggles: Dictionary = {}
var view_toggle: Button
var object_tree: Tree
var object_results: ScrollContainer
var object_grid: GridContainer
var paging: HBoxContainer
var page_label: Label
var object_page := 0
var grid_mode := true
var thumbnail_busy := false
var thumbnail_queue: Array = []
var thumbnail_cache: Dictionary = {}
var result_generation := 0
const PAGE_SIZE := 24
var objects: ItemList
var search: LineEdit
var tool: OptionButton
var shape: OptionButton
var texture_search: LineEdit
var texture_category: OptionButton
var texture_subcategory: OptionButton
var texture_catalog: Array = []
var texture_filtered: Array = []
var texture: OptionButton
var texture_preview: TextureRect
var texture_caption: Label
var terrain_loader: RefCounted
var radius: SpinBox
var strength: SpinBox
var angle_control: SpinBox
var owner_slot: VBoxContainer
var save_name: LineEdit
var preview: Node3D
var preview_instance := -1
var preview_template := ''
var snap_held := false
var placement_grid := 50.0
var placement_anchor := Vector2.ZERO
var dragging := false
var start := Vector2.ZERO
var cursor := Vector2.ZERO
var points: Array[Vector2] = []
var stroke_undo: Array = []
var stroke_seen := {}
var stroke_count := 0
var stroke_height := 0.0
var stroke_clock := 0.0
var touches := {}
var undo_stack: Array[Dictionary] = []
var overlay: Array[Dictionary] = []
var terrain_info: Dictionary = {}
var selected_objects: Array[int] = []
var apply_owner: Button
var road_controls: VBoxContainer
var road_type: OptionButton
var road_width: SpinBox
var road_catalog: Array = []      ## the engine's road types (name, texture, width, width_in_texture), in road_type's item order
var box_diagonal: CheckButton    ## Border box: drag a box rotated 45° (its corners square in the rotated frame)
var border_none := false          ## Border box: paint the box without border roads
var path_library: VBoxContainer
var path_library_title: Label
var path_search: LineEdit
var path_grid: GridContainer
var path_thumbnails: Dictionary = {} ## texture file -> ImageTexture of its straight-road band, or the load error (String)
var road_action: OptionButton
var road_list: ItemList
var roads: Array = []
var road_selected := -1
var road_endpoint := -1
var road_anchor := Vector2(INF,INF)
var road_before: Array = []
var road_drag_origin := Vector2.ZERO
var road_drag := false
var outline: Control
var scroll: ScrollContainer
var content: VBoxContainer

class Outline extends Control:
 var editor: CanvasLayer
 func _draw():
  if not editor.active: return
  if editor.snap_held and editor.tool.selected in [1,2]:
   var center=editor._snap_point(editor.hud.pixel_to_world(editor.cursor))
   for i in range(-5,6):
    for axis in 2:
     var a=center+Vector2(i*editor.placement_grid,-5*editor.placement_grid) if axis==0 else center+Vector2(-5*editor.placement_grid,i*editor.placement_grid)
     var b=a+Vector2(0,10*editor.placement_grid) if axis==0 else a+Vector2(10*editor.placement_grid,0)
     editor.outline.draw_line(editor.hud.world_to_pixel(a),editor.hud.world_to_pixel(b),Color(.3,1,.6,.45),1)
  if editor.tool.selected==8:
   for i in editor.roads.size():
    var road=editor.roads[i];var a=editor.hud.world_to_pixel(editor._road_point(road.points[0]));var b=editor.hud.world_to_pixel(editor._road_point(road.points[1]))
    var color=Color(1,.8,.2) if i==editor.road_selected else Color(.3,.85,1)
    draw_line(a,b,color,3);draw_circle(a,7,color);draw_circle(b,7,color)
   if editor.road_action.selected==1 and is_finite(editor.road_anchor.x):
    draw_line(editor.hud.world_to_pixel(editor.road_anchor),editor.cursor,Color(1,.8,.2),3)
   return
  if editor.tool.selected==9:
   for id in editor.selected_objects:
    var obj:Dictionary=editor.world.get_object(id)
    if obj.get('ok',false):draw_circle(editor.hud.world_to_pixel(Vector2(obj.x,obj.y)),12,Color(1,.8,.2),false,3)
  if editor.dragging and editor.tool.selected==10 and not editor.points.is_empty():
   var corners=editor._box_corners(editor.points[0],editor.hud.pixel_to_world(editor.cursor),editor.box_diagonal.button_pressed)
   var border=PackedVector2Array()
   for point in corners:border.append(editor.hud.world_to_pixel(point))
   border.append(border[0]);draw_colored_polygon(border,Color(.3,1,.6,.15));draw_polyline(border,Color(1,.8,.2),3)
  elif editor.dragging and editor.tool.selected in [0,9]:
   draw_rect(Rect2(editor.start,editor.cursor-editor.start).abs(),Color(1,.3,.2,.15),true)
   draw_rect(Rect2(editor.start,editor.cursor-editor.start).abs(),Color(1,.4,.3),false,2)
  elif editor.tool.selected>=2 and editor.tool.selected<7:
   var center=editor.hud.pixel_to_world(editor.cursor)
   if center.x < -1e8: return
   var ring=PackedVector2Array()
   for i in 65:
    var a=float(i)*TAU/64
    var delta=Vector2(cos(a),sin(a))*editor.radius.value
    if editor.shape.selected==0: delta=delta/max(abs(cos(a)),abs(sin(a)))
    if editor.grid_rotation.button_pressed:delta=delta.rotated(PI/4)
    ring.append(editor.hud.world_to_pixel(center+delta))
   draw_polyline(ring,Color(.3,1,.6),2)

func _ready():
 layer=40
 panel=PanelContainer.new();panel.position=Vector2(20,80);panel.custom_minimum_size=Vector2(520,0);add_child(panel)
 var background=StyleBoxFlat.new();background.bg_color=Color('#10191e');background.border_color=Color('#c4a76a');background.set_border_width_all(1);background.set_corner_radius_all(8);background.content_margin_left=12;background.content_margin_right=12;background.content_margin_top=12;background.content_margin_bottom=12;panel.add_theme_stylebox_override('panel',background)
 var ui_theme=Theme.new();ui_theme.default_font_size=24;panel.theme=ui_theme
 scroll=ScrollContainer.new();scroll.custom_minimum_size=Vector2(490,min(750.0,max(280.0,get_viewport().get_visible_rect().size.y-130)));panel.add_child(scroll)
 var box=VBoxContainer.new();content=box;box.size_flags_horizontal=Control.SIZE_EXPAND_FILL;scroll.add_child(box)
 var heading=Label.new();heading.text='MAP EDITOR · simulation paused';box.add_child(heading)
 var close=Button.new();close.text='Exit editor';close.pressed.connect(func():set_active(false));box.add_child(close)
 tool=OptionButton.new()
 for text in ['Delete objects','Place object','Object brush','Raise terrain','Lower terrain','Flatten terrain','Paint ground texture','Pick terrain texture','Edit roads / paths','Select objects / owner','Texture box with road border']: tool.add_item(text)
 tool.item_selected.connect(func(_i):hovered_texture='';_clear_preview();_update_controls());box.add_child(tool)
 shape=OptionButton.new();shape.add_item('Block / square');shape.add_item('Brush / circle');box.add_child(shape)
 grid_rotation=CheckButton.new();grid_rotation.text='Rotate brush grid 45°';box.add_child(grid_rotation)
 radius=_number(box,'Brush radius / block half-width',10,500,40,10)
 strength=_number(box,'Height step / object spacing',1,200,20,1)
 angle_control=_number(box,'Object rotation (degrees)',-360,360,0,15)
 owner_slot=preload('res://scripts/player_table.gd').new();owner_slot.setup(box,'Owner player',world.get_player_table())
 apply_owner=Button.new();apply_owner.text='Set selected objects owner';apply_owner.pressed.connect(_apply_owner);box.add_child(apply_owner)
 search=LineEdit.new();search.placeholder_text='Search objects, rocks, cliffs, trees…';search.text_changed.connect(_filter);box.add_child(search)
 objects=ItemList.new();objects.custom_minimum_size=Vector2(480,250);objects.item_selected.connect(func(_i):_clear_preview());box.add_child(objects)
 texture_search=LineEdit.new();texture_search.placeholder_text='Search terrain name or texture file…';box.add_child(texture_search);texture_search.text_changed.connect(func(_q):_filter_textures())
 texture_category=OptionButton.new();box.add_child(texture_category);texture_category.item_selected.connect(func(_i):_terrain_subcategories())
 texture_subcategory=OptionButton.new();box.add_child(texture_subcategory);texture_subcategory.item_selected.connect(func(_i):_filter_textures())
 texture=OptionButton.new();box.add_child(texture);texture.item_selected.connect(_texture_selected)
 terrain_loader=ClassDB.instantiate('MapTerrainBuilder')
 texture_preview=TextureRect.new();texture_preview.custom_minimum_size=Vector2(0,220);texture_preview.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;texture_preview.stretch_mode=TextureRect.STRETCH_KEEP_ASPECT_CENTERED;box.add_child(texture_preview)
 texture_caption=Label.new();texture_caption.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;box.add_child(texture_caption)
 road_controls=VBoxContainer.new();box.add_child(road_controls)
 road_type=OptionButton.new();road_controls.add_child(road_type)
 road_width=_number(road_controls,'Path width (0 = road type default)',0,400,0,1)
 box_diagonal=CheckButton.new();box_diagonal.text='Diagonal (45°)';box_diagonal.tooltip_text='Drag a box rotated 45°: the drag sets two opposite corners';road_controls.add_child(box_diagonal)
 road_action=OptionButton.new()
 for label in ['Select / move points','Add connected points','Insert point on segment']:road_action.add_item(label)
 road_controls.add_child(road_action)
 road_list=ItemList.new();road_list.custom_minimum_size=Vector2(480,180);road_controls.add_child(road_list)
 road_list.item_selected.connect(func(i):road_selected=i;road_endpoint=1;road_anchor=_road_point(roads[i].points[1]);_select_road_type())
 for pair in [['New path',_new_road],['Apply road type and width',_change_road_type],['Delete selected segment',_delete_road]]:
  var button=Button.new();button.text=pair[0];button.custom_minimum_size.y=52;button.pressed.connect(pair[1]);road_controls.add_child(button)
 var controls=HBoxContainer.new();box.add_child(controls);box.move_child(controls,3)
 var delete=Button.new();delete.text='Delete objects';delete.pressed.connect(func():tool.select(0);_clear_preview();_update_controls());controls.add_child(delete)
 var undo=Button.new();undo.text='Undo';undo.pressed.connect(_undo);controls.add_child(undo)
 save_name=LineEdit.new();save_name.placeholder_text='Edit file name';save_name.text='map-edits';box.add_child(save_name)
 for pair in [['Save edits',_save],['Load edits',_load]]:
  var button=Button.new();button.text=pair[0];button.pressed.connect(pair[1]);box.add_child(button)
 var help=Label.new();help.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;help.custom_minimum_size.x=480;help.text='Click or drag to edit. R rotates placement.\nRight-drag pans; wheel zooms. Esc exits.\nTerrain brushes update while dragging. Pick terrain previews under the cursor; click to paint with it.';box.add_child(help)
 status=Label.new();status.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;status.custom_minimum_size.x=480;box.add_child(status)
 outline=Outline.new();outline.editor=self;outline.mouse_filter=Control.MOUSE_FILTER_IGNORE;outline.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);add_child(outline)
 for dropdown in [tool,shape,texture,texture_category,texture_subcategory,road_type,road_action]:
  dropdown.fit_to_longest_item=false;dropdown.clip_text=true
 for button in panel.find_children('*','Button',true,false):button.custom_minimum_size.y=52
 _build_workspace(ui_theme,background)
 _build_inspector()
 _update_controls();panel.hide();lookup.hide();toolbar.hide();set_process(false)

func _build_workspace(ui_theme: Theme,background: StyleBoxFlat):
 toolbar=PanelContainer.new();toolbar.theme=ui_theme;toolbar.add_theme_stylebox_override('panel',background);add_child(toolbar)
 var rows=VBoxContainer.new();toolbar.add_child(rows)
 var header=HBoxContainer.new();rows.add_child(header)
 var title=Label.new();title.text='MAP EDITOR · simulation paused';title.size_flags_horizontal=Control.SIZE_EXPAND_FILL;header.add_child(title)
 exit_button=Button.new();exit_button.text='Exit editor · Esc';exit_button.custom_minimum_size=Vector2(230,60);exit_button.tooltip_text='Return to the game';exit_button.pressed.connect(func():set_active(false));header.add_child(exit_button)
 for state in ['normal','hover','pressed','focus']:
  var style=StyleBoxFlat.new();style.bg_color=Color('#98432d') if state=='normal' else Color('#be603c');style.border_color=Color('#ffda8a');style.set_border_width_all(2);style.set_corner_radius_all(6);style.set_content_margin_all(12);exit_button.add_theme_stylebox_override(state,style)
 exit_button.add_theme_color_override('font_color',Color.WHITE)
 exit_button.add_theme_color_override('font_hover_color',Color.WHITE)
 exit_button.add_theme_color_override('font_pressed_color',Color.WHITE)
 header.move_child(exit_button,0);header.add_theme_constant_override('separation',16)
 var modes=HFlowContainer.new();rows.add_child(modes)
 var names=['Delete','Object','Object brush','Raise','Lower','Flatten','Paint','Pick texture','Roads','Select','Border box']
 var icons=['⌫','⬡','▦','▲','▼','▬','▧','⌖','⌁','↖','▣']
 for i in names.size():
  var button=Button.new();button.text=icons[i]+'\n'+names[i];button.tooltip_text=tool.get_item_text(i);button.toggle_mode=true;button.custom_minimum_size=Vector2(96,78)
  var block=StyleBoxFlat.new();block.bg_color=Color('#24343f');block.set_corner_radius_all(6);block.set_content_margin_all(8);button.add_theme_stylebox_override('normal',block)
  button.pressed.connect(func():tool.select(i);hovered_texture='';_clear_preview();_update_controls());modes.add_child(button);mode_buttons.append(button)
 tool.hide()
 var actions=HBoxContainer.new();rows.add_child(actions)
 for pair in [['Undo',_undo],['Save',_save],['Load',_load],['Zoom +',func():_zoom_camera(1)],['Zoom −',func():_zoom_camera(-1)]]:
  var button=Button.new();button.text=pair[0];button.custom_minimum_size.y=48;button.pressed.connect(pair[1]);actions.add_child(button)
 save_name.reparent(actions);save_name.custom_minimum_size.x=180;save_name.size_flags_horizontal=Control.SIZE_EXPAND_FILL
 grid_rotation.reparent(actions);grid_rotation.text='45° brush'
 var snap=CheckButton.new();snap.text='Snap grid';snap.toggled.connect(func(on):snap_held=on);snap.name='SnapToggle';actions.add_child(snap)
 lookup=PanelContainer.new();lookup.theme=ui_theme;lookup.add_theme_stylebox_override('panel',background);add_child(lookup)
 var browser=VBoxContainer.new();lookup.add_child(browser)
 lookup_title=Label.new();browser.add_child(lookup_title)
 for control in [search,objects,texture_search,texture_category,texture_subcategory,texture]:control.reparent(browser)
 objects.hide() # selection backing for placement and existing editor operations
 _build_object_browser(browser)
 texture_preview.reparent(browser);texture_preview.custom_minimum_size=Vector2(0,240)
 object_image=TextureRect.new();object_image.custom_minimum_size=Vector2(0,240);object_image.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;object_image.stretch_mode=TextureRect.STRETCH_KEEP_ASPECT_CENTERED;browser.add_child(object_image)
 object_view=SubViewport.new();object_view.size=Vector2i(512,384);object_view.own_world_3d=true;object_view.transparent_bg=false;object_view.render_target_update_mode=SubViewport.UPDATE_DISABLED;object_image.add_child(object_view);object_image.texture=object_view.get_texture()
 object_stage=Node3D.new();object_view.add_child(object_stage)
 object_camera=Camera3D.new();object_stage.add_child(object_camera);object_camera.current=true;object_camera.projection=Camera3D.PROJECTION_ORTHOGONAL;object_camera.near=.1;object_camera.far=100000
 var light=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.5;object_stage.add_child(light)
 var environment=WorldEnvironment.new();var env=Environment.new();env.background_mode=Environment.BG_COLOR;env.background_color=Color('#15232e');env.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;env.ambient_light_color=Color.WHITE;env.ambient_light_energy=.7;environment.environment=env;object_stage.add_child(environment)
 texture_caption.reparent(browser)
 _build_path_browser(browser)
 # Replace the original stacked heading/actions with the toolbar.
 for child in content.get_children():
  if child is Label and child!=status and not child.has_meta('keep'):child.hide()
  if child is Button or child is HBoxContainer:child.hide()
 scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED
 panel.custom_minimum_size=Vector2(360,0);scroll.custom_minimum_size.x=336;status.custom_minimum_size.x=320;road_list.custom_minimum_size.x=320
 for field in [radius,strength,angle_control,owner_slot]:
  var caption:Label=field.get_meta('label');caption.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;caption.custom_minimum_size.x=300;caption.show()

func _build_path_browser(browser: VBoxContainer):
 path_library=VBoxContainer.new();browser.add_child(path_library)
 path_library_title=Label.new();path_library_title.text='PATH LIBRARY';path_library.add_child(path_library_title)
 path_search=LineEdit.new();path_search.placeholder_text='Search path name or texture file…';path_search.text_changed.connect(func(_q):_render_paths());path_library.add_child(path_search)
 var results=ScrollContainer.new();results.size_flags_vertical=Control.SIZE_EXPAND_FILL;results.custom_minimum_size.y=300;results.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;path_library.add_child(results)
 path_grid=GridContainer.new();path_grid.columns=3;path_grid.size_flags_horizontal=Control.SIZE_EXPAND_FILL;path_grid.add_theme_constant_override('h_separation',8);path_grid.add_theme_constant_override('v_separation',8);results.add_child(path_grid)

## the straight-road band of a road atlas (TerrainRoads.cpp: centred on v=85/512, RoadWidthInTexture/4 of the height)
func _path_thumbnail(item: Dictionary):
 var file=str(item.texture)
 if path_thumbnails.has(file):return path_thumbnails[file]
 var result:Dictionary=terrain_loader.preview_road_texture(fs,file)
 if not result.get('ok',false):path_thumbnails[file]=str(result.get('error','Road texture unavailable'))+'\n'+'\n'.join(result.get('errors',[]));return path_thumbnails[file]
 var image:Image=result.texture.get_image()
 if image.is_compressed():image.decompress()
 var height=float(image.get_height());var band=maxi(1,int(float(item.width_in_texture)/4.0*height))
 var top=clampi(int(85.0/512.0*height)-band/2,0,image.get_height()-band)
 path_thumbnails[file]=ImageTexture.create_from_image(image.get_region(Rect2i(0,top,image.get_width(),band)))
 return path_thumbnails[file]

func _path_card(title: String,detail: String,thumbnail,selected: bool,choose: Callable):
 var card=Button.new();card.toggle_mode=true;card.button_pressed=selected;card.size_flags_horizontal=Control.SIZE_EXPAND_FILL;card.custom_minimum_size=Vector2(190,150);card.tooltip_text=title+'\n'+detail;path_grid.add_child(card);card.pressed.connect(choose)
 var box=VBoxContainer.new();box.mouse_filter=Control.MOUSE_FILTER_IGNORE;box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);box.offset_left=8;box.offset_right=-8;box.offset_top=8;box.offset_bottom=-8;card.add_child(box)
 var image=TextureRect.new();image.mouse_filter=Control.MOUSE_FILTER_IGNORE;image.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;image.stretch_mode=TextureRect.STRETCH_SCALE;image.custom_minimum_size.y=56;image.size_flags_vertical=Control.SIZE_EXPAND_FILL;box.add_child(image)
 if thumbnail is Texture2D:image.texture=thumbnail
 elif thumbnail is String:
  var missing=Label.new();missing.text='Texture failed to load';missing.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;missing.vertical_alignment=VERTICAL_ALIGNMENT_CENTER;missing.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);missing.add_theme_font_size_override('font_size',14);missing.mouse_filter=Control.MOUSE_FILTER_IGNORE;image.add_child(missing);card.tooltip_text+='\n'+thumbnail
 var label=Label.new();label.mouse_filter=Control.MOUSE_FILTER_IGNORE;label.text=title+'\n'+detail;label.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;label.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;label.add_theme_font_size_override('font_size',15);box.add_child(label)

func _render_paths():
 if path_grid==null:return
 # a card's own pressed signal rebuilds the grid, so cards leave now and are freed after it
 for child in path_grid.get_children():path_grid.remove_child(child);child.queue_free()
 if not (active and tool.selected in [8,10]):return
 var boxing=tool.selected==10
 var query=path_search.text.strip_edges().to_lower()
 if boxing and (query.is_empty() or query in 'none no border'):
  _path_card('None','No border roads',null,border_none,func():border_none=true;_render_paths())
 for i in mini(road_catalog.size(),road_type.item_count):
  var item:Dictionary=road_catalog[i]
  if not query.is_empty() and query not in (str(item.name)+' '+str(item.texture)).to_lower():continue
  var chosen=road_type.selected==i and not (boxing and border_none)
  _path_card(str(item.name),'%s · width %d'%[item.texture,roundi(float(item.width))],_path_thumbnail(item),chosen,func():border_none=false;road_type.select(i);_render_paths())

func _over_editor(pixel: Vector2) -> bool:
 for window in [panel,toolbar,lookup]:
  if is_instance_valid(window) and window.visible and window.get_global_rect().has_point(pixel):return true
 return false

func _update_object_view():
 var name=_selected()
 if object_view_name==name:return
 object_view_name=name
 if is_instance_valid(object_renderer):object_renderer.free()
 object_renderer=null
 if name.is_empty():texture_caption.text='Select an object to preview';return
 object_renderer=ClassDB.instantiate('W3DInstancer');object_stage.add_child(object_renderer);object_renderer.setup(fs)
 var model=world.get_template_model(name);var id=object_renderer.add_model(model) if not model.is_empty() else -1
 texture_caption.text=name
 if id<0:texture_caption.text+=' · No preview model';return
 object_renderer.add_instance(id,Transform3D.IDENTITY,'',0,0)
 var extent=maxf(10,float(object_renderer.get_model_radius(id)))
 object_camera.size=extent*2.3
 object_camera.position=Vector3(1,.75,1).normalized()*extent*3.2;object_camera.look_at(Vector3.ZERO)
 object_renderer.update_now()
 object_view.render_target_update_mode=SubViewport.UPDATE_ALWAYS
 _fit_object_image.call_deferred(name)

func _fit_object_image(name: String):
 await RenderingServer.frame_post_draw
 if not is_instance_valid(object_view) or object_view_name!=name:return
 var picture=object_view.get_texture().get_image()
 if picture==null or picture.is_empty():return
 var bg=picture.get_pixel(0,0);var left=picture.get_width();var right=0;var top=picture.get_height();var bottom=0
 for y in range(0,picture.get_height(),4):
  for x in range(0,picture.get_width(),4):
   var pixel=picture.get_pixel(x,y)
   if absf(pixel.r-bg.r)+absf(pixel.g-bg.g)+absf(pixel.b-bg.b)>.08:left=mini(left,x);right=maxi(right,x);top=mini(top,y);bottom=maxi(bottom,y)
 if right<=left or bottom<=top:return
 var height=float(picture.get_height());var width=float(picture.get_width())
 var offset=object_camera.basis.x*((left+right-width)*.5/height*object_camera.size)+object_camera.basis.y*((height-top-bottom)*.5/height*object_camera.size)
 object_camera.position+=offset
 object_camera.size*=maxf(float(right-left)/width,float(bottom-top)/height)*1.2

func _number(parent: Node,title: String,minimum: float,maximum: float,value: float,step: float) -> SpinBox:
 var label=Label.new();label.text=title;parent.add_child(label)
 var input=SpinBox.new();input.min_value=minimum;input.max_value=maximum;input.value=value;input.step=step;input.set_meta('label',label);input.custom_minimum_size.y=52;parent.add_child(input)
 if 'Player_N_Start' in title:preload('res://scripts/player_table.gd').attach_start_picker(world,input)
 return input

func _load_texture_catalog():
 var entries={};var entry={}
 for raw in fs.read_file('data/ini/terrain.ini').get_string_from_utf8().split('\n'):
  var line=raw.split(';')[0].strip_edges()
  if line.to_lower().begins_with('terrain '):entry={'name':line.substr(8).strip_edges(),'file':'','types':[],'regions':[]}
  elif line.to_lower()=='end':
   if not entry.is_empty() and not entry.file.is_empty():entries[entry.name.to_lower()]=entry
   entry={}
  elif not entry.is_empty() and '=' in line:
   var key=line.get_slice('=',0).strip_edges().to_lower();var value=line.substr(line.find('=')+1).strip_edges()
   if key=='texture':entry.file=value
   elif key=='class':
    for tag in value.split('NEXT'):
     var words=tag.strip_edges().split(' ',false)
     if words.size()>1:
      if words[0].to_lower()=='type':entry.types.append(' '.join(words.slice(1)))
      elif words[0].to_lower()=='region':entry.regions.append(' '.join(words.slice(1)))
 texture_catalog=entries.values();texture_catalog.sort_custom(func(a,b):return a.name.naturalnocasecmp_to(b.name)<0)
 texture_category.clear();texture_category.add_item('All types')
 var categories=[]
 for item in texture_catalog:
  for category in item.types:
   if category not in categories:categories.append(category)
 categories.sort()
 for category in categories:texture_category.add_item(category)
 _terrain_subcategories()

func _terrain_subcategories():
 texture_subcategory.clear();texture_subcategory.add_item('All regions')
 var regions=[];var category=texture_category.get_item_text(texture_category.selected)
 for item in texture_catalog:
  if texture_category.selected>0 and category not in item.types:continue
  for region in item.regions:
   if region not in regions:regions.append(region)
 regions.sort()
 for region in regions:texture_subcategory.add_item(region)
 _filter_textures()

func _filter_textures():
 var previous=texture_filtered[texture.selected].name if texture.selected>=0 and texture.selected<texture_filtered.size() else ''
 texture.clear();texture_filtered=[]
 var category=texture_category.get_item_text(texture_category.selected)
 var region=texture_subcategory.get_item_text(texture_subcategory.selected)
 var query=texture_search.text.strip_edges().to_lower()
 for item in texture_catalog:
  if texture_category.selected>0 and category not in item.types:continue
  if texture_subcategory.selected>0 and region not in item.regions:continue
  if not query.is_empty() and query not in (item.name+' '+item.file+' '+' '.join(item.types)+' '+' '.join(item.regions)).to_lower():continue
  texture_filtered.append(item);texture.add_item(item.name)
  if item.name==previous:texture.select(texture.item_count-1)
 _texture_selected(texture.selected)

func _texture_selected(index: int):
 texture_preview.texture=null;texture_caption.text=''
 if index<0 or index>=texture.item_count:return
 var result:Dictionary=terrain_loader.preview_texture(fs,texture_filtered[index].name)
 if result.get('ok',false):
  texture_preview.texture=result.texture;texture_caption.text=str(result.get('file',''))
 else:texture_caption.text=str(result.get('error','Texture preview unavailable'))

func _field_visible(field: Control,enabled: bool):
 field.visible=enabled;field.get_meta('label').visible=enabled

func _update_controls():
 if inspector!=null:
  if active and tool.selected==9:
   if not inspector.visible:inspector.popup_centered()
  else:inspector.hide()

 var placing=tool.selected in [1,2]
 tool.hide()
 for i in mode_buttons.size():mode_buttons[i].set_pressed_no_signal(i==tool.selected)
 lookup.visible=active and tool.selected in [1,2,6,7,8,10]
 lookup_title.text='OBJECT LIBRARY' if placing else ('PATH LIBRARY' if tool.selected==8 else 'TERRAIN LIBRARY')
 path_library.visible=tool.selected in [8,10];path_library_title.visible=tool.selected==10
 _render_paths()
 if placing:texture_caption.text=_selected()
 if tool.selected in [6,10]:_texture_selected(texture.selected)
 object_image.visible=placing and not grid_mode and not _selected().is_empty()
 object_view.render_target_update_mode=SubViewport.UPDATE_ALWAYS if placing and active and not grid_mode else SubViewport.UPDATE_DISABLED
 texture_caption.visible=(placing and not grid_mode) or tool.selected in [6,7,10]
 road_controls.visible=tool.selected in [8,10]
 # road_type backs the path library's selection (like objects for the object library)
 for control in road_controls.get_children():control.visible=control!=road_type and (control in [road_width,road_width.get_meta('label')] or (control==box_diagonal)==(tool.selected==10) and tool.selected in [8,10])
 _field_visible(angle_control,placing);_field_visible(owner_slot,placing or tool.selected==9);apply_owner.visible=tool.selected==9
 _field_visible(radius,tool.selected>=2 and tool.selected<7);shape.visible=tool.selected>=2 and tool.selected<7;grid_rotation.visible=tool.selected>=2 and tool.selected<7
 _field_visible(strength,tool.selected in [2,3,4])
 search.visible=placing;objects.hide();object_filters.visible=placing;view_toggle.visible=placing;object_tree.visible=placing and not grid_mode;object_results.visible=placing and grid_mode;paging.visible=placing;texture.visible=tool.selected in [6,10];texture_search.visible=tool.selected in [6,10];texture_category.visible=tool.selected in [6,10];texture_subcategory.visible=tool.selected in [6,10];texture_preview.visible=tool.selected in [6,7,10];texture_caption.visible=(placing and not grid_mode) or tool.selected in [6,7,10]
 panel.reset_size()

func set_active(enabled: bool):
 if active==enabled:return
 if not enabled and inspector!=null:inspector.hide()
 if enabled:owner_slot.set_rows(world.get_player_table())
 active=enabled;active_changed.emit(enabled);panel.visible=enabled;toolbar.visible=enabled;lookup.visible=enabled and tool.selected in [1,2,6,7,8,10];set_process(enabled)
 if enabled:
  was_free_camera=hud.get_camera().get('free_camera',false);hud.set_free_camera(true)
  was_paused=world.is_paused();hud_was_processing=hud.is_processing_input();world.set_paused(true);was_shroud=world.get_shroud_report().get('displayed',true);world.set_shroud_drawn(false);hud.deselect_all();hud.set_process_input(false)
  terrain_info=world.map_editor_terrain({});_load_texture_catalog()
  roads=terrain_info.get('roads',[]).duplicate(true);road_type.clear()
  road_catalog=terrain_info.get('road_types',[]);road_catalog.sort_custom(func(a,b):return str(a.name).naturalnocasecmp_to(str(b.name))<0)
  for item in road_catalog:road_type.add_item(item.name)
  _refresh_roads()
  if catalog.is_empty():_load_object_catalog()
  status.text='Editing live map. Save edits as a reusable JSON overlay.'
 else:
  if road_drag:roads=road_before.duplicate(true);road_drag=false
  if dragging and tool.selected in [3,4,5,6]:_paint_live();_finish_terrain_stroke()
  hud.set_free_camera(was_free_camera)
  dragging=false;touches.clear();_clear_preview();world.set_shroud_drawn(was_shroud);hud.set_process_input(hud_was_processing);world.set_paused(was_paused);outline.queue_redraw()

func _load_object_catalog():
 var first=1
 while true:
  # Lua console output is capped at 64 KiB. Page the catalog and require a
  # terminator, so a truncated response can never become a partial catalog.
  var code="local names={}; for name in pairs(bfme.templates) do names[#names+1]=name end; table.sort(names); local rows={}; for i=%d,math.min(%d,#names) do local name=names[i]; local t=bfme.templates[name]; local c=bfme.buildMode.classify(t); local flags={}; for _,k in ipairs({'building','unit','scenery','trainable','builderBuildable','plotBuildable','enhancedPlotBuildable','plot','placeholder','unfinishedContent','unsupported','marker'}) do if c[k] then flags[#flags+1]=k end end; rows[#rows+1]=table.concat({name,tostring(t.cost or 0),table.concat(flags,','),table.concat(c.reasons or {},'; '),tostring(t.faction or ''),tostring(t.displayName or ''),table.concat(c.producers or {},','),table.concat(c.builders or {},','),table.concat(c.plots or {},',')},string.char(9)) end; rows[#rows+1]='__CATALOG_END__'; return table.concat(rows,string.char(10))"%[first,first+49]
  var result:Dictionary=world.execute_lua(code)
  var output=str(result.output).strip_edges()
  if not result.ok or not output.ends_with('__CATALOG_END__'):
   status.text='Object catalog failed: '+output;catalog.clear();object_records.clear();return
  var count=0
  for line in output.split('\n',false):
   if line=='__CATALOG_END__':continue
   var fields=line.split('\t')
   if fields.size()<3:continue
   var record={'name':fields[0],'cost':fields[1],'flags':fields[2].split(','),'notes':fields[3] if fields.size()>3 else ''}
   record.faction=fields[4] if fields.size()>4 else '';record.display_name=fields[5] if fields.size()>5 else ''
   record.producers=', '.join([fields[6],fields[7],fields[8]].filter(func(value):return not value.is_empty())) if fields.size()>8 else ''
   object_records[fields[0]]=record;catalog.append(fields[0]);count+=1
  if count<50:break
  first+=50
 _filter(search.text)

func _build_object_browser(browser: VBoxContainer):
 object_filters=HFlowContainer.new();browser.add_child(object_filters)
 object_category=OptionButton.new()
 for title in ['All objects','Buildings','Units','Scenery']:object_category.add_item(title)
 object_filters.add_child(object_category);object_category.item_selected.connect(func(_i):_filter(search.text))
 availability=OptionButton.new()
 for title in ['All objects','Buildable only','Non-buildable only']:availability.add_item(title)
 object_filters.add_child(availability);availability.item_selected.connect(func(_i):_filter(search.text))
 for pair in [['plot','Hide plots'],['placeholder','Hide placeholders'],['unfinished','Hide unfinished'],['unsupported','Hide unsupported']]:
  var check=CheckButton.new();check.text=pair[1];check.button_pressed=pair[0]!='unsupported';check.tooltip_text='Uses the native build-mode classification';object_filters.add_child(check);filter_toggles[pair[0]]=check;check.toggled.connect(func(_on):_filter(search.text))
 view_toggle=Button.new();view_toggle.text='▦ Grid · switch to list';view_toggle.pressed.connect(func():grid_mode=not grid_mode;view_toggle.text='▦ Grid · switch to list' if grid_mode else '☷ List · switch to grid';_render_object_results();_update_controls());browser.add_child(view_toggle)
 object_tree=Tree.new();object_tree.add_theme_font_size_override('font_size',18);object_tree.columns=4;object_tree.hide_root=true;object_tree.column_titles_visible=true;object_tree.size_flags_vertical=Control.SIZE_EXPAND_FILL;object_tree.custom_minimum_size.y=220
 for i in 4:
  object_tree.set_column_title(i,['Object name','Type','Cost','Classification / notes'][i]);object_tree.set_column_expand(i,i in [0,3]);object_tree.set_column_custom_minimum_width(i,[190,100,60,180][i])
 object_tree.item_selected.connect(func():var item=object_tree.get_selected();_select_object(str(item.get_metadata(0))) if item else null);browser.add_child(object_tree)
 object_results=ScrollContainer.new();object_results.size_flags_vertical=Control.SIZE_EXPAND_FILL;object_results.custom_minimum_size.y=220;object_results.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;browser.add_child(object_results)
 object_grid=GridContainer.new();object_grid.columns=3;object_grid.size_flags_horizontal=Control.SIZE_EXPAND_FILL;object_grid.add_theme_constant_override('h_separation',8);object_grid.add_theme_constant_override('v_separation',8);object_results.add_child(object_grid)
 paging=HBoxContainer.new();browser.add_child(paging)
 for direction in [-1,1]:
  var button=Button.new();button.text='Previous' if direction<0 else 'Next';button.pressed.connect(func():object_page=clampi(object_page+direction,0,maxi(0,(filtered.size()-1)/PAGE_SIZE));_render_object_results());paging.add_child(button)
 page_label=Label.new();paging.add_child(page_label)

func _filter(query: String):
 var previous=_selected()
 filtered=PackedStringArray();objects.clear();object_page=0
 var category=['','building','unit','scenery'][object_category.selected] if object_category else ''
 for name in catalog:
  var r:Dictionary=object_records.get(name,{'name':name,'cost':'0','flags':[],'notes':''})
  var flags=r.flags
  if not category.is_empty() and category not in flags:continue
  var buildable='trainable' in flags or 'builderBuildable' in flags or 'plotBuildable' in flags or 'enhancedPlotBuildable' in flags
  if availability.selected==1 and not buildable or availability.selected==2 and buildable:continue
  if filter_toggles.plot.button_pressed and 'plot' in flags:continue
  if filter_toggles.placeholder.button_pressed and 'placeholder' in flags:continue
  if filter_toggles.unfinished.button_pressed and 'unfinishedContent' in flags:continue
  if filter_toggles.unsupported.button_pressed and 'unsupported' in flags:continue
  if not query.is_empty() and query.to_lower() not in (name+' '+str(r.get('faction',''))+' '+str(r.get('display_name',''))+' '+str(r.get('producers',''))+' '+str(flags)+' '+r.notes).to_lower():continue
  filtered.append(name);objects.add_item(name)
  if name==previous:objects.select(objects.item_count-1)
 _clear_preview();_render_object_results()

func _select_object(name: String):
 var index=filtered.find(name)
 if index>=0:objects.select(index);_clear_preview()
 for card in object_grid.get_children():
  if card is Button:card.set_pressed_no_signal(card.get_meta('template','')==name)

func _render_object_results():
 result_generation+=1;thumbnail_queue.clear()
 for child in object_grid.get_children():child.free()
 object_tree.clear();var root=object_tree.create_item()
 var first=object_page*PAGE_SIZE;var last=mini(first+PAGE_SIZE,filtered.size())
 page_label.text='%d–%d / %d'%[first+1 if filtered.size() else 0,last,filtered.size()]
 for index in range(first,last):
  var name=filtered[index];var r:Dictionary=object_records.get(name,{'flags':[],'cost':'0','notes':''})
  var kind='Building' if 'building' in r.flags else ('Unit' if 'unit' in r.flags else 'Scenery / other')
  var notes=', '.join(r.flags)+' '+r.notes
  var row=object_tree.create_item(root);row.set_text(0,name);row.set_text(1,kind);row.set_text(2,r.cost);row.set_text(3,notes);row.set_metadata(0,name)
  for column in 4:row.set_tooltip_text(column,name+'\n'+notes)
  if name==_selected():row.select(0)
  if not grid_mode:continue
  var card=Button.new();card.toggle_mode=true;card.button_pressed=name==_selected();card.set_meta('template',name);card.size_flags_horizontal=Control.SIZE_EXPAND_FILL;card.custom_minimum_size=Vector2(190,188);card.tooltip_text=name+'\n'+kind+' · Cost '+r.cost+'\n'+notes;object_grid.add_child(card);card.pressed.connect(func():_select_object(name))
  var box=VBoxContainer.new();box.mouse_filter=Control.MOUSE_FILTER_IGNORE;box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);box.offset_left=8;box.offset_right=-8;box.offset_top=8;box.offset_bottom=-8;card.add_child(box)
  var image=TextureRect.new();image.mouse_filter=Control.MOUSE_FILTER_IGNORE;image.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;image.stretch_mode=TextureRect.STRETCH_KEEP_ASPECT_CENTERED;image.custom_minimum_size.y=128;image.size_flags_vertical=Control.SIZE_EXPAND_FILL;box.add_child(image)
  var label=Label.new();label.mouse_filter=Control.MOUSE_FILTER_IGNORE;label.text=name;label.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;label.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;label.add_theme_font_size_override('font_size',16);box.add_child(label)
  if thumbnail_cache.has(name):
   image.texture=thumbnail_cache[name]
   if image.texture==null:_missing_thumbnail(image)
  else:thumbnail_queue.append({'name':name,'image':image,'generation':result_generation})
 object_tree.visible=active and tool.selected in [1,2] and not grid_mode
 object_results.visible=active and tool.selected in [1,2] and grid_mode

func _missing_thumbnail(image: TextureRect):
 image.tooltip_text='No preview model available'
 var unavailable=Label.new();unavailable.text='No preview model';unavailable.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;unavailable.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);unavailable.vertical_alignment=VERTICAL_ALIGNMENT_CENTER;unavailable.add_theme_font_size_override('font_size',14);unavailable.mouse_filter=Control.MOUSE_FILTER_IGNORE;image.add_child(unavailable)

func _render_thumbnail(job: Dictionary):
 if not is_instance_valid(job.image) or job.generation!=result_generation:return
 thumbnail_busy=true
 var view=SubViewport.new();view.size=Vector2i(256,192);view.own_world_3d=true;view.render_target_update_mode=SubViewport.UPDATE_ONCE;add_child(view)
 var stage=Node3D.new();view.add_child(stage)
 var renderer=ClassDB.instantiate('W3DInstancer');stage.add_child(renderer);renderer.setup(fs)
 var model=world.get_template_model(job.name);var id=renderer.add_model(model) if not model.is_empty() else -1
 if id<0:
  _missing_thumbnail(job.image)
  thumbnail_cache[job.name]=null;view.queue_free();thumbnail_busy=false;return
 renderer.add_instance(id,Transform3D.IDENTITY,'',0,0);renderer.update_now()
 var cam=Camera3D.new();stage.add_child(cam);cam.current=true;cam.projection=Camera3D.PROJECTION_ORTHOGONAL;cam.far=100000
 var extent=maxf(10,float(renderer.get_model_radius(id)));cam.size=extent*2.5;cam.position=Vector3(1,.8,1).normalized()*extent*3.5;cam.look_at(Vector3.ZERO)
 var light=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.5;stage.add_child(light)
 var environment=WorldEnvironment.new()
 # Use the same neutral backdrop and ambient lighting as the selected-object preview.
 var env=Environment.new();env.background_mode=Environment.BG_COLOR;env.background_color=Color('#15232e');env.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;env.ambient_light_color=Color.WHITE;env.ambient_light_energy=.7;environment.environment=env;stage.add_child(environment)
 await RenderingServer.frame_post_draw
 if is_instance_valid(view):
  var picture=view.get_texture().get_image()
  if picture and not picture.is_empty():
   # Tighten the framing around visible pixels; some model pivots have very large bounds.
   var bg=picture.get_pixel(0,0);var bounds=Rect2i();var found=false
   for y in range(0,picture.get_height(),3):
    for x in range(0,picture.get_width(),3):
     var pixel=picture.get_pixel(x,y)
     if absf(pixel.r-bg.r)+absf(pixel.g-bg.g)+absf(pixel.b-bg.b)>.08:
      bounds=bounds.expand(Vector2i(x,y)) if found else Rect2i(x,y,1,1);found=true
   if found:
    var center=Vector2(bounds.get_center())-Vector2(view.size)*.5
    cam.position+=cam.basis.x*(center.x/float(view.size.y)*cam.size)-cam.basis.y*(center.y/float(view.size.y)*cam.size)
    cam.size*=maxf(float(bounds.size.x)/view.size.x,float(bounds.size.y)/view.size.y)*1.2
    view.render_target_update_mode=SubViewport.UPDATE_ONCE
    await RenderingServer.frame_post_draw
    if not is_instance_valid(view):thumbnail_busy=false;return
    picture=view.get_texture().get_image()
   if thumbnail_cache.size()>=96:thumbnail_cache.erase(thumbnail_cache.keys()[0])
   var image=ImageTexture.create_from_image(picture);thumbnail_cache[job.name]=image
   if is_instance_valid(job.image) and job.generation==result_generation:job.image.texture=image
  view.queue_free()
 thumbnail_busy=false

func _selected() -> String:
 var selected=objects.get_selected_items()
 return filtered[selected[0]] if not selected.is_empty() else ''

func _clear_preview():
 if is_instance_valid(preview):preview.queue_free()
 preview=null;preview_instance=-1;preview_template=''

func _process(_delta):
 if not world.is_logic_held():
  return
 stroke_clock+=_delta
 if dragging and tool.selected in [3,4,5,6] and points.size()>stroke_count and stroke_clock>=0.1:_paint_live()
 if not is_instance_valid(hud):queue_free();return
 if tool.selected==7 and not _over_editor(cursor):_pick_terrain(false)
 var pixels=get_viewport().get_visible_rect().size
 # Keep the editor compact; large displays should expose more map rather than enlarge every control.
 var ui_scale=clampf(pixels.y/1440.0,.6,1.0)
 var screen=pixels/ui_scale
 for window in [toolbar,panel,lookup]:window.scale=Vector2.ONE*ui_scale
 var toolbar_width=minf(1160,screen.x-24)
 toolbar.size=Vector2(toolbar_width,0);toolbar.position=Vector2((screen.x-toolbar_width)/2,12)*ui_scale
 var top=toolbar.size.y+24
 panel.position=Vector2(12,top)*ui_scale;scroll.custom_minimum_size.y=min(content.get_combined_minimum_size().y,max(160.0,screen.y-top-20));panel.reset_size()
 var browser_width=minf(780,maxf(660,screen.x*.44))
 lookup.position=Vector2(maxf(384,screen.x-browser_width-12),top)*ui_scale;lookup.size=Vector2(browser_width,maxf(300,screen.y-top-20))
 if not thumbnail_busy and not thumbnail_queue.is_empty() and grid_mode and lookup.visible and DisplayServer.get_name()!='headless':_render_thumbnail(thumbnail_queue.pop_front())
 if tool.selected in [1,2] and not grid_mode:_update_object_view()
 outline.queue_redraw()
 if tool.selected not in [1,2] or _over_editor(cursor):
  if is_instance_valid(preview):preview.hide()
  return
 var name=_selected();var point:Vector2=placement_anchor if dragging and tool.selected==1 else _snap_point(hud.pixel_to_world(cursor))
 if name.is_empty():status.text='Select an object from the list to preview and place it.';return
 if point.x < -1e8:return
 if preview_template!=name:
  _clear_preview();preview_template=name
  preview=ClassDB.instantiate('W3DInstancer');world.add_child(preview);preview.setup(fs);preview.set_alpha_override(true)
  var model=world.get_template_model(name)
  var id=preview.add_model(model) if not model.is_empty() else -1
  if id>=0:preview_instance=preview.add_instance(id,Transform3D.IDENTITY,'',0,0);preview.set_instance_opacity(preview_instance,.5)
  status.text='Preview: '+name if id>=0 else 'No preview model: '+name
 if preview_instance>=0:
  preview.show();preview.set_instance_transform(preview_instance,Transform3D(Basis(Vector3.UP,_placement_angle()),Vector3(point.x,world.get_ground_height(point.x,point.y),-point.y)))

func _zoom_camera(notches: float):
 # Editor mode disables gameplay input; zoom the client camera directly.
 var state:Dictionary=hud.get_camera()
 if not state.has('height_above_ground'):return
 var height=float(state.height_above_ground)
 hud.camera_set_height(height*pow(.85,notches))
 outline.queue_redraw()

func handle_input(event: InputEvent) -> bool:
 if not active:return false
 if event is InputEventWithModifiers:snap_held=event.shift_pressed or toolbar.find_child('SnapToggle',true,false).button_pressed
 if event is InputEventKey and event.pressed and not event.echo:
  if event.keycode==KEY_ESCAPE:set_active(false);return true
  if get_viewport().gui_get_focus_owner() is LineEdit:return false
  if event.keycode==KEY_R:angle_control.value+=15;return true
 if event is InputEventMouse:
  cursor=event.position
  if _over_editor(cursor) and not dragging and not road_drag:return false
 # macOS trackpads and Magic Mouse report scrolling as pan gestures and pinch as magnify, not wheel buttons; gameplay's HUD
 # handles those but its input is off in the editor. Scrolling up / spreading the fingers zooms in, like the wheel.
 if event is InputEventPanGesture or event is InputEventMagnifyGesture:
  cursor=event.position
  if _over_editor(cursor):return false
  if event is InputEventPanGesture and is_finite(event.delta.y):_zoom_camera(-event.delta.y*0.5)
  elif event is InputEventMagnifyGesture and is_finite(event.factor) and event.factor>0:_zoom_camera(log(event.factor)/log(1/.85))
  return true
 if event is InputEventMouseButton:
  if event.button_index in [MOUSE_BUTTON_WHEEL_UP,MOUSE_BUTTON_WHEEL_DOWN] and event.pressed:
   _zoom_camera((1 if event.button_index==MOUSE_BUTTON_WHEEL_UP else -1)*(event.factor if event.factor>0 else 1.0));return true
  if event.button_index==MOUSE_BUTTON_LEFT:
   if event.pressed:
    if tool.selected==8:_road_press()
    else:_begin_stroke()
   elif road_drag:_road_release()
   elif dragging:
    _end_stroke()
   return true
 if event is InputEventMouseMotion:
  if event.button_mask & MOUSE_BUTTON_MASK_RIGHT:hud.camera_pan(-event.relative*2);return true
  if road_drag:_road_motion()
  elif dragging:_sample(cursor)
  return true
 if event is InputEventScreenTouch:
  cursor=event.position
  if event.pressed:
   if _over_editor(cursor):return false
   touches[event.index]=cursor
   if touches.size()>1:
    if road_drag:roads=road_before.duplicate(true);road_drag=false
    dragging=false;_finish_terrain_stroke();points.clear();return true
   if tool.selected==8:_road_press()
   else:_begin_stroke()
  else:
   touches.erase(event.index)
   if road_drag:_road_release()
   elif dragging:_end_stroke()
  return true
 if event is InputEventScreenDrag:
  cursor=event.position
  if not touches.has(event.index):return false
  touches[event.index]=cursor
  if touches.size()>1:hud.camera_pan(-event.relative);return true
  if road_drag:_road_motion()
  elif dragging:_sample(cursor)
  return true
 return false

func _road_point(p) -> Vector2:
 return Vector2(p[0],p[1])

func _refresh_roads():
 road_list.clear()
 for i in roads.size():road_list.add_item('%d · %s'%[i+1,roads[i].name])
 if road_selected>=0 and road_selected<roads.size():road_list.select(road_selected)

func _select_road_type():
 if road_selected<0:return
 road_width.value=float(roads[road_selected].get('width',0))
 for i in road_type.item_count:
  if road_type.get_item_text(i).to_lower()==str(roads[road_selected].name).to_lower():road_type.select(i);_render_paths();return

## a new segment of the selected road type, with the width setting when it is not the type's default (0)
func _road_segment(name: String,a: Vector2,b: Vector2,width: float) -> Dictionary:
 var segment={'name':name,'points':[[a.x,a.y],[b.x,b.y]]}
 if width>0:segment['width']=width
 return segment

func _new_road():
 road_selected=-1;road_endpoint=-1;road_anchor=Vector2(INF,INF);road_action.select(1);road_list.deselect_all()
 status.text='Click the first point, then click more points to extend the path.'

func _change_road_type():
 if road_selected<0 or road_type.selected<0:return
 var before=roads.duplicate(true);roads[road_selected].name=road_type.get_item_text(road_type.selected)
 if road_width.value>0:roads[road_selected]['width']=road_width.value
 else:roads[road_selected].erase('width')
 _commit_roads(before)

func _delete_road():
 if road_selected<0:return
 var before=roads.duplicate(true);roads.remove_at(road_selected);road_selected=-1;road_anchor=Vector2(INF,INF);_commit_roads(before)

func _commit_roads(before: Array):
 var result:Dictionary=world.map_editor_terrain({'roads':roads})
 if result.get('ok',false):
  undo_stack.append({'roads':before});overlay.append({'roads':roads.duplicate(true)});status.text='Updated %d road segments'%roads.size()
 else:roads=before.duplicate(true);status.text=str(result.get('error','Road update failed'))
 _refresh_roads()

func _road_press():
 var p:Vector2=hud.pixel_to_world(cursor)
 if p.x < -1e8:return
 if road_action.selected==1:
  if road_type.selected<0:status.text='No road types available.';return
  if is_finite(road_anchor.x) and p.distance_to(road_anchor)>1:
   var before=roads.duplicate(true)
   roads.append(_road_segment(road_type.get_item_text(road_type.selected),road_anchor,p,road_width.value))
   road_selected=roads.size()-1;road_endpoint=1;_commit_roads(before)
  road_anchor=p;return
 var closest=18.0;road_selected=-1;road_endpoint=-1
 for i in roads.size():
  for j in 2:
   var distance=cursor.distance_to(hud.world_to_pixel(_road_point(roads[i].points[j])))
   if distance<closest:closest=distance;road_selected=i;road_endpoint=j
 if road_selected>=0 and road_action.selected==0:
  road_anchor=_road_point(roads[road_selected].points[road_endpoint]);road_drag_origin=road_anchor;road_before=roads.duplicate(true);road_drag=true;_select_road_type();_refresh_roads();return
 closest=24.0
 for i in roads.size():
  var a=hud.world_to_pixel(_road_point(roads[i].points[0]));var b=hud.world_to_pixel(_road_point(roads[i].points[1]))
  var distance=cursor.distance_to(Geometry2D.get_closest_point_to_segment(cursor,a,b))
  if distance<closest:closest=distance;road_selected=i
 if road_selected<0:return
 _select_road_type();_refresh_roads()
 if road_action.selected==2:
  var before=roads.duplicate(true);var road=roads[road_selected]
  var split=Geometry2D.get_closest_point_to_segment(p,_road_point(road.points[0]),_road_point(road.points[1]))
  if split.distance_to(_road_point(road.points[0]))<1 or split.distance_to(_road_point(road.points[1]))<1:return
  var end=road.points[1].duplicate();road.points[1]=[split.x,split.y]
  var tail={'name':road.name,'points':[[split.x,split.y],end]}
  if road.has('width'):tail['width']=road.width
  roads.insert(road_selected+1,tail);_commit_roads(before)
 else:road_endpoint=1;road_anchor=_road_point(roads[road_selected].points[1])

func _road_motion():
 var p:Vector2=hud.pixel_to_world(cursor)
 if p.x < -1e8:return
 # Shared endpoints move together so adjacent segments stay connected.
 for i in roads.size():
  for j in 2:
   if _road_point(road_before[i].points[j]).distance_to(road_drag_origin)<0.1:roads[i].points[j]=[p.x,p.y]
 road_anchor=p

func _road_release():
 _road_motion();road_drag=false
 if roads!=road_before:_commit_roads(road_before)

func _pick_terrain(select: bool):
 var p:Vector2=hud.pixel_to_world(cursor)
 if p.x < -1e8:return
 var cell=(p/10).floor()
 if not select and cell==terrain_cursor_cell and not hovered_texture.is_empty():return
 terrain_cursor_cell=cell
 var result:Dictionary=world.map_editor_terrain({'sample':p})
 if not result.get('ok',false):
  hovered_texture='';texture_preview.texture=null;texture_caption.text='No terrain texture';return
 var name=str(result.texture)
 if select:
  texture_search.text='';texture_category.select(0);_terrain_subcategories()
  for i in texture_filtered.size():
   if texture_filtered[i].name.to_lower()==name.to_lower():texture.select(i);_texture_selected(i);tool.select(6);_update_controls();status.text='Selected terrain: '+name;return
 elif name!=hovered_texture:
  hovered_texture=name
  var image:Dictionary=terrain_loader.preview_texture(fs,name)
  texture_preview.texture=image.get('texture',null);texture_caption.text=name+'\n'+str(image.get('file',image.get('error','')))

func _end_stroke():
 if tool.selected==1:_rotate_placement()
 else:_sample(cursor)
 dragging=false;_apply()

func _snap_point(point: Vector2) -> Vector2:
 return point.snapped(Vector2.ONE*placement_grid) if snap_held and point.x>-1e8 else point

func _placement_angle() -> float:
 var angle=deg_to_rad(angle_control.value)
 return snappedf(angle,PI/4) if snap_held else angle

func _rotate_placement():
 if tool.selected!=1 or not dragging or cursor.distance_to(start)<8:return
 var aim:Vector2=hud.pixel_to_world(cursor)
 var delta=aim-placement_anchor
 if delta.length()>1:angle_control.value=rad_to_deg(atan2(delta.y,delta.x))

func _begin_stroke():
 if tool.selected==7:_pick_terrain(true);return
 dragging=true;start=cursor;points=[];stroke_undo=[];stroke_seen={};stroke_count=0;stroke_clock=0
 _sample(cursor)
 if tool.selected==1 and not points.is_empty():placement_anchor=points[0]
 if not points.is_empty():stroke_height=world.get_ground_height(points[0].x,points[0].y)
 if tool.selected in [3,4,5,6]:_paint_live()

func _terrain_command() -> Dictionary:
 var command={'points':points.duplicate(),'radius':radius.value,'square':shape.selected==0,'amount':strength.value,'grid_angle':PI/4 if grid_rotation.button_pressed else 0.0}
 match tool.selected:
  3:command.mode='raise'
  4:command.mode='lower';command.amount=-strength.value
  5:command.mode='flatten';command.height=stroke_height
  6:
   if texture.selected<0:return {}
   command.mode='paint';command.texture=texture_filtered[texture.selected].name
 return command

func _paint_live():
 if points.size()<=stroke_count:return
 var command=_terrain_command()
 if command.is_empty():return
 command.points=points.slice(stroke_count)
 command.skip=stroke_seen.keys()
 var result:Dictionary=world.map_editor_terrain(command)
 if not result.get('ok',false):status.text=str(result.get('error','Terrain edit failed'));return
 for sample in result.undo:
  stroke_seen[sample.index]=true;stroke_undo.append(sample)
 stroke_count=points.size();stroke_clock=0;_refresh_camera()
 status.text='Edited %d terrain samples'%stroke_undo.size()

func _finish_terrain_stroke():
 if stroke_undo.is_empty():return
 undo_stack.append({'terrain':stroke_undo});overlay.append({'terrain':_terrain_command()})
 stroke_undo=[];stroke_seen={};stroke_count=0

func _sample(pixel: Vector2):
 if tool.selected==1 and dragging and not points.is_empty():_rotate_placement();return
 var p:Vector2=_snap_point(hud.pixel_to_world(pixel)) if tool.selected in [1,2] else hud.pixel_to_world(pixel)
 if p.x < -1e8:return
 var spacing=max(10.0,radius.value*.35)
 if points.is_empty():points.append(p);return
 var previous:Vector2=points[-1];var distance=previous.distance_to(p)
 if distance<spacing:return
 for i in range(1,min(100,int(distance/spacing)+1)+1):points.append(previous.lerp(p,min(1.0,i*spacing/distance)))

func _snapshot(id: int) -> Dictionary:
 var data:Dictionary=world.get_object(id)
 return {'original_id':id,'template':data.get('template',''),'owner':data.get('player_id',0),'x':data.get('x',0),'y':data.get('y',0),'angle':data.get('angle',0)} if data.get('ok',false) else {}

func _delete_ids() -> Array[int]:
 var ids:Array[int]=[];var area=Rect2(start,cursor-start).abs()
 if start.distance_to(cursor)<8:
  var picked=int(hud.editor_pick(cursor)) if hud.has_method('editor_pick') else int(hud.pick_probe(cursor,0).get('picked',0))
  if picked>0:ids.append(picked);return ids
  var nearest=25.0
  for raw in world.get_object_ids():
   var b:Dictionary=world.get_object(raw)
   if not b.get('ok',false):continue
   var distance=cursor.distance_to(hud.world_to_pixel(Vector2(b.x,b.y)))
   if distance<nearest:nearest=distance;ids=[int(raw)]
 else:
  for raw in world.get_object_ids():
   var b:Dictionary=world.get_object(raw)
   if b.get('ok',false) and area.has_point(hud.world_to_pixel(Vector2(b.x,b.y))):ids.append(int(raw))
 return ids

func _refresh_camera():
 if hud.has_method('camera_refresh_terrain'):hud.camera_refresh_terrain()

func _apply_owner():
 if selected_objects.is_empty():status.text='Click an object or drag an area to select objects.';return
 var changes=[];var saved=[]
 for id in selected_objects:
  var obj=_snapshot(id)
  if obj.is_empty():continue
  changes.append({'id':id,'owner':int(owner_slot.value)});obj['new_owner']=int(owner_slot.value);saved.append(obj)
 var result:Dictionary=world.map_editor_terrain({'owners':changes})
 if result.get('ok',false):undo_stack.append({'owners':result.undo_owners});overlay.append({'owners':saved});status.text='Changed owner for %d objects'%changes.size()
 else:status.text=str(result.get('error','Owner change failed'))
 if inspector.visible:_inspect_selection()

func _apply():
 if tool.selected==10:
  if not points.is_empty():_apply_bordered_box(points[0],hud.pixel_to_world(cursor))
  return
 if tool.selected==9:
  selected_objects=_delete_ids()
  if not selected_objects.is_empty():owner_slot.value=world.get_object(selected_objects[0]).get('player_id',0)
  _inspect_selection()
  status.text='Selected %d objects. Details are in the selection window.'%selected_objects.size();return
 if tool.selected==0:
  var deleted:Array=[]
  for id in _delete_ids():
   var data=_snapshot(id)
   if not data.is_empty() and world.destroy_object(id):deleted.append(data)
  if not deleted.is_empty():undo_stack.append({'restore':deleted});overlay.append({'delete':deleted})
  world.map_editor_terrain({'sync':true});status.text='Deleted %d objects'%deleted.size();return
 if points.is_empty():return
 if tool.selected in [1,2]:
  var template=_selected()
  if template.is_empty():status.text='Choose an object from the list.';return
  var placed:Array=[];var ids:Array=[]
  var sites:Array[Vector2]=[]
  if tool.selected==1:sites.append(points[-1])
  else:
   var seen={};var spacing=max(10.0,strength.value)
   for center in points:
    for y in range(-int(radius.value/spacing),int(radius.value/spacing)+1):
     for x in range(-int(radius.value/spacing),int(radius.value/spacing)+1):
      var offset=Vector2(x,y)*spacing
      if shape.selected==1 and offset.length()>radius.value:continue
      var rotation=PI/4 if grid_rotation.button_pressed else 0.0
      var p=((center.rotated(-rotation)+offset).snapped(Vector2.ONE*spacing)).rotated(rotation);var key=str(p)
      if not seen.has(key):seen[key]=true;sites.append(p)
  if sites.size()>256:status.text='Brush exceeds 256 objects. Increase spacing or reduce radius.';return
  for p in sites:
   var id=world.create_object(template,int(owner_slot.value),p.x,p.y,_placement_angle())
   if id>=0:ids.append(id);placed.append({'template':template,'owner':int(owner_slot.value),'x':p.x,'y':p.y,'angle':_placement_angle()})
  if not ids.is_empty():undo_stack.append({'remove':ids});overlay.append({'place':placed})
  world.map_editor_terrain({'sync':true});status.text='Placed %d objects'%ids.size();return
 _paint_live()
 _finish_terrain_stroke()

## the box's corners in order; diagonal: the box with a and b as opposite corners in a frame rotated 45°
func _box_corners(a: Vector2,b: Vector2,diagonal:=false) -> Array[Vector2]:
 var turn=PI/4 if diagonal else 0.0
 var ra=a.rotated(-turn);var rb=b.rotated(-turn)
 var lo=Vector2(minf(ra.x,rb.x),minf(ra.y,rb.y));var hi=Vector2(maxf(ra.x,rb.x),maxf(ra.y,rb.y))
 var corners:Array[Vector2]=[]
 for c in [lo,Vector2(hi.x,lo.y),hi,Vector2(lo.x,hi.y)]:corners.append(c.rotated(turn))
 return corners

func _apply_bordered_box(a: Vector2,b: Vector2,saved: Dictionary={}):
 for point in [a,b]:
  if not is_finite(point.x) or not is_finite(point.y) or absf(point.x)>1e7 or absf(point.y)>1e7:return false
 if saved.is_empty() and (texture.selected<0 or (road_type.selected<0 and not border_none)):status.text='Choose a ground texture and a border path (or None).';return false
 var diagonal=bool(saved.get('diagonal',false)) if not saved.is_empty() else box_diagonal.button_pressed
 var corners=_box_corners(a,b,diagonal)
 if corners[0].distance_to(corners[1])<10 or corners[1].distance_to(corners[2])<10:status.text='Drag a box at least one terrain cell wide and high.';return false
 var name=str(saved.texture) if saved.has('texture') else str(texture_filtered[texture.selected].name)
 var border_name=str(saved.road) if saved.has('road') else ('' if border_none else road_type.get_item_text(road_type.selected))
 var border_width=float(saved.get('width',0)) if not saved.is_empty() else road_width.value
 var command={'mode':'paint','texture':name,'points':[(a+b)*.5]}
 if diagonal:command['polygon']=corners.map(func(c):return [c.x,c.y])
 else:command['rectangle']=[corners[0].x,corners[0].y,corners[2].x,corners[2].y]
 var painted:Dictionary=world.map_editor_terrain(command)
 if not painted.get('ok',false):status.text=str(painted.get('error'));return false
 if border_name.is_empty():
  undo_stack.append({'terrain':painted.undo})
  overlay.append({'bordered_box':{'a':[a.x,a.y],'b':[b.x,b.y],'texture':name,'road':'','width':0,'diagonal':diagonal}})
  _refresh_camera();status.text='Painted box without borders.'
  return true
 var bordered=roads.duplicate(true)
 for i in 4:
  var p=corners[i];var q=corners[(i+1)%4]
  # FLAG_ROAD_CORNER_ANGLED (0x08) on both ends: mitred square corners, not the rounded curve artwork
  var side=_road_segment(border_name,p,q,border_width);side['flags']=[8,8];bordered.append(side)
 var result:Dictionary=world.map_editor_terrain({'roads':bordered})
 if not result.get('ok',false):
  world.map_editor_terrain({'patch':painted.undo});status.text=str(result.get('error'));return false
 undo_stack.append({'terrain':painted.undo,'roads':result.undo_roads})
 overlay.append({'bordered_box':{'a':[a.x,a.y],'b':[b.x,b.y],'texture':name,'road':border_name,'width':border_width,'diagonal':diagonal}})
 roads=bordered;_refresh_roads();_refresh_camera();status.text='Painted box with four road borders.'
 return true

func _undo():
 if undo_stack.is_empty():return
 var action=undo_stack.pop_back()
 if action.has('remove'):
  for id in action.remove:world.destroy_object(id)
 if action.has('restore'):
  for b in action.restore:
   var new_id=world.create_object(b.template,int(b.owner),b.x,b.y,b.angle)
   for earlier in undo_stack:
    if earlier.has('remove'):
     var index=earlier.remove.find(b.get('original_id',-1))
     if index>=0:earlier.remove[index]=new_id
 if action.has('owners'):world.map_editor_terrain({'owners':action.owners})
 if action.has('roads'):
  var restored:Dictionary=world.map_editor_terrain({'roads':action.roads})
  if restored.get('ok',false):roads=action.roads.duplicate(true);road_selected=-1;road_anchor=Vector2(INF,INF);_refresh_roads()
 if action.has('terrain'):world.map_editor_terrain({'patch':action.terrain});_refresh_camera()
 if not overlay.is_empty():overlay.pop_back()
 world.map_editor_terrain({'sync':true})
 status.text='Last edit undone.'

func _path() -> String:
 var key=save_name.text.strip_edges()
 if key.is_empty() or key.validate_filename()!=key:return ''
 return 'user://map_edits/'+key+'.json'

func _encode(value):
 if value is Vector2:return [value.x,value.y]
 if value is Array:
  var output=[]
  for item in value:output.append(_encode(item))
  return output
 if value is Dictionary:
  var output={}
  for key in value:output[key]=_encode(value[key])
  return output
 return value

func _save():
 var path=_path()
 if path.is_empty():status.text='Enter a valid file name.';return
 DirAccess.make_dir_recursive_absolute('user://map_edits')
 var file=FileAccess.open(path,FileAccess.WRITE)
 if not file:status.text='Could not save edit file.';return
 file.store_string(JSON.stringify({'version':1,'map':terrain_info.get('map',''),'operations':_encode(overlay)},'  '));file.close()
 status.text='Saved '+path

func _load():
 var path=_path()
 if path.is_empty() or not FileAccess.file_exists(path):status.text='Edit file not found.';return
 if not overlay.is_empty():status.text='Load edits on a fresh map to avoid applying them twice.';return
 var document=JSON.parse_string(FileAccess.get_file_as_string(path))
 if not document is Dictionary or document.get('version',0)!=1 or document.get('map','')!=terrain_info.get('map',''):
  status.text='Invalid edit file or different map.';return
 for action in document.get('operations',[]):
  if action.has('bordered_box'):
   var box=action.bordered_box
   if not _apply_bordered_box(Vector2(box.a[0],box.a[1]),Vector2(box.b[0],box.b[1]),box):return
   continue
  if action.has('terrain'):
   var command:Dictionary=action.terrain.duplicate(true);var samples:Array[Vector2]=[]
   for p in command.points:samples.append(Vector2(p[0],p[1]))
   command.points=samples;var result:Dictionary=world.map_editor_terrain(command)
   if result.get('ok',false):undo_stack.append({'terrain':result.undo});_refresh_camera()
   else:status.text=str(result.get('error'));return
  elif action.has('owners'):
   var changes=[]
   for saved in action.owners:
    for id in world.get_object_ids():
     var obj:Dictionary=world.get_object(id)
     if obj.get('ok',false) and obj.template==saved.template and Vector2(obj.x,obj.y).distance_to(Vector2(saved.x,saved.y))<1:
      changes.append({'id':id,'owner':int(saved.new_owner)});break
   var result:Dictionary=world.map_editor_terrain({'owners':changes})
   if not result.get('ok',false):status.text=str(result.get('error'));return
   undo_stack.append({'owners':result.undo_owners})
  elif action.has('roads'):
   var result:Dictionary=world.map_editor_terrain({'roads':action.roads})
   if not result.get('ok',false):status.text=str(result.get('error'));return
   undo_stack.append({'roads':result.undo_roads});roads=action.roads.duplicate(true);_refresh_roads()
  elif action.has('place'):
   var ids=[]
   for b in action.place:
    var id=world.create_object(b.template,int(b.owner),b.x,b.y,b.angle)
    if id>=0:ids.append(id)
   undo_stack.append({'remove':ids})
  elif action.has('delete'):
   var removed=[]
   for b in action.delete:
    for raw in world.get_object_ids():
     var live:Dictionary=world.get_object(raw)
     if live.get('ok',false) and live.template==b.template and Vector2(live.x,live.y).distance_to(Vector2(b.x,b.y))<1:
      var snapshot=_snapshot(raw)
      if world.destroy_object(raw):removed.append(snapshot)
      break
   undo_stack.append({'restore':removed})
  overlay.append(action)
 world.map_editor_terrain({'sync':true})
 status.text='Loaded '+path

func _exit_tree():
 _clear_preview()
 if active and is_instance_valid(hud):hud.set_process_input(hud_was_processing)
 if active and is_instance_valid(world):world.set_shroud_drawn(was_shroud);world.set_paused(was_paused)

func _build_inspector():
 inspector=Window.new();inspector.title='Selected object';inspector.size=Vector2i(620,660);inspector.min_size=Vector2i(440,360);inspector.visible=false;add_child(inspector)
 inspector.close_requested.connect(func():inspector.hide())
 var margin=MarginContainer.new();margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 for edge in ['left','right','top','bottom']:margin.add_theme_constant_override('margin_'+edge,16)
 inspector.add_child(margin)
 var box=VBoxContainer.new();margin.add_child(box)
 inspector_objects=ItemList.new();inspector_objects.custom_minimum_size.y=120;box.add_child(inspector_objects)
 inspector_objects.item_selected.connect(func(i):_inspect_object(int(inspector_objects.get_item_metadata(i))))
 inspector_details=RichTextLabel.new();inspector_details.selection_enabled=true;inspector_details.size_flags_vertical=Control.SIZE_EXPAND_FILL;box.add_child(inspector_details)
 inspector_details.text='Click an object or drag an area to inspect objects.'

func _inspect_selection():
 inspector_objects.clear()
 for id in selected_objects:
  var data:Dictionary=world.get_object(id)
  if not data.get('ok',false):continue
  var row=inspector_objects.add_item('%d · %s'%[id,data.get('template','')]);inspector_objects.set_item_metadata(row,id)
 inspector.popup_centered()
 if inspector_objects.item_count>0:
  inspector_objects.select(0);_inspect_object(int(inspector_objects.get_item_metadata(0)))
 else:inspector_details.text='No object selected. Click an object or drag an area.'

func _inspect_object(id: int):
 var data:Dictionary=world.get_object(id)
 if not data.get('ok',false):inspector_details.text='Object %d no longer exists.'%id;return
 inspector_details.text=_object_details(data)

func _object_details(data: Dictionary) -> String:
 var name=str(data.get('template',''))
 var record:Dictionary=object_records.get(name,{})
 var lines=PackedStringArray(['Template / name: '+name,'Object ID: '+str(data.get('id',''))])
 if not str(record.get('display_name','')).is_empty():lines.append('Display name / label: '+str(record.display_name))
 if not str(record.get('faction','')).is_empty():lines.append('Faction: '+str(record.faction))
 lines.append('Owner: %s · Team: %s'%[owner_slot.label_for(int(data.get('player_id',0))),data.get('team','')])
 if data.has('health'):lines.append('Health: %s / %s'%[data.health,data.get('max_health','?')])
 lines.append('Position: %.1f, %.1f, %.1f · Rotation: %.1f°'%[data.get('x',0),data.get('y',0),data.get('z',0),rad_to_deg(float(data.get('angle',0)))])
 if data.get('under_construction',false):lines.append('Construction: %s%%'%data.get('construction_percent',0))
 if record.has('cost'):lines.append('Build cost: '+str(record.cost))
 if not str(record.get('producers','')).is_empty():lines.append('Built / trained by: '+str(record.producers).trim_prefix(', ').trim_suffix(', '))
 if record.has('flags'):lines.append('Classification: '+', '.join(record.flags))
 if not str(record.get('notes','')).is_empty():lines.append('Notes: '+str(record.notes))
 if data.get('contained_by',0)!=0:lines.append('Contained by object: '+str(data.contained_by))
 if not data.get('members',[]).is_empty():lines.append('Contained / horde members: '+str(data.members))
 if data.has('ai_state'):lines.append('AI state: '+str(data.ai_state))
 if data.has('ai_victim'):lines.append('Target object: '+str(data.ai_victim))
 if data.has('ai_goal'):lines.append('AI goal: '+str(data.ai_goal))
 if data.has('ai_has_path'):lines.append('Path: %s · Blocked: %s · Moving: %s'%[data.ai_has_path,data.get('ai_blocked',false),data.get('ai_moving',false)])
 lines.append('Drawable hidden: '+str(data.get('drawable_hidden',false)))
 return '\n\n'.join(lines)
