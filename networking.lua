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

local function sanitizeAngle(value, fallback)
    local num = tonumber(value)
    if num ~= nil then
        return num
    end
    return tonumber(fallback) or 0
end

local function sanitizeOrientationTable(value, fallback)
    local source = (type(value) == "table") and value or fallback or {}
    return {
        yaw = sanitizeAngle(source.yaw, 0),
        pitch = sanitizeAngle(source.pitch, 0),
        roll = sanitizeAngle(source.roll, 0)
    }
end

local function buildOrientationOffsetQuat(q, baseOffset, orientation)
    local orient = sanitizeOrientationTable(orientation, { yaw = 0, pitch = 0, roll = 0 })
    local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, math.rad(orient.yaw))
    local pitchQuat = q.fromAxisAngle({ 1, 0, 0 }, math.rad(orient.pitch))
    local rollQuat = q.fromAxisAngle({ 0, 0, 1 }, math.rad(orient.roll))
    local userOffset = q.normalize(q.multiply(q.multiply(yawQuat, pitchQuat), rollQuat))
    if baseOffset then
        return q.normalize(q.multiply(baseOffset, userOffset))
    end
    return userOffset
end

local function resolveBaseOffset(baseOffsetOrResolver, role, modelHash)
    if type(baseOffsetOrResolver) == "function" then
        local ok, resolved = pcall(baseOffsetOrResolver, role, modelHash)
        if ok and type(resolved) == "table" then
            return resolved
        end
        return nil
    end
    if type(baseOffsetOrResolver) == "table" then
        return baseOffsetOrResolver
    end
    return nil
end

