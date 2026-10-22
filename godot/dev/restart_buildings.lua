-- Restart handoff uses the same named JSON layouts and reconstruction path as
-- recordBuildings/replayBuildings. Each occupied start retains its own owner.
function recordRestartBuildings(name)
    local groups = {}
    for id, player in pairs(bfme.players) do
        if type(id) == 'number' and player.startSlot ~= nil and player.startSlot >= 0 and player.name == 'Player_'..(player.startSlot+1) then
            local layout = name .. '-slot-' .. player.startSlot
            recordBuildings(layout, player)
            groups[#groups+1] = {layout=layout, slot=player.startSlot, money=player.money}
        end
    end
    table.sort(groups, function(a,b) return a.slot < b.slot end)
    assert(#groups > 0, 'No occupied starts to record')
    bfme.layouts.save(name, {version=1, groups=groups, ubermode=ubermode and ubermode.enabled == true})
    return #groups
end

function replayRestartBuildings(name)
    local saved = bfme.layouts.load(name)
    assert(saved.version == 1 and type(saved.groups) == 'table', 'Invalid restart building manifest')
    if saved.ubermode then ubermode.enable() end
    local event = 'restart-building-replays'
    local generation = bfme.match.getStatus().generation
    local index, active, owner = 1, false, nil
    restartBuildingReplay = {running=true, failed=0}
    bfme.events.onUpdate(event, function()
        if bfme.match.getStatus().generation ~= generation then
            bfme.events.remove(event); restartBuildingReplay.running=false; return
        end
        if active then
            if buildingReplay.running then return end
            restartBuildingReplay.failed = restartBuildingReplay.failed + buildingReplay.failed
            if saved.groups[index].money ~= nil then owner.money=saved.groups[index].money end
            active=false; index=index+1
        end
        local group=saved.groups[index]
        if not group then
            bfme.events.remove(event); restartBuildingReplay.running=false
            print('Restart buildings restored: '..restartBuildingReplay.failed..' errors')
            return
        end
        owner=nil
        for id, player in pairs(bfme.players) do
            if type(id)=='number' and player.startSlot==group.slot and player.name=='Player_'..(group.slot+1) then owner=player; break end
        end
        local ok, err=pcall(function()
            assert(owner, 'Recorded building owner start is unoccupied: '..group.slot)
            -- Construction pays normally; restore the recorded funds afterwards.
            owner.money=2000000000
            replayBuildings(group.layout,{player=owner})
        end)
        if ok then active=true
        else
            if owner and group.money~=nil then owner.money=group.money end
            restartBuildingReplay.failed=restartBuildingReplay.failed+1
            print('Restart buildings failed: '..tostring(err)); index=index+1
        end
    end, 250)
end
