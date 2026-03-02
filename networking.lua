local networking = {}

local objectLib = require "object"
local cubeModel = objectLib.cubeModel

function networking.createObjectForPeer(peerID, objects, q, playerModel)
    local model = playerModel or cubeModel
    local obj = {
        model = model,
        pos = { 0, 0, 0 },
        rot = q.identity(),
        color = { math.random(), math.random(), math.random() },
        isSolid = true,
        id = peerID,
        scale = { 0.9, 0.9, 0.9 },
        halfSize = { x = 0.9, y = 0.9, z = 0.9 }
    }

    table.insert(objects, obj)
    return obj
end

function networking.handlePacket(data, peers, objects, q, playerModel)
    if type(data) ~= "string" then
        return
    end

    local parts = {}
    for p in string.gmatch(data, "([^|]+)") do
        table.insert(parts, p)
    end

    if parts[1] ~= "STATE" then
        return
    end

    if #parts < 9 then
        return
    end

    local id = tonumber(parts[#parts])
    local px = tonumber(parts[2])
    local py = tonumber(parts[3])
    local pz = tonumber(parts[4])
    local rw = tonumber(parts[5])
    local rx = tonumber(parts[6])
    local ry = tonumber(parts[7])
    local rz = tonumber(parts[8])

    if not (id and px and py and pz and rw and rx and ry and rz) then
        return
    end

    if not peers[id] then
        peers[id] = networking.createObjectForPeer(id, objects, q, playerModel)
    end

    local obj = peers[id]
    obj.pos = { px, py, pz }
    obj.rot = { w = rw, x = rx, y = ry, z = rz }
end

return networking