function networking.createObjectForPeer(peerID, objects, q, playerModel, playerModelRotationOffset, defaults)
    defaults = defaults or {}
    local startScale = sanitizeScale(defaults.scale, 1.35)
    local planeScale = sanitizeScale(defaults.planeScale, startScale)
    local walkingScale = sanitizeScale(defaults.walkingScale, startScale)
    local role = sanitizeRole(defaults.role or "plane")
    local planeModelHash = defaults.planeModelHash or defaults.modelHash or "builtin-cube"
    local walkingModelHash = defaults.walkingModelHash or defaults.modelHash or planeModelHash
    local planeSkinHash = defaults.planeSkinHash or ""
    local walkingSkinHash = defaults.walkingSkinHash or ""
    local planeOrientation = sanitizeOrientationTable(defaults.planeOrientation, { yaw = 0, pitch = 0, roll = 0 })
    local walkingOrientation = sanitizeOrientationTable(defaults.walkingOrientation, { yaw = 0, pitch = 0, roll = 0 })
    local activeScale = (role == "walking") and walkingScale or planeScale
    local activeModelHash = (role == "walking") and walkingModelHash or planeModelHash
    local activeOrientation = (role == "walking") and walkingOrientation or planeOrientation
    local baseOffset = resolveBaseOffset(playerModelRotationOffset, role, activeModelHash)
    local model = playerModel or cubeModel
    local obj = {
        model = model,
        pos = { 0, 0, 0 },
        basePos = { 0, 0, 0 },
        rot = buildOrientationOffsetQuat(q, baseOffset, activeOrientation),
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
        planeSkinHash = planeSkinHash,
        walkingSkinHash = walkingSkinHash,
        planeModelScale = planeScale,
        walkingModelScale = walkingScale,
        planeOrientation = planeOrientation,
        walkingOrientation = walkingOrientation,
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

    if parts[1] == "STATE2" then
        local kv = {}
        for i = 2, #parts do
            local key, value = parts[i]:match("^([^=]+)=(.*)$")
            if key and value then
                kv[key] = value
            end
        end
        local packetId = kv.id
        if not packetId then
            local trailingId = tonumber(parts[#parts])
            if trailingId then
                packetId = tostring(math.floor(trailingId))
            end
        end
        if not packetId then
            return nil
        end
        parts = {
            "STATE",
            kv.px or kv.x or "0",
            kv.py or kv.y or "0",
            kv.pz or kv.z or "0",
            kv.rw or "1",
            kv.rx or "0",
            kv.ry or "0",
            kv.rz or "0",
            kv.scale or "",
            kv.modelHash or "",
            kv.role or "",
            kv.planeScale or "",
            kv.planeModelHash or "",
            kv.planeYaw or "",
            kv.planePitch or "",
            kv.planeRoll or "",
            kv.walkingScale or "",
            kv.walkingModelHash or "",
            kv.walkingYaw or "",
            kv.walkingPitch or "",
            kv.walkingRoll or "",
            kv.planeSkinHash or "",
            kv.walkingSkinHash or "",
            kv.callsign or "",
            packetId
        }
    elseif parts[1] ~= "STATE" then
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
    local hasExtendedOrientation = #parts >= 23
    local remotePlaneYaw = hasExtendedOrientation and tonumber(parts[14]) or nil
    local remotePlanePitch = hasExtendedOrientation and tonumber(parts[15]) or nil
    local remotePlaneRoll = hasExtendedOrientation and tonumber(parts[16]) or nil
    local remoteWalkingScale = hasExtendedOrientation and tonumber(parts[17]) or ((#parts >= 15) and tonumber(parts[14]) or nil)
    local remoteWalkingHash = hasExtendedOrientation and parts[18] or ((#parts >= 16) and parts[15] or nil)
    local remoteWalkingYaw = hasExtendedOrientation and tonumber(parts[19]) or nil
    local remoteWalkingPitch = hasExtendedOrientation and tonumber(parts[20]) or nil
    local remoteWalkingRoll = hasExtendedOrientation and tonumber(parts[21]) or nil
    local hasSkinHashes = #parts >= 25
    local remotePlaneSkinHash = hasSkinHashes and parts[22] or nil
    local remoteWalkingSkinHash = hasSkinHashes and parts[23] or nil
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
        if remotePlaneSkinHash ~= nil then
            defaults.planeSkinHash = remotePlaneSkinHash
        end
        if remoteWalkingSkinHash ~= nil then
            defaults.walkingSkinHash = remoteWalkingSkinHash
        end
        if remotePlaneYaw ~= nil or remotePlanePitch ~= nil or remotePlaneRoll ~= nil then
            defaults.planeOrientation = {
                yaw = sanitizeAngle(remotePlaneYaw, 0),
                pitch = sanitizeAngle(remotePlanePitch, 0),
                roll = sanitizeAngle(remotePlaneRoll, 0)
            }
        end
        if remoteWalkingYaw ~= nil or remoteWalkingPitch ~= nil or remoteWalkingRoll ~= nil then
            defaults.walkingOrientation = {
                yaw = sanitizeAngle(remoteWalkingYaw, 0),
                pitch = sanitizeAngle(remoteWalkingPitch, 0),
                roll = sanitizeAngle(remoteWalkingRoll, 0)
            }
        end
        peers[id] = networking.createObjectForPeer(id, objects, q, playerModel, playerModelRotationOffset, defaults)
    end

    local obj = peers[id]
    obj.planeOrientation = sanitizeOrientationTable(obj.planeOrientation, { yaw = 0, pitch = 0, roll = 0 })
    obj.walkingOrientation = sanitizeOrientationTable(obj.walkingOrientation, { yaw = 0, pitch = 0, roll = 0 })
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
    if remotePlaneSkinHash ~= nil then
        obj.planeSkinHash = remotePlaneSkinHash
    end
    if remoteWalkingSkinHash ~= nil then
        obj.walkingSkinHash = remoteWalkingSkinHash
    end
    if remotePlaneYaw ~= nil then
        obj.planeOrientation.yaw = sanitizeAngle(remotePlaneYaw, obj.planeOrientation.yaw)
    end
    if remotePlanePitch ~= nil then
        obj.planeOrientation.pitch = sanitizeAngle(remotePlanePitch, obj.planeOrientation.pitch)
    end
    if remotePlaneRoll ~= nil then
        obj.planeOrientation.roll = sanitizeAngle(remotePlaneRoll, obj.planeOrientation.roll)
    end
    if remoteWalkingYaw ~= nil then
        obj.walkingOrientation.yaw = sanitizeAngle(remoteWalkingYaw, obj.walkingOrientation.yaw)
    end
    if remoteWalkingPitch ~= nil then
        obj.walkingOrientation.pitch = sanitizeAngle(remoteWalkingPitch, obj.walkingOrientation.pitch)
    end
    if remoteWalkingRoll ~= nil then
        obj.walkingOrientation.roll = sanitizeAngle(remoteWalkingRoll, obj.walkingOrientation.roll)
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
    local activeOrientation = (activeRole == "walking") and obj.walkingOrientation or obj.planeOrientation
    local baseOffset = resolveBaseOffset(playerModelRotationOffset, activeRole, activeModelHash)
    local modelOffset = buildOrientationOffsetQuat(q, baseOffset, activeOrientation)
    obj.rot = q.normalize(q.multiply(baseRot, modelOffset))

    return id
end

return networking
