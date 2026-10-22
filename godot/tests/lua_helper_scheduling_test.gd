extends SceneTree

func _initialize() -> void:
	var lua = ClassDB.instantiate("LuaState")
	lua.open_libraries()
	var chunks := ["""
updates, events = {}, {}
local player = {id=1, faction='Men', money=1000, commandPointsBase=100, commandPointsCap=100, commandPointsBonus=0}
function player:units() return {} end
function player:buildings() return {} end
function player:setFogOfWar() end
bfme = {players={[1]=player,localPlayer=player},
 match={getStatus=function() return {ready=true,generation=1} end,getUnitRoster=function() return {ready=true,ids={}} end,getCompletedBuildingRoster=function() return {ready=true,ids={}} end},
 timing={onBuild=function()end,onTrain=function()end,onUpgrade=function()end,clear=function()end},
 events={onUpdate=function(name,fn) updates[name]=fn end,remove=function(name) updates[name]=nil; events[name]=nil end,
 onUnitsCreated=function(name,fn) events[name]=fn end,onBuildingsCompleted=function(name,fn) events[name]=fn end,
 onMatchStarted=function(name,fn) events[name]=fn end}}
""", FileAccess.get_file_as_string("res://dev/ubermode.lua"), """
assert(next(updates)==nil, 'disabled helpers must not schedule worker stops')
ubermode.enable(); assert(events['ubermode-units'] and events['ubermode-buildings']); assert(next(updates)==nil)
ubermode.disable(); assert(updates.ubermode==nil)
ubermode.enable(); events['ubermode-reset'](); assert(not ubermode.enabled and updates.ubermode==nil)

"""]
	for chunk in chunks:
		var result = lua.do_string(chunk)
		if result is Object and result.get_class() == "LuaError":
			push_error(str(result))
			quit(1)
			return
	print("LUA HELPER SCHEDULING PASS")
	quit(0)
