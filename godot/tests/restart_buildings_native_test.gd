extends SceneTree
var world:Node
func _initialize():call_deferred('_run')
func lua(code:String)->String:
 var result=world.execute_lua(code);assert(result.ok,str(result));return str(result.output).strip_edges()
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 world=ClassDB.instantiate('GameWorld');root.add_child(world);assert(world.setup(fs).ok)
 var slots=[]
 for i in 8:slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
 for i in 2:slots[i]={'state':6,'name':'Restart test '+str(i),'accepted':true,'color':i,'start_pos':i,'player_template':4,'team':i}
 assert(world.prepare_new_game({'mode':'skirmish','map_crc':0,'map_size':0,'map':'maps/map wor buckland/map wor buckland.map','seed':42,'starting_cash':2000,'slots':slots}).ok)
 assert(world.start_new_game({'logic_thread':false}).ok)
 lua('bfme.console.reload()');world.update_lua()
 lua('ubermode.enable()');world.advance(.2);world.update_lua()
 var name='restart-native-'+str(Time.get_ticks_usec())
 lua('assert(recordRestartBuildings('+JSON.stringify(name)+')==2)')
 lua('ubermode.disable()');world.advance(.2);world.update_lua()
 # Reusing existing starting castles must not create another fortress for either owner.
 var count=world.get_object_count()
 lua('replayRestartBuildings('+JSON.stringify(name)+')')
 var done=false
 for i in 100:
  for frame in 5:world.advance(.2)
  world.update_lua()
  await create_timer(.11).timeout
  if lua('return restartBuildingReplay.running')=='false':done=true;break
 assert(done,'Restart replay must finish')
 world.advance(.2);world.update_lua()
 assert(lua('return restartBuildingReplay.failed')=='0')
 assert(world.get_object_count()==count,'Existing buildings are reused')
 assert(lua('return bfme.players.localPlayer.money')=='1000000')
 assert(lua('return ubermode.enabled')=='true')
 var fog=world.get_shroud_report();assert(fog.fogged==0 and fog.shrouded==0)
 for suffix in ['', '-slot-0','-slot-1']:DirAccess.remove_absolute('user://building_layouts/'+name+suffix+'.json')
 world.free();print('RESTART BUILDINGS NATIVE PASS');quit(0)
