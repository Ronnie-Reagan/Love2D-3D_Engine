
local networking = {}

local objectLib = require "object"
local cubeModel = objectLib.cubeModel


function networking.createObjectForPeer(peerID, objects, q)
    local obj = {
        model = cubeModel,
        pos = {0,0,0},
        rot = q.identity(),
        color = {math.random(), math.random(), math.random()},
        isSolid = true,
        id = peerID
    }

    table.insert(objects, obj)
    return obj
end

function networking.handlePacket(data, peers, objects, q)
    local parts = {}
    for p in string.gmatch(data, "([^|]+)") do
        table.insert(parts, p)
    end

    if parts[1] == "STATE" then
        local id = tonumber(parts[#parts])

        if not peers[id] and id ~= nil then
            peers[id] = networking.createObjectForPeer(id, objects, q)
        end

        local obj = peers[id]

        obj.pos = {
            tonumber(parts[2]),
            tonumber(parts[3]),
            tonumber(parts[4])
        }

        obj.rot = {
            w = tonumber(parts[5]),
            x = tonumber(parts[6]),
            y = tonumber(parts[7]),
            z = tonumber(parts[8])
        }
    end
end

return networking