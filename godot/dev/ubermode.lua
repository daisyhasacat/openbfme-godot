-- Local sandbox cheat; independent of the Uber simulation/performance profile.
-- Console: ubermode.enable(), ubermode.disable(), ubermode.toggle().
ubermode = ubermode or { enabled = false, seen = {}, pending = {} }
local mode = ubermode

local function localMatch()
    local status = bfme.match.getStatus()
    assert(status.ready and not status.networkGame and not status.replay,
        "ubermode requires a local live match")
    return assert(bfme.players.localPlayer, "no local player")
end

function mode.enable()
    local player = localMatch()
    player:setFogOfWar(false)
    if mode.enabled then return end
    mode.previousCP = {base = player.commandPointsBase, cap = player.commandPointsCap,
        bonus = player.commandPointsBonus}
    mode.playerId = player.id
    mode.seen, mode.pending = {}, {}
    mode.enabled = true
    player.money = 1000000
    player.commandPointsBonus = 0
    player.commandPointsBase = 1000000
    player.commandPointsCap = 1000000
    local function oneSecond(context)
        if mode.enabled and context.playerId == mode.playerId then return 1 end
    end
    bfme.timing.onBuild(oneSecond)
    bfme.timing.onTrain(oneSecond)
    bfme.timing.onUpgrade(oneSecond)
    -- Existing units are left as they are; newly trained/created units are enhanced.
    for _, unit in ipairs(player:units()) do mode.seen[unit.id] = true end
    print("ubermode enabled: 1,000,000 gold/CP; fog disabled; one-second production; K+recruit = five per matching building")
end

function mode.disable()
    if not mode.enabled then return end
    local player = bfme.players[mode.playerId]
    if player then player:setFogOfWar(true) end
    if player and mode.previousCP then
        player.commandPointsBase = mode.previousCP.base
        player.commandPointsCap = mode.previousCP.cap
        player.commandPointsBonus = mode.previousCP.bonus
    end
    mode.enabled, mode.pending = false, {}
    bfme.timing.clear()
    print("ubermode disabled; granted gold, levels and upgrades are retained")
end

function mode.toggle()
    if mode.enabled then mode.disable() else mode.enable() end
end

-- Called by the input bridge only for a recruitment button, before its normal click.
function mode.massTrain(unitType, selected)
    if not mode.enabled then return false end
    local player = localMatch()
    selected = selected or bfme.selection.first()
    if not selected or selected.playerId ~= player.id then return false end
    local matched = false
    for _, building in ipairs(player:buildings()) do
        if building.typeId == selected.typeId and not building.underConstruction then
            for _, option in ipairs(building:getTrainableUnits()) do
                if option.unit == unitType then
                    mode.pending[#mode.pending + 1] = {building = building.id, unit = unitType, left = 5}
                    matched = true
                    break
                end
            end
        end
    end
    return matched
end

bfme.events.onUpdate("ubermode", function()
    if not mode.enabled then return end
    -- Most ticks have no new troops or recruitment jobs. A detached ID list
    -- avoids building the full army snapshot merely to discover nothing changed.
    local roster = bfme.match.getUnitRoster(mode.playerId)
    if not roster.ready then mode.enabled, mode.pending = false, {}; return end
    local unseen = false
    for _, id in ipairs(roster.ids) do
        if not mode.seen[id] then unseen = true; break end
    end
    if not unseen and #mode.pending == 0 then return end
    local player = localMatch()
    for _, id in ipairs(roster.ids) do
        local unit = not mode.seen[id] and bfme.unitInstances[id]
        if unit then
            mode.seen[unit.id] = true
            if unit.maxLevel and unit.maxLevel > unit.level then unit.level = unit.maxLevel end
            for _, upgrade in ipairs(unit:getUpgrades()) do
                if upgrade.kind == "object" and upgrade.status ~= "complete" then unit:giveUpgrade(upgrade.upgrade) end
            end
        end
    end
    -- One request per producer per tick; wait for its result before consuming the
    -- count. A full native queue waits instead of silently dropping recruits.
    local busy = {}
    for i = #mode.pending, 1, -1 do
        local job = mode.pending[i]
        local building = bfme.buildingInstances[job.building]
        if not building or building.playerId ~= mode.playerId then
            table.remove(mode.pending, i)
        else
            if job.ticket then
                local status = bfme.mutations.getStatus(job.ticket)
                if status ~= "pending" then
                    if status == "applied" then job.left = job.left - 1
                    else print("ubermode recruitment: " .. tostring(status)) end
                    job.ticket = nil
                end
            end
            if job.left == 0 then table.remove(mode.pending, i)
            elseif not busy[job.building] then
                busy[job.building] = true
                if not job.ticket and building:canTrain(job.unit) then job.ticket = building:trainUnit(job.unit, 1) end
            end
        end
    end
end, 200)

bfme.events.onMatchStarted("ubermode-reset", function()
    mode.enabled, mode.seen, mode.pending, mode.previousCP = false, {}, {}, nil
end)
