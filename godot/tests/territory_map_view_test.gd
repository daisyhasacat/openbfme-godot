extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
 root.size=Vector2i(1280,960)
 create_timer(90).timeout.connect(func(): push_error("Territory viewport test timed out"); quit(1))
 var fs = ClassDB.instantiate("RetailFileSystem")
 assert(fs.mount_retail().ok)
 var world = ClassDB.instantiate("GameWorld")
 root.add_child(world)
 assert(world.setup(fs).ok)
 var options := FileAccess.open("user://territory-map-view-options.ini",FileAccess.WRITE)
 options.store_string("SuppressStrategicPromo = true\nStaticGameLOD = UltraHigh\n")
 options.close()
 var needs_profile := not FileAccess.file_exists("user://territory-map-view-test.ini")
 var shell = ClassDB.instantiate("AptMenuPlayer")
 root.add_child(shell)
 assert(shell.boot_shell(fs,world,{"skirmish_ini":"user://territory-map-view-test.ini","options_file":"user://territory-map-view-options.ini"}).ok)
 shell.auto_process = false
 assert(shell.shell_push("MainMenu.apt"))
 for frame in 60: shell.tick(1.0/30.0)
 assert(shell.shell_fscommand("AptMainMenu::WarOfTheRing",""))
 for frame in 100:
  shell.tick(1.0/30.0)
  shell.render(false)
  await process_frame
 if needs_profile: assert(shell.lobby_apply({"profile":"TerritoryViewTest"}).ok)
 for frame in 20:
  shell.tick(1.0/30.0)
  shell.render(false)
  await process_frame
 assert(shell.lobby_apply({"strategic_scenario":2}).ok)
 assert(shell.lobby_apply({"slots":[{"slot":0,"color":0},{"slot":1,"color":1}]}).ok)
 for frame in 4:
  shell.tick(0.0)
  shell.render(false)
  await process_frame
 var report: Dictionary = shell.get_report().living_world
 print("TERRITORY VIEW meshes=",report.region_meshes," starts=",report.starts," errors=",report.errors)
 assert(report.visible,str(report))
 assert(report.errors.is_empty(),str(report))
 assert(report.meshes > 0) # tolerance: mounted models may have different subobject counts; an empty viewport is always invalid.
 assert(report.ocean_meshes > 0) # tolerance: this mounted map must use its authored water shader rather than the generic white FX fallback.
 assert(report.region_meshes > 0) # tolerance: every mounted region campaign must expose pickable territory meshes.
 # A scenario with preset starts first releases the local player's claimed region.
 for region in report.regions:
  if region.holder != 0: continue
  var released := false
  for point in region.pick_points:
   if not report.rect.has_point(point): continue
   shell.post_mouse_move_stage(shell.window_to_stage(point))
   if shell.get_report().living_world.hover != region.name: continue
   shell.post_mouse_button(true)
   shell.post_mouse_button(false)
   released = shell.get_report().living_world.starts[0].is_empty()
   if released: break
  if released: break
 report = shell.get_report().living_world
 var before_panel: Image
 if DisplayServer.get_name() != "headless":
  await RenderingServer.frame_post_draw
  before_panel=root.get_texture().get_image().get_region(Rect2i(report.rect))
 var picked_point := Vector2.ZERO
 var picked := false
 for region in report.regions:
  if not region.allowed or region.holder != -1: continue
  for point in region.pick_points:
   if not report.rect.has_point(point): continue
   shell.post_mouse_move_stage(shell.window_to_stage(point))
   if shell.get_report().living_world.hover != region.name: continue
   var before_starts: Array = shell.get_report().living_world.starts
   shell.post_mouse_button(true)
   shell.post_mouse_button(false)
   var after: Dictionary = shell.get_report().living_world
   picked = after.starts != before_starts
   if picked: picked_point=point
   if picked: print("TERRITORY PICK ",region.name," label=",region.label," description=",region.description," map=",region.map," -> ",after.starts)
   if picked: break
  if picked: break
 assert(picked,"A real click on an unclaimed rendered territory must assign a start")
 for frame in 5:
  shell.tick(0.0)
  shell.render(false)
  await process_frame
 assert(not shell.lobby_state().territory_description.is_empty())
 for line in shell.lobby_state().territory_description: assert(not "MISSING:" in line)
 var selected_starts: Array = shell.get_report().living_world.starts
 shell.post_mouse_move_stage(Vector2(-1000,-1000))
 assert(shell.get_report().living_world.hover.is_empty())
 assert(shell.lobby_state().territory_description.is_empty())
 assert(shell.get_report().living_world.starts==selected_starts,"Leaving a territory must preserve the selection")
 shell.post_mouse_move_stage(shell.window_to_stage(picked_point))
 for frame in 4:
  shell.tick(0.0)
  shell.render(false)
  await process_frame
 var view: SubViewport = shell.get_node("AptLivingWorldMap")
 assert(view != null)
 if DisplayServer.get_name() != "headless":
  await RenderingServer.frame_post_draw
  var image := view.get_texture().get_image()
  var after_panel := root.get_texture().get_image().get_region(Rect2i(report.rect))
  assert(before_panel.get_data()!=after_panel.get_data(),"Player selection must visibly change the territory panel")
  var colours := {}
  for y in range(0,image.get_height(),8):
   for x in range(0,image.get_width(),8): colours[image.get_pixel(x,y).to_rgba32()] = true
  print("TERRITORY COLOURS ",colours.size())
  assert(colours.size() > 32) # tolerance: textured map must contain varied imagery, rather than the former blank panel.
  var capture := OS.get_environment("OPENBFME_TERRITORY_SCREENSHOT")
  if not capture.is_empty():
   assert(root.get_texture().get_image().save_png(capture) == OK)
   assert(image.save_png(capture.get_basename()+"-map.png") == OK)
 var saved_starts: Array = shell.get_report().living_world.starts
 assert(shell.lobby_apply({"strategic_scenario":3}).ok)
 for frame in 4:
  shell.tick(0.0)
  shell.render(false)
  await process_frame
 assert(shell.lobby_state().strategic_scenario == 3)
 assert(shell.get_report().living_world.hover.is_empty(),"Changing scenarios must clear hover")
 var updates: int = shell.get_report().living_world.render_updates
 for frame in 8:
  shell.tick(0.0)
  shell.render(false)
  await process_frame
 assert(shell.get_report().living_world.render_updates == updates,"Static map must reuse its rendered texture")
 shell.shell_pop()
 for frame in 60:
  shell.tick(1.0/30.0)
  shell.render(false)
  await process_frame
 assert(not shell.get_report().living_world.visible,"Leaving the lobby must hide the map viewport")
 assert(view.render_target_update_mode == SubViewport.UPDATE_DISABLED)
 shell.free()
 world.free()
 await process_frame
 print("TERRITORY MAP VIEW PASS")
 quit()
