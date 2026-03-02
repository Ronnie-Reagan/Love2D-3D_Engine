local networking = {}

local objectLib = require "object"
local cubeModel = objectLib.cubeModel


function networking.createObjectForPeer(peerID, objects, q)
    local obj = {
        model = cubeModel,
        pos = { 0, 0, 0 },
        rot = q.identity(),
        color = { math.random(), math.random(), math.random() },
        isSolid = true,
        id = peerID
    }

    table.insert(objects, obj)
    return obj
end

function networking.handlePacket(data, peers, objects, q)
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
        peers[id] = networking.createObjectForPeer(id, objects, q)
    end

    local obj = peers[id]
    obj.pos = { px, py, pz }
    obj.rot = { w = rw, x = rx, y = ry, z = rz }
end

return networking
