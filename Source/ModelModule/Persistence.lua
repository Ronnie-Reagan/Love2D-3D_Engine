local persistence = {}

function persistence.buildModelSnapshot(asset, role, scale)
    if type(asset) ~= "table" then
        return nil
    end
    local snapshot = {
        role = role,
        hash = asset.modelHash,
        scale = tonumber(scale) or 1,
        sourceFormat = asset.sourceFormat
    }
    if type(asset.sourceLabel) == "string" and asset.sourceLabel ~= "" then
        snapshot.source = asset.sourceLabel
    end
    if type(asset.encodedRaw) == "string" then
        snapshot.encoded = asset.encodedRaw
    end
    local orient = asset.orientationByRole and asset.orientationByRole[role]
    if orient then
        snapshot.orientation = {
            yaw = tonumber(orient.yaw) or 0,
            pitch = tonumber(orient.pitch) or 0,
            roll = tonumber(orient.roll) or 0
        }
    end
    return snapshot
end

function persistence.applyOrientationSnapshot(asset, role, snapshot)
    if type(asset) ~= "table" or type(role) ~= "string" or type(snapshot) ~= "table" then
        return false
    end
    local orient = snapshot.orientation
    if type(orient) ~= "table" then
        return true
    end
    asset.orientationByRole = asset.orientationByRole or {}
    asset.orientationByRole[role] = {
        yaw = tonumber(orient.yaw) or 0,
        pitch = tonumber(orient.pitch) or 0,
        roll = tonumber(orient.roll) or 0
    }
    return true
end

return persistence
