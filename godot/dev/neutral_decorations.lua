-- Allow building over neutral scenery. Merely previewing never destroys anything.
neutralDecorations = neutralDecorations or {enabled=true}
local function applyToMatch()
    local match = bfme.match.getStatus()
    if match.ready and not match.networkGame and not match.replay then
        bfme.match.setClearNeutralDecorations(neutralDecorations.enabled)
    end
end
function neutralDecorations.enable(enabled)
    assert(type(enabled)=='boolean', 'Expected true or false')
    neutralDecorations.enabled = enabled
    applyToMatch()
end
-- Remove the old timer when hot-reloading from an earlier version.
bfme.events.remove('neutral-decoration-placement')
bfme.events.onMatchStarted('neutral-decoration-placement', applyToMatch)
applyToMatch() -- hot reload during an existing match
