# Installed-data regression: two MOTW fortresses with facing built-in wall hub expansions.
extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 var w=ClassDB.instantiate('GameWorld');root.add_child(w);assert(w.setup(fs).ok)
 var slots=[]
 for i in 8:slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
 slots[0]={'state':6,'name':'Wall probe','accepted':true,'color':0,'start_pos':0,'player_template':4,'team':0}
 assert(w.prepare_new_game({'mode':'skirmish','map':'maps/map wor buckland/map wor buckland.map','map_crc':0,'map_size':0,'seed':42,'starting_cash':100000,'slots':slots}).ok)
 assert(w.start_new_game({'logic_thread':false,'wall_rebalance':true}).ok)
 var p=w.execute_lua('return bfme.players.localPlayer.id');assert(p.ok)
 var owner=int(p.output)
 assert(w.execute_lua("local p=bfme.players.localPlayer; p.money=100000; first=p:construct('MenFortress',{x=800,y=800},100); second=p:construct('MenFortress',{x=1200,y=800},100)").ok)
 w.update_lua()
 for i in 200:
  w.advance(.2)
  await create_timer(.03).timeout
 w.update_lua()
 assert(w.execute_lua("for _,b in ipairs(bfme.players.localPlayer:buildings()) do if b.typeId=='MenFortressExpansionPadCorner' and b.y==736 and (b.x==864 or b.x==1136) then bfme.players.localPlayer:construct('MenWallHubSmallExpansion',{x=b.x,y=b.y,plotId=b.id},100) end end").ok)
 w.update_lua()
 for i in 200:
  w.advance(.2)
  await create_timer(.03).timeout
 w.update_lua()
 var workerA=w.create_object('MenPorter',owner,864,736,0)
 var workerB=w.create_object('MenPorter',owner,1136,736,0)
 w.update_lua()
 assert(w.execute_lua("for _,b in ipairs(bfme.players.localPlayer:buildings()) do if b.typeId=='MenWallHubSmallExpansion' then local id=b.x==864 and "+str(workerA)+" or "+str(workerB)+"; bfme.unitInstances[id]:resumeConstruction(b) end end").ok)
 w.update_lua()
 for i in 150:
  w.advance(.2)
  await create_timer(.02).timeout
 w.update_lua()
 var source={};var target={}
 for id in w.get_object_ids():
  var o=w.get_object(id)
  if o.get('template','')=='MenWallHubSmallExpansion':
   if o.x==864:source=o
   if o.x==1136:target=o
 assert(not source.is_empty() and not target.is_empty())
 var camera=Camera3D.new();root.add_child(camera)
 var hud=ClassDB.instantiate('InGameHudNode');root.add_child(hud)
 assert(hud.setup(fs,w,camera,'Player_1',{'camera_start':Vector3(1000,0,-736),'alternate_mouse':true}).ok)
 w.set_shroud_drawn(false);hud.set_controller_layout(true);hud.select_controller_group(PackedInt64Array([source.id]))
 w.advance(.2)
 for i in 8:await process_frame
 var command={}
 for c in hud.get_command_buttons():
  if c.get('template','')=='MenWallHubSmall':command=c
 assert(not command.is_empty(),str(hud.get_command_buttons()))
 assert(hud.activate_command(command.slot,command.in_palantir,false))
 hud.inject_mouse_move(hud.world_to_pixel(Vector2(target.x,target.y)))
 for i in 12:await process_frame
 var preview=hud.get_placement_ghost_state()
 assert(not preview.icons.is_empty(),str(preview))
 for icon in preview.icons:assert(icon.code==0,str(icon))
 assert(w.build2_wall_span(source.id,'MenWallHubSmall',source.x,source.y,target.x,target.y,0))
 w.advance(.2)
 var built=0
 for id in w.get_object_ids():
  var o=w.get_object(id)
  if o.get('template','')=='MenWallSegmentSmall' and o.y==736 and o.x>864 and o.x<1136:built+=1
 assert(built==preview.icons.size(),'Every legal segment was created')
 assert(w.get_object(source.id).ok and w.get_object(target.id).ok)
 print('MOTW FORTRESS WALL JOIN PASS: ',built,' segments')
 hud.free();camera.free();w.free();quit()
