local networking = {}

local objectLib = require "Source.Core.ObjectDefs"
local packetCodec = require "Source.Net.PacketCodec"
local quat = require "Source.Math.Quat"
local cubeModel = objectLib.cubeModel

local MIN_MODEL_SCALE = 0.1
local DEFAULT_INTERPOLATION_DELAY = 0.120
local DEFAULT_EXTRAPOLATION_CAP = 0.200
local SNAPSHOT_BUFFER_LIMIT = 48

local function sanitizeScale(value, fallback)
    local scale = tonumber(value)
    if scale then
        scale = math.abs(scale)
    end
    if scale and scale >= MIN_MODEL_SCALE then
        return scale
    end

    local fallbackScale = tonumber(fallback)
    if fallbackScale then
        fallbackScale = math.abs(fallbackScale)
    end
    if fallbackScale and fallbackScale >= MIN_MODEL_SCALE then
        return fallbackScale
    end

    return 1.35
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

local function nonEmptyString(value)
    if type(value) ~= "string" or value == "" then
        return nil
    end
    return value
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
    q = q or quat
    if type(q.fromAxisAngle) ~= "function" or type(q.multiply) ~= "function" or type(q.normalize) ~= "function" then
        q = quat
    end
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

local function quatIdentity(qOps)
    if type(qOps) == "table" and type(qOps.identity) == "function" then
        return qOps.identity()
    end
    return quat.identity()
end

local function makeHalfSize(scale)
    local safeScale = sanitizeScale(scale, 1.35)
    return { x = safeScale, y = safeScale, z = safeScale }
end

local function copyVec3(v, fallback)
    local source = (type(v) == "table") and v or fallback or { 0, 0, 0 }
    return {
        tonumber(source[1]) or 0,
        tonumber(source[2]) or 0,
        tonumber(source[3]) or 0
    }
end

local function vec3Add(a, b)
    return {
        (a[1] or 0) + (b[1] or 0),
        (a[2] or 0) + (b[2] or 0),
        (a[3] or 0) + (b[3] or 0)
    }
end

local function vec3Scale(v, s)
    return {
        (v[1] or 0) * s,
        (v[2] or 0) * s,
        (v[3] or 0) * s
    }
end

local function vec3Lerp(a, b, t)
    return {
        (a[1] or 0) + ((b[1] or 0) - (a[1] or 0)) * t,
        (a[2] or 0) + ((b[2] or 0) - (a[2] or 0)) * t,
        (a[3] or 0) + ((b[3] or 0) - (a[3] or 0)) * t
    }
end

local function dotQuat(a, b)
    return (a.w or 0) * (b.w or 0) +
        (a.x or 0) * (b.x or 0) +
        (a.y or 0) * (b.y or 0) +
        (a.z or 0) * (b.z or 0)
end

local function quatNlerp(q1, q2, t, qOps)
    qOps = qOps or quat
    local a = q1 or qOps.identity()
    local b = q2 or qOps.identity()
    if dotQuat(a, b) < 0 then
        b = {
            w = -(b.w or 0),
            x = -(b.x or 0),
            y = -(b.y or 0),
            z = -(b.z or 0)
        }
    end
    local blend = {
        w = (a.w or 0) + ((b.w or 0) - (a.w or 0)) * t,
        x = (a.x or 0) + ((b.x or 0) - (a.x or 0)) * t,
        y = (a.y or 0) + ((b.y or 0) - (a.y or 0)) * t,
        z = (a.z or 0) + ((b.z or 0) - (a.z or 0)) * t
    }
    return qOps.normalize(blend)
end

local function integrateQuaternion(rot, angVel, dt, qOps)
    qOps = qOps or quat
    local base = rot or qOps.identity()
    local omega = {
        w = 0,
        x = tonumber(angVel and angVel[1]) or 0,
        y = tonumber(angVel and angVel[2]) or 0,
        z = tonumber(angVel and angVel[3]) or 0
    }
    local qDot = qOps.multiply(base, omega)
    local nextRot = {
        w = (base.w or 1) + 0.5 * (qDot.w or 0) * dt,
        x = (base.x or 0) + 0.5 * (qDot.x or 0) * dt,
        y = (base.y or 0) + 0.5 * (qDot.y or 0) * dt,
        z = (base.z or 0) + 0.5 * (qDot.z or 0) * dt
    }
    return qOps.normalize(nextRot)
end

local function hermiteVec3(p0, v0, p1, v1, dt, t)
    local t2 = t * t
    local t3 = t2 * t
    local h00 = 2 * t3 - 3 * t2 + 1
    local h10 = t3 - 2 * t2 + t
    local h01 = -2 * t3 + 3 * t2
    local h11 = t3 - t2

    local out = { 0, 0, 0 }
    for i = 1, 3 do
        local p0i = p0[i] or 0
        local p1i = p1[i] or 0
        local v0i = v0[i] or 0
        local v1i = v1[i] or 0
        out[i] = h00 * p0i + h10 * dt * v0i + h01 * p1i + h11 * dt * v1i
    end
    return out
