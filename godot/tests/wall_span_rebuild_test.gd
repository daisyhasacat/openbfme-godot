extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 var world=ClassDB.instantiate('GameWorld');root.add_child(world);assert(world.setup(fs).ok)
 assert(world.load_map('map mp fall back 4p',{'slots':[{'player':'Player_1','faction':'FactionMen','human':true}]}).ok)
 var player=world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
 assert(player.ok)
 var source=world.create_object('MenWallHubSmall',int(player.output),1800,1800,0)
 assert(world.build2_wall_span(source,'MenWallHubSmall',1800,1800,2200,1800,0))
 for i in 180:world.advance(.2)
 var pieces=[];var end={}
 for id in world.get_object_ids():
  var o=world.get_object(id)
  if not o.ok or absf(float(o.y)-1800)>1 or float(o.x)<=1800:continue
  if 'wallsegment' in str(o.template).to_lower():pieces.append(o)
  elif o.template=='MenWallHubSmall':end=o
 # tolerance: The fixture needs interior pieces between endpoints to create a repairable gap.
 assert(pieces.size()>3 and not end.is_empty(),'Original wall built')
 var gap=pieces[pieces.size()/2]
 assert(world.build2_damage(gap.id,1000000))
 for i in 2:world.advance(.2)
 var survivors=[]
 for o in pieces:
  if o.id!=gap.id:survivors.append(o.id)
 # Rebuild from the opposite hub through the regular wall-span command.
 assert(world.build2_wall_span(end.id,'MenWallHubSmall',end.x,end.y,1800,1800,0))
 for i in 180:world.advance(.2)
 var replacements=[]
 for id in world.get_object_ids():
  var o=world.get_object(id)
  if o.ok and absf(float(o.x)-float(gap.x))<1 and absf(float(o.y)-float(gap.y))<1:
   # tolerance: Every remaining piece must be alive; exact health depends on construction progress.
   assert(float(o.get('health',0))>0,'No dead rubble remains in the repaired gap')
   replacements.append(o)
 assert(replacements.size()==1 and 'wallsegment' in str(replacements[0].template).to_lower(),'Exactly one replacement at the original slot')
 for id in survivors:assert(world.get_object(id).ok,'Surviving wall retained')
 assert(world.get_object(source).ok and world.get_object(end.id).ok,'Both hubs retained')
 world.free();print('WALL SPAN REBUILD PASS');quit()
