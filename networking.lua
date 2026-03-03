local networking = {}

local objectLib = require "object"
local cubeModel = objectLib.cubeModel

local function sanitizeScale(value, fallback)
    local scale = tonumber(value)
    if scale and scale > 0 then
        return scale
    end
    return fallback
end

local function sanitizeRole(token)
    if token == "walking" or token == "walk" or token == "w" then
        return "walking"
    end
    return "plane"
end

local function sanitizeCallsign(value)
    if type(value) ~= "string" then
        return nil
    end
    local text = value:gsub("[^ -~]", "")
    text = text:gsub("|", "")
    text = text:gsub("^%s+", ""):gsub("%s+$", "")
    if text == "" then
        return nil
    end
    if #text > 20 then
        text = text:sub(1, 20)
    end
    return text
end

function networking.createObjectForPeer(peerID, objects, q, playerModel, playerModelRotationOffset, defaults)
    defaults = defaults or {}
    local startScale = sanitizeScale(defaults.scale, 1.35)
    local planeScale = sanitizeScale(defaults.planeScale, startScale)
    local walkingScale = sanitizeScale(defaults.walkingScale, startScale)
    local role = sanitizeRole(defaults.role or "plane")
    local planeModelHash = defaults.planeModelHash or defaults.modelHash or "builtin-cube"
    local walkingModelHash = defaults.walkingModelHash or defaults.modelHash or planeModelHash
    local activeScale = (role == "walking") and walkingScale or planeScale
    local activeModelHash = (role == "walking") and walkingModelHash or planeModelHash
    local model = playerModel or cubeModel
    local obj = {
        model = model,
        pos = { 0, 0, 0 },
        basePos = { 0, 0, 0 },
        rot = playerModelRotationOffset or q.identity(),
        color = { math.random(), math.random(), math.random() },
        isSolid = true,
        id = peerID,
        scale = { activeScale, activeScale, activeScale },
        halfSize = { x = activeScale, y = activeScale, z = activeScale },
        visualOffsetY = tonumber(defaults.visualOffsetY) or 0,
        modelHash = activeModelHash,
        remoteRole = role,
        planeModelHash = planeModelHash,
        walkingModelHash = walkingModelHash,
        planeModelScale = planeScale,
        walkingModelScale = walkingScale,
        callsign = sanitizeCallsign(defaults.callsign)
    }

    table.insert(objects, obj)
    return obj
end

function networking.handlePacket(data, peers, objects, q, playerModel, playerModelRotationOffset, peerDefaults)
    if type(data) ~= "string" then
        return nil
    end

    local parts = {}
    for p in string.gmatch(data, "([^|]+)") do
        table.insert(parts, p)
    end

    if parts[1] ~= "STATE" then
        return nil
    end

    if #parts < 9 then
        return nil
    end

    local id = tonumber(parts[#parts])
    local px = tonumber(parts[2])
    local py = tonumber(parts[3])
    local pz = tonumber(parts[4])
    local rw = tonumber(parts[5])
    local rx = tonumber(parts[6])
    local ry = tonumber(parts[7])
    local rz = tonumber(parts[8])
    local remoteScale = (#parts >= 10) and tonumber(parts[9]) or nil
    local remoteModelHash = (#parts >= 11) and parts[10] or nil
    local remoteRole = (#parts >= 12) and sanitizeRole(parts[11]) or nil
    local remotePlaneScale = (#parts >= 13) and tonumber(parts[12]) or nil
    local remotePlaneHash = (#parts >= 14) and parts[13] or nil
    local remoteWalkingScale = (#parts >= 15) and tonumber(parts[14]) or nil
    local remoteWalkingHash = (#parts >= 16) and parts[15] or nil
    local remoteCallsign = (#parts >= 17) and sanitizeCallsign(parts[#parts - 1]) or nil

    if not (id and px and py and pz and rw and rx and ry and rz) then
        return nil
    end

    if not peers[id] then
        local defaults = {}
        if type(peerDefaults) == "table" then
            for k, v in pairs(peerDefaults) do
                defaults[k] = v
            end
        end
        if remoteScale and remoteScale > 0 then
            defaults.scale = remoteScale
        end
        if remoteModelHash and remoteModelHash ~= "" then
            defaults.modelHash = remoteModelHash
        end
        if remoteRole then
            defaults.role = remoteRole
        end
        if remotePlaneScale and remotePlaneScale > 0 then
            defaults.planeScale = remotePlaneScale
        end
        if remoteWalkingScale and remoteWalkingScale > 0 then
            defaults.walkingScale = remoteWalkingScale
        end
        if remotePlaneHash and remotePlaneHash ~= "" then
            defaults.planeModelHash = remotePlaneHash
        end
        if remoteWalkingHash and remoteWalkingHash ~= "" then
            defaults.walkingModelHash = remoteWalkingHash
        end
        if remoteCallsign then
            defaults.callsign = remoteCallsign
        end
        peers[id] = networking.createObjectForPeer(id, objects, q, playerModel, playerModelRotationOffset, defaults)
    end

    local obj = peers[id]
    obj.basePos = { px, py, pz }
    obj.pos = { px, py + (obj.visualOffsetY or 0), pz }

    if remoteRole then
        obj.remoteRole = remoteRole
    end

    if remotePlaneScale and remotePlaneScale > 0 then
        obj.planeModelScale = remotePlaneScale
    end
    if remoteWalkingScale and remoteWalkingScale > 0 then
        obj.walkingModelScale = remoteWalkingScale
    end
    if remotePlaneHash and remotePlaneHash ~= "" then
        obj.planeModelHash = remotePlaneHash
    end
    if remoteWalkingHash and remoteWalkingHash ~= "" then
        obj.walkingModelHash = remoteWalkingHash
    end
    if remoteCallsign then
        obj.callsign = remoteCallsign
    end

    if remoteModelHash and remoteModelHash ~= "" then
        if remoteRole == "walking" then
            obj.walkingModelHash = remoteModelHash
        elseif remoteRole == "plane" then
            obj.planeModelHash = remoteModelHash
        else
            obj.planeModelHash = remoteModelHash
            obj.walkingModelHash = remoteModelHash
        end
    end

    if remoteScale and remoteScale > 0 then
        if remoteRole == "walking" then
            obj.walkingModelScale = remoteScale
        elseif remoteRole == "plane" then
            obj.planeModelScale = remoteScale
        else
            obj.planeModelScale = remoteScale
            obj.walkingModelScale = remoteScale
        end
    end

    local activeRole = sanitizeRole(obj.remoteRole or "plane")
    local activeScale
    local activeModelHash
    if activeRole == "walking" then
        activeScale = sanitizeScale(obj.walkingModelScale, sanitizeScale(obj.scale and obj.scale[1], 1.35))
        activeModelHash = obj.walkingModelHash or obj.modelHash or "builtin-cube"
    else
        activeScale = sanitizeScale(obj.planeModelScale, sanitizeScale(obj.scale and obj.scale[1], 1.35))
        activeModelHash = obj.planeModelHash or obj.modelHash or "builtin-cube"
    end

    obj.scale = { activeScale, activeScale, activeScale }
    obj.halfSize = { x = activeScale, y = activeScale, z = activeScale }
    obj.modelHash = activeModelHash

    local baseRot = { w = rw, x = rx, y = ry, z = rz }
    if playerModelRotationOffset then
        obj.rot = q.normalize(q.multiply(baseRot, playerModelRotationOffset))
    else
        obj.rot = baseRot
    end

    return id
end

return networking