end

local function clamp(value, minValue, maxValue)
    if value < minValue then
        return minValue
    end
    if value > maxValue then
        return maxValue
    end
    return value
end

local function appendSnapshot(obj, snapshot)
    obj.stateBuffer = obj.stateBuffer or {}
    local buffer = obj.stateBuffer

    if #buffer == 0 then
        buffer[1] = snapshot
        return
    end

    local last = buffer[#buffer]
    if snapshot.t >= last.t then
        if math.abs(snapshot.t - last.t) <= 1e-6 then
            buffer[#buffer] = snapshot
        else
            buffer[#buffer + 1] = snapshot
        end
    else
        local inserted = false
        for i = #buffer, 1, -1 do
            if snapshot.t >= buffer[i].t then
                table.insert(buffer, i + 1, snapshot)
                inserted = true
                break
            end
        end
        if not inserted then
            table.insert(buffer, 1, snapshot)
        end
    end

    while #buffer > SNAPSHOT_BUFFER_LIMIT do
        table.remove(buffer, 1)
    end
end

local function ensurePeerObject(id, peers, objects, qOps, playerModel, playerModelRotationOffset, defaults)
    if peers[id] then
        return peers[id]
    end
    peers[id] = networking.createObjectForPeer(id, objects, qOps, playerModel, playerModelRotationOffset, defaults)
    return peers[id]
end

function networking.createObjectForPeer(peerID, objects, qOps, playerModel, playerModelRotationOffset, defaults)
    qOps = qOps or quat
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
    local modelOffset = buildOrientationOffsetQuat(qOps, baseOffset, activeOrientation)
    local model = playerModel or cubeModel
    local obj = {
        model = model,
        pos = { 0, 0, 0 },
        basePos = { 0, 0, 0 },
        baseRot = quatIdentity(qOps),
        modelOffset = modelOffset,
        rot = modelOffset,
        color = { math.random(), math.random(), math.random() },
        isSolid = true,
        id = peerID,
        scale = { activeScale, activeScale, activeScale },
        halfSize = makeHalfSize(activeScale),
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
        callsign = sanitizeCallsign(defaults.callsign),
        netVel = { 0, 0, 0 },
        netAngVel = { 0, 0, 0 },
        netControls = { throttle = 0, elevator = 0, aileron = 0, rudder = 0 },
        netTick = 0,
        netTs = 0,
        netReceivedAt = 0,
        stateBuffer = {}
    }

    table.insert(objects, obj)
    return obj
end

local function parseStatePacket(data)
    local parts = packetCodec.splitFields(data)
    local packetType = parts[1]
    if packetType == "STATE2" or packetType == "STATE3" then
        local kv = packetCodec.parseKeyValueFields(parts, 2)
        local packetId = tonumber(kv.id)
        if packetId then
            packetId = math.floor(packetId)
        else
            packetId = select(1, packetCodec.readTrailingInteger(parts, 2))
        end
        if not packetId then
            return nil
        end

        local normalizedParts = {
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
            tostring(packetId)
        }

        local dynamic = {
            velocity = {
                tonumber(kv.vx) or 0,
                tonumber(kv.vy) or 0,
                tonumber(kv.vz) or 0
            },
            angularVelocity = {
                tonumber(kv.wx) or 0,
                tonumber(kv.wy) or 0,
                tonumber(kv.wz) or 0
            },
            controls = {
                throttle = tonumber(kv.thr) or 0,
                elevator = tonumber(kv.elev) or 0,
                aileron = tonumber(kv.ail) or 0,
                rudder = tonumber(kv.rud) or 0
            },
            tick = math.floor(tonumber(kv.tick) or 0),
            ts = tonumber(kv.ts)
        }
        return normalizedParts, dynamic
    end

    if packetType == "STATE" then
        return parts, nil
    end

    return nil
end

function networking.handlePacket(data, peers, objects, qOps, playerModel, playerModelRotationOffset, peerDefaults, receiveTime)
    if type(data) ~= "string" then
        return nil
    end

    qOps = qOps or quat
    local parts, dynamic = parseStatePacket(data)
    if not parts then
        return nil
    end

    local id, idFieldIndex = packetCodec.readTrailingInteger(parts, 2)
    if not id or not idFieldIndex or idFieldIndex < 9 then
        return nil
    end

    local px = tonumber(parts[2])
    local py = tonumber(parts[3])
    local pz = tonumber(parts[4])
    local rw = tonumber(parts[5])
    local rx = tonumber(parts[6])
    local ry = tonumber(parts[7])
    local rz = tonumber(parts[8])
    if not (px and py and pz and rw and rx and ry and rz) then
        return nil
    end

    local payloadFieldCount = idFieldIndex - 1
    local remoteScale = (payloadFieldCount >= 9) and tonumber(nonEmptyString(parts[9])) or nil
    local remoteModelHash = (payloadFieldCount >= 10) and nonEmptyString(parts[10]) or nil
    local roleToken = (payloadFieldCount >= 11) and nonEmptyString(parts[11]) or nil
    local remoteRole = roleToken and sanitizeRole(roleToken) or nil
    local remotePlaneScale = (payloadFieldCount >= 12) and tonumber(nonEmptyString(parts[12])) or nil
    local remotePlaneHash = (payloadFieldCount >= 13) and nonEmptyString(parts[13]) or nil

    local hasExtendedOrientation = payloadFieldCount >= 23
    local remotePlaneYaw = hasExtendedOrientation and tonumber(parts[14]) or nil
    local remotePlanePitch = hasExtendedOrientation and tonumber(parts[15]) or nil
    local remotePlaneRoll = hasExtendedOrientation and tonumber(parts[16]) or nil
    local remoteWalkingScale = hasExtendedOrientation and tonumber(nonEmptyString(parts[17])) or
        ((payloadFieldCount >= 15) and tonumber(nonEmptyString(parts[14])) or nil)
    local remoteWalkingHash = hasExtendedOrientation and nonEmptyString(parts[18]) or
        ((payloadFieldCount >= 16) and nonEmptyString(parts[15]) or nil)
    local remoteWalkingYaw = hasExtendedOrientation and tonumber(parts[19]) or nil
    local remoteWalkingPitch = hasExtendedOrientation and tonumber(parts[20]) or nil
    local remoteWalkingRoll = hasExtendedOrientation and tonumber(parts[21]) or nil

    local hasSkinHashes = payloadFieldCount >= 24
    local remotePlaneSkinHash = hasSkinHashes and (parts[22] or "") or nil
    local remoteWalkingSkinHash = hasSkinHashes and (parts[23] or "") or nil
    local remoteCallsign = (idFieldIndex > 2) and sanitizeCallsign(parts[idFieldIndex - 1]) or nil

    local defaults = {}
    if type(peerDefaults) == "table" then
        for k, v in pairs(peerDefaults) do
            defaults[k] = v
        end
    end

    if remoteScale then
        defaults.scale = sanitizeScale(remoteScale, defaults.scale or 1.35)
    end
    if remoteModelHash and remoteModelHash ~= "" then
        defaults.modelHash = remoteModelHash
    end
    if remoteRole then
        defaults.role = remoteRole
    end
    if remotePlaneScale then
        defaults.planeScale = sanitizeScale(remotePlaneScale, defaults.planeScale or defaults.scale or 1.35)
    end
    if remoteWalkingScale then
        defaults.walkingScale = sanitizeScale(remoteWalkingScale, defaults.walkingScale or defaults.scale or 1.35)
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

    local obj = ensurePeerObject(id, peers, objects, qOps, playerModel, playerModelRotationOffset, defaults)

    obj.planeOrientation = sanitizeOrientationTable(obj.planeOrientation, { yaw = 0, pitch = 0, roll = 0 })
    obj.walkingOrientation = sanitizeOrientationTable(obj.walkingOrientation, { yaw = 0, pitch = 0, roll = 0 })

    if remoteRole then
        obj.remoteRole = remoteRole
    end

    if remotePlaneScale then
        obj.planeModelScale = sanitizeScale(remotePlaneScale, obj.planeModelScale or 1.35)
    end
    if remoteWalkingScale then
        obj.walkingModelScale = sanitizeScale(remoteWalkingScale, obj.walkingModelScale or obj.planeModelScale or 1.35)
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

    if remoteScale then
        local nextScale = sanitizeScale(remoteScale, 1.35)
        if remoteRole == "walking" then
            obj.walkingModelScale = nextScale
        elseif remoteRole == "plane" then
            obj.planeModelScale = nextScale
        else
            obj.planeModelScale = nextScale
            obj.walkingModelScale = nextScale
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
    obj.halfSize = makeHalfSize(activeScale)
    obj.modelHash = activeModelHash

    local baseRot = { w = rw, x = rx, y = ry, z = rz }
    local activeOrientation = (activeRole == "walking") and obj.walkingOrientation or obj.planeOrientation
    local baseOffset = resolveBaseOffset(playerModelRotationOffset, activeRole, activeModelHash)
    local modelOffset = buildOrientationOffsetQuat(qOps, baseOffset, activeOrientation)
    obj.baseRot = baseRot
    obj.modelOffset = modelOffset
    obj.rot = qOps.normalize(qOps.multiply(baseRot, modelOffset))

    local now = tonumber(receiveTime) or os.clock()
    obj.basePos = { px, py, pz }
    obj.pos = { px, py + (obj.visualOffsetY or 0), pz }

    if dynamic then
        obj.netVel = copyVec3(dynamic.velocity, obj.netVel)
        obj.netAngVel = copyVec3(dynamic.angularVelocity, obj.netAngVel)
        obj.netControls = {
            throttle = tonumber(dynamic.controls and dynamic.controls.throttle) or 0,
            elevator = tonumber(dynamic.controls and dynamic.controls.elevator) or 0,
            aileron = tonumber(dynamic.controls and dynamic.controls.aileron) or 0,
            rudder = tonumber(dynamic.controls and dynamic.controls.rudder) or 0
        }
        obj.netTick = math.max(0, math.floor(tonumber(dynamic.tick) or 0))
        obj.netTs = tonumber(dynamic.ts) or 0
    else
        obj.netVel = obj.netVel or { 0, 0, 0 }
        obj.netAngVel = obj.netAngVel or { 0, 0, 0 }
        obj.netControls = obj.netControls or { throttle = 0, elevator = 0, aileron = 0, rudder = 0 }
    end
    obj.netReceivedAt = now

    appendSnapshot(obj, {
        t = now,
        pos = { px, py, pz },
        rot = { w = baseRot.w, x = baseRot.x, y = baseRot.y, z = baseRot.z },
        vel = copyVec3(obj.netVel),
        angVel = copyVec3(obj.netAngVel)
    })

    return id
end

local function samplePeerSnapshot(peer, sampleTime, qOps, extrapolationCap)
    local buffer = peer.stateBuffer
    if type(buffer) ~= "table" or #buffer == 0 then
        return nil
    end

    while #buffer >= 3 and sampleTime >= (buffer[2].t or -math.huge) do
        table.remove(buffer, 1)
    end

    local first = buffer[1]
    if sampleTime <= (first.t or 0) then
        return {
            pos = copyVec3(first.pos),
            rot = { w = first.rot.w, x = first.rot.x, y = first.rot.y, z = first.rot.z },
            vel = copyVec3(first.vel),
            angVel = copyVec3(first.angVel)
        }
    end

    for i = 1, #buffer - 1 do
        local a = buffer[i]
        local b = buffer[i + 1]
        local ta = tonumber(a.t) or 0
        local tb = tonumber(b.t) or ta
        if sampleTime >= ta and sampleTime <= tb then
            local dt = math.max(1e-6, tb - ta)
            local t = clamp((sampleTime - ta) / dt, 0, 1)
            return {
                pos = hermiteVec3(a.pos, a.vel, b.pos, b.vel, dt, t),
                rot = quatNlerp(a.rot, b.rot, t, qOps),
                vel = vec3Lerp(a.vel, b.vel, t),
                angVel = vec3Lerp(a.angVel, b.angVel, t)
            }
        end
    end

    local last = buffer[#buffer]
    local dt = clamp(sampleTime - (last.t or sampleTime), 0, extrapolationCap)
    return {
        pos = vec3Add(last.pos, vec3Scale(last.vel, dt)),
        rot = integrateQuaternion(last.rot, last.angVel, dt, qOps),
        vel = copyVec3(last.vel),
        angVel = copyVec3(last.angVel)
    }
end

function networking.updateRemoteInterpolation(peers, now, opts)
    if type(peers) ~= "table" then
        return
    end

    opts = opts or {}
    local qOps = opts.qOps or quat
    local interpolationDelay = math.max(0, tonumber(opts.interpolationDelay) or DEFAULT_INTERPOLATION_DELAY)
    local extrapolationCap = math.max(0, tonumber(opts.extrapolationCap) or DEFAULT_EXTRAPOLATION_CAP)
    local sampleTime = (tonumber(now) or os.clock()) - interpolationDelay

    for _, peer in pairs(peers) do
        if type(peer) == "table" and type(peer.stateBuffer) == "table" and #peer.stateBuffer > 0 then
            local sampled = samplePeerSnapshot(peer, sampleTime, qOps, extrapolationCap)
            if sampled then
                peer.basePos = sampled.pos
                peer.pos = {
                    sampled.pos[1],
                    sampled.pos[2] + (peer.visualOffsetY or 0),
                    sampled.pos[3]
                }
                peer.baseRot = sampled.rot
                peer.netVel = sampled.vel
                peer.netAngVel = sampled.angVel

                local modelOffset = peer.modelOffset or quatIdentity(qOps)
                peer.rot = qOps.normalize(qOps.multiply(sampled.rot, modelOffset))
            end
        end
    end
end

return networking
