-- Local sandbox cheat; independent of the Uber simulation/performance profile.
-- Console: ubermode.enable(), ubermode.disable(), ubermode.toggle().
ubermode = ubermode or { enabled = false, seen = {}, pending = {} }
local mode = ubermode
mode.seenBuildings = mode.seenBuildings or {}
mode.newUnits, mode.newBuildings = mode.newUnits or {}, mode.newBuildings or {}
-- Hordes already given their upgrades: a produced horde's members join one by one after it, and only the
-- members it holds when it is given an upgrade receive it, so each later member has the horde regranted.
mode.upgradedHordes = mode.upgradedHordes or {}
local update, watchCompletions
local scheduled = false
local function schedule()
    if not scheduled then bfme.events.onUpdate("ubermode", update, 250); scheduled = true end
end

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
    mode.seen, mode.pending, mode.seenBuildings = {}, {}, {}
    mode.newUnits, mode.newBuildings, mode.upgradedHordes = {}, {}, {}
    mode.enabled = true
    watchCompletions()
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
    for _, building in ipairs(player:buildings()) do
        if not building.underConstruction then mode.seenBuildings[building.id] = true end
    end
    print("ubermode enabled: 1,000,000 gold/CP; fog disabled; Gollum revealed; one-second production; completed buildings reach max level; K+recruit = five per matching building")
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
    mode.newUnits, mode.newBuildings = {}, {}
    bfme.events.remove("ubermode"); scheduled = false
    bfme.events.remove("ubermode-units")
    bfme.events.remove("ubermode-buildings")
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
    if matched then schedule() end
    return matched
end

update = function()
    if not mode.enabled then return end
    local player = localMatch()
    -- Preserve a burst's unprocessed IDs if the native mutation queue fills.
    local function enhance(pending, collection, equipment)
        for id in pairs(pending) do
            local ok, err = pcall(function()
                local object = collection[id]
                if not object or object.playerId ~= mode.playerId then return end
                if object.underConstruction then return end
                if object.maxLevel and object.maxLevel > object.level then object.level = object.maxLevel end
                if equipment then
                    -- A horde reports "complete" once any member has an upgrade, so a regrant gives
                    -- every object upgrade again: the horde passes them all to its current members.
                    local granted = false
                    for _, upgrade in ipairs(object:getUpgrades()) do
                        if upgrade.kind == "object" and (upgrade.status ~= "complete" or mode.upgradedHordes[id]) then
                            object:giveUpgrade(upgrade.upgrade); granted = true
                        end
                    end
                    if granted then mode.upgradedHordes[id] = true end
                end
            end)
            if ok then pending[id] = nil
            elseif mode.enhanceError ~= tostring(err) then
                mode.enhanceError = tostring(err); print("ubermode enhancement waiting: " .. tostring(err))
            end
        end
    end
    enhance(mode.newUnits, bfme.unitInstances, true)
    enhance(mode.newBuildings, bfme.buildingInstances, false)
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
    if #mode.pending == 0 and not next(mode.newUnits) and not next(mode.newBuildings) then
        bfme.events.remove("ubermode"); scheduled = false
    else
        schedule()
    end
end

watchCompletions = function()
    bfme.events.onUnitsCreated("ubermode-units", function(event)
        if not mode.enabled then return end
        for _, record in ipairs(event.objects) do
            if record.playerId == mode.playerId and not mode.seen[record.objectId] then
                mode.newUnits[record.objectId] = true
                mode.seen[record.objectId] = true
                local unit = bfme.unitInstances[record.objectId]
                if unit and unit.hordeId and mode.upgradedHordes[unit.hordeId] then mode.newUnits[unit.hordeId] = true end
            end
        end
        if next(mode.newUnits) then update() end
    end)
    bfme.events.onBuildingsCompleted("ubermode-buildings", function(event)
        if not mode.enabled then return end
        for _, record in ipairs(event.objects) do
            if record.playerId == mode.playerId and not mode.seenBuildings[record.objectId] then
                mode.newBuildings[record.objectId] = true
                mode.seenBuildings[record.objectId] = true
            end
        end
        if next(mode.newBuildings) then update() end
    end)
end

-- Disabled helpers must not stop the simulation worker just to return immediately.
bfme.events.remove("ubermode")
bfme.events.remove("ubermode-units")
bfme.events.remove("ubermode-buildings")
if mode.enabled then
    watchCompletions()
    if #mode.pending > 0 or next(mode.newUnits) or next(mode.newBuildings) then schedule() end
end

bfme.events.onMatchStarted("ubermode-reset", function()
    mode.enabled, mode.seen, mode.pending, mode.previousCP, mode.seenBuildings = false, {}, {}, nil, {}
    mode.newUnits, mode.newBuildings, mode.upgradedHordes = {}, {}, {}
    bfme.events.remove("ubermode"); scheduled = false
    bfme.events.remove("ubermode-units")
    bfme.events.remove("ubermode-buildings")
end)
