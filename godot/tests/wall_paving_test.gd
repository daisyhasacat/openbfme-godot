extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 assert(fs.mount_retail().ok)
 var world=ClassDB.instantiate('GameWorld');root.add_child(world)
 assert(world.setup(fs).ok)
 var slots:Array=[]
 for i in 8:slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
 slots[0]={'state':6,'name':'PavingTest','accepted':true,'color':0,'start_pos':0,'player_template':3,'team':0}
 assert(world.prepare_new_game({'mode':'skirmish','difficulty':1,'rank_points':0,'map_crc':0,'map_size':0,'map_mask':0,'map':'maps/map mp evendim/map mp evendim.map','seed':9001,'starting_cash':100000,'slots':slots}).ok)
 assert(world.start_new_game({'wall_rebalance':true,'starting_walls':true,'require_enclosed':false,'logic_thread':false}).ok)
 for i in 8:world.advance(.2)
 var terrain=world.get_node('Terrain')
 # tolerance: the retail map's cell count varies; a completed ring must produce a nonempty paved region.
 assert(int(terrain.get_meta('paving_cells',0))>0,'Completed starting walls must receive automatic paving')
 var materials:Array=[];var nodes:Array=[terrain]
 while not nodes.is_empty():
  var node=nodes.pop_back()
  nodes.append_array(node.get_children())
  if node is MeshInstance3D:
   for surface in node.mesh.get_surface_count():
    var material=node.get_active_material(surface)
    if material is ShaderMaterial and material.shader.resource_path=='res://shaders/terrain.gdshader':materials.append(material)
 assert(not materials.is_empty())
 for material in materials:
  assert(material.get_shader_parameter('paving_enabled'))
  assert(material.get_shader_parameter('paving_texture') is Texture2D)
  assert(material.get_shader_parameter('paving_mask') is Texture2D)
 # Ordinary structures also change the navigation topology used by the mask.
 var before=int(terrain.get_meta('paving_cells'))
 var image:Image=materials[0].get_shader_parameter('paving_mask').get_image()
 var site=Vector2.ZERO
 for y in range(4,image.get_height()-4):
  for x in range(4,image.get_width()-4):
   if image.get_pixel(x,y).r<.5:continue
   var clear=true
   for dy in range(-3,4):
    for dx in range(-3,4):
     if image.get_pixel(x+dx,y+dy).r<.5:clear=false
   if clear:site=Vector2((x+.5)*10,(y+.5)*10);break
  if site!=Vector2.ZERO:break
 assert(site!=Vector2.ZERO,'Need an interior patch for the structure invalidation regression')
 if '--visual' in OS.get_cmdline_user_args():
  root.size=Vector2i(1280,900)
  var camera=Camera3D.new();world.add_child(camera)
  camera.position=Vector3(site.x,600,-site.y+500);camera.look_at(Vector3(site.x,0,-site.y));camera.current=true
  world.set_shroud_drawn(false)
  await process_frame
  await process_frame
  await RenderingServer.frame_post_draw
  root.get_texture().get_image().save_png('/tmp/wall-paving.png')
 var statue=world.create_object('GondorStatue',0,site.x,site.y,0)
 # tolerance: object IDs depend on the retail map and starting payload; any allocated nonzero ID is valid.
 assert(statue>0)
 for i in 2:world.advance(.2)
 # tolerance: the statue must remove some navigable cells; its exact footprint rasterization is covered by placement tests.
 assert(int(terrain.get_meta('paving_cells'))<before,'A new structure must invalidate the paving mask')
 assert(world.destroy_object(statue))
 for i in 2:world.advance(.2)
 assert(int(terrain.get_meta('paving_cells'))==before,'Removing the structure restores the paving mask')
 # Removing the entire wall ring restores native terrain, without editing its tiles.
 for id in world.get_object_ids():
  var obj=world.get_object(id)
  if 'Wall' in str(obj.get('template','')):world.destroy_object(id)
 for i in 2:world.advance(.2)
 assert(int(terrain.get_meta('paving_cells',-1))==0)
 for material in materials:assert(not material.get_shader_parameter('paving_enabled'))
 world.free()
 print('WALL PAVING PASS');quit()
