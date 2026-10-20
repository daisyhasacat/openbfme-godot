extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var lua=ClassDB.instantiate('LuaState');lua.open_libraries()
 var chunks=["""
local saved, callbacks, records, replayed = {}, {}, {}, {}
local generation=1
local function player(id,slot) return {id=id,startSlot=slot,name='Player_'..(slot+1),money=400+id} end
bfme={players={[1]=player(1,0),[2]=player(2,3),[3]=player(3,-1)},
 layouts={save=function(k,v)saved[k]=v end,load=function(k)return assert(saved[k]) end},
 match={getStatus=function()return {generation=generation}end},
 events={onUpdate=function(k,f)callbacks[k]=f end,remove=function(k)callbacks[k]=nil end}}
local enabled=0
ubermode={enabled=true,enable=function()enabled=enabled+1 end}
buildingReplay={running=false,failed=0}
function recordBuildings(name,p) records[#records+1]={name,p.startSlot};saved[name]={{template='Farm',x=1,y=2}} end
function replayBuildings(name,options)
 assert(not buildingReplay.running);assert(saved[name]);assert(options.player.money==2000000000)
 replayed[#replayed+1]=options.player.startSlot;buildingReplay={running=true,failed=0}
end
function runTest()
 assert(recordRestartBuildings('restart')==2)
 assert(#records==2 and #saved.restart.groups==2 and saved.restart.ubermode)
 bfme.players[4]={id=4,startSlot=0,name='PlyrCivilian'}
 -- Ownership resolves by start position after player IDs change.
 bfme.players[1]=nil;bfme.players[9]=player(9,0)
 replayRestartBuildings('restart');assert(enabled==1)
 local tick=callbacks['restart-building-replays'];tick()
 assert(replayed[1]==0);tick();assert(#replayed==1)
 buildingReplay.running=false;tick()
 assert(bfme.players[9].money==401 and replayed[2]==3)
 buildingReplay.running=false;buildingReplay.failed=2;tick()
 assert(not restartBuildingReplay.running and restartBuildingReplay.failed==2)
 assert(bfme.players[2].money==402 and callbacks['restart-building-replays']==nil)
 replayRestartBuildings('restart');generation=2;callbacks['restart-building-replays']()
 assert(not restartBuildingReplay.running and #replayed==2)
end
""",FileAccess.get_file_as_string('res://dev/restart_buildings.lua'),'runTest()']
 for chunk in chunks:
  var result=lua.do_string(chunk)
  if result is Object and result.get_class()=='LuaError':push_error(str(result));quit(1);return
 print('RESTART BUILDINGS PASS');quit(0)
