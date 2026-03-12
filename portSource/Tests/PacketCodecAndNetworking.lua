local packetCodec = require("Source.Net.PacketCodec")
local networking = require("Source.Net.Networking")
local objectDefs = require("Source.Core.ObjectDefs")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function assertEq(actual, expected, message)
    if actual ~= expected then
        error((message or "assertion failed") .. string.format(" (actual=%s expected=%s)", tostring(actual), tostring(expected)), 2)
    end
end

local function quatLen(rot)
    return math.sqrt(
        (rot.w or 0) * (rot.w or 0) +
        (rot.x or 0) * (rot.x or 0) +
        (rot.y or 0) * (rot.y or 0) +
        (rot.z or 0) * (rot.z or 0)
    )
end

local function buildLegacyStatePacket()
    local fields = {
        "STATE",
        "1", "2", "3",
        "1", "0", "0", "0",
        "1.2",
        "hashA",
        "walking",
        "", "", "", "", "", "", "", "", "", "", "", "",
        "Pilot",
        "7"
    }
    return table.concat(fields, "|")
end

local function buildState3Packet(fields)
    return table.concat({
        "STATE3",
        "px=" .. tostring(fields.px or 0),
        "py=" .. tostring(fields.py or 0),
        "pz=" .. tostring(fields.pz or 0),
        "rw=1",
        "rx=0",
        "ry=0",
        "rz=0",
        "vx=" .. tostring(fields.vx or 0),
        "vy=" .. tostring(fields.vy or 0),
        "vz=" .. tostring(fields.vz or 0),
        "wx=" .. tostring(fields.wx or 0),
        "wy=" .. tostring(fields.wy or 0),
        "wz=" .. tostring(fields.wz or 0),
        "thr=" .. tostring(fields.thr or 0),
        "elev=" .. tostring(fields.elev or 0),
        "ail=" .. tostring(fields.ail or 0),
        "rud=" .. tostring(fields.rud or 0),
        "tick=" .. tostring(fields.tick or 0),
        "ts=" .. tostring(fields.ts or 0),
        "scale=1.1",
        "modelHash=hashC",
        "role=plane",
        "callsign=Ace",
        tostring(fields.id or 9)
    }, "|")
end

local function run()
    local fields = packetCodec.splitFields("A||B|")
    assertEq(#fields, 4, "split should preserve empty and trailing fields")
    assertEq(fields[1], "A")
    assertEq(fields[2], "")
    assertEq(fields[3], "B")
    assertEq(fields[4], "")

    local kv = packetCodec.parseKeyValueFields(packetCodec.splitFields("X|a=1|b=|c=three", "|"), 2)
    assertEq(kv.a, "1")
    assertEq(kv.b, "")
    assertEq(kv.c, "three")

    local peers = {}
    local objects = {}
    local q = {
        fromAxisAngle = function()
            return { w = 1, x = 0, y = 0, z = 0 }
        end,
        multiply = function(a)
            return a
        end,
        normalize = function(v)
            return v
        end
    }

    local legacyPacket = buildLegacyStatePacket()
    local legacyId = networking.handlePacket(legacyPacket, peers, objects, q, objectDefs.cubeModel, nil, {
        scale = 1.0,
        modelHash = "builtin-cube",
        planeScale = 1.0,
        walkingScale = 1.0,
        planeModelHash = "builtin-cube",
        walkingModelHash = "builtin-cube",
        role = "plane"
    })
    assertEq(legacyId, 7, "legacy STATE packet should parse")
    assertTrue(peers[7] ~= nil, "peer should be created for legacy packet")
    assertEq(peers[7].callsign, "Pilot", "callsign should parse from legacy packet")

    local state2Packet = table.concat({
        "STATE2",
        "px=4",
        "py=5",
        "pz=6",
        "rw=1",
        "rx=0",
        "ry=0",
        "rz=0",
        "scale=-2",
        "modelHash=hashB",
        "role=plane",
        "callsign=",
        "8"
    }, "|")

    local state2Id = networking.handlePacket(state2Packet, peers, objects, q, objectDefs.cubeModel, nil, {
        scale = 1.0,
        modelHash = "builtin-cube",
        planeScale = 1.0,
        walkingScale = 1.0,
        planeModelHash = "builtin-cube",
        walkingModelHash = "builtin-cube",
        role = "plane"
    })
    assertEq(state2Id, 8, "STATE2 packet should parse with trailing id")
    assertTrue(peers[8] ~= nil, "peer should be created for STATE2 packet")
    assertTrue(peers[8].scale[1] > 0, "negative scale should sanitize to positive")

    local malformedId = networking.handlePacket("STATE|1|2|3|1|0|0|0|", peers, objects, q, objectDefs.cubeModel, nil, nil)
    assertTrue(malformedId == nil, "malformed positional packet should be rejected")

    local state3a = buildState3Packet({
        id = 9,
        px = 10,
        py = 20,
        pz = 30,
        vx = 5,
        vy = 0,
        vz = 0,
        tick = 100,
        ts = 1.0
    })
    local state3Id = networking.handlePacket(state3a, peers, objects, q, objectDefs.cubeModel, nil, {
        scale = 1.0,
        modelHash = "builtin-cube",
        planeScale = 1.0,
        walkingScale = 1.0,
        planeModelHash = "builtin-cube",
        walkingModelHash = "builtin-cube",
        role = "plane"
    }, 10.00)
    assertEq(state3Id, 9, "STATE3 packet should parse")
    assertTrue(type(peers[9].netVel) == "table", "STATE3 should store velocity")
    assertEq(peers[9].netTick, 100, "STATE3 should store tick")
    assertEq(peers[9].callsign, "Ace", "STATE3 should parse callsign")

    local state3b = buildState3Packet({
        id = 9,
        px = 10.5,
        py = 20,
        pz = 30,
        vx = 5,
        vy = 0,
        vz = 0,
        tick = 101,
        ts = 1.1
    })
    networking.handlePacket(state3b, peers, objects, q, objectDefs.cubeModel, nil, {
        scale = 1.0,
        modelHash = "builtin-cube",
        planeScale = 1.0,
        walkingScale = 1.0,
        planeModelHash = "builtin-cube",
        walkingModelHash = "builtin-cube",
        role = "plane"
    }, 10.10)
    networking.updateRemoteInterpolation(peers, 10.22, {
        interpolationDelay = 0.12,
        extrapolationCap = 0.2
    })
    assertTrue((peers[9].basePos and peers[9].basePos[1] or 0) >= 10.45, "interpolation should advance peer position")

    local spinPacket = buildState3Packet({
        id = 11,
        px = 0,
        py = 5,
        pz = 0,
        vx = 0,
        vy = 0,
        vz = 0,
        wx = 0,
        wy = 1.0,
        wz = 0,
        tick = 300,
        ts = 2.0
    })
    networking.handlePacket(spinPacket, peers, objects, q, objectDefs.cubeModel, nil, {
        scale = 1.0,
        modelHash = "builtin-cube",
        planeScale = 1.0,
        walkingScale = 1.0,
        planeModelHash = "builtin-cube",
        walkingModelHash = "builtin-cube",
        role = "plane"
    }, 20.0)
    networking.updateRemoteInterpolation(peers, 20.4, {
        interpolationDelay = 0.0,
        extrapolationCap = 0.2
    })
    assertTrue(peers[11] ~= nil and peers[11].baseRot ~= nil, "extrapolated peer should keep rotation state")
    assertTrue(math.abs(quatLen(peers[11].baseRot) - 1.0) < 1e-3, "extrapolated quaternion should stay normalized")
    assertTrue(math.abs(peers[11].baseRot.y or 0) > 1e-4, "angular velocity extrapolation should rotate peer state")

    print("Packet codec/networking tests passed")
end

run()
