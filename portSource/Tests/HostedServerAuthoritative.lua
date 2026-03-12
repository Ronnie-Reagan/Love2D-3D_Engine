local enet = require("enet")
local hostedServer = require("Source.Net.HostedServer")
local packetCodec = require("Source.Net.PacketCodec")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function splitFields(packet)
    return packetCodec.splitFields(packet or "", "|")
end

local function parseKv(packet)
    local kv = {}
    for _, field in ipairs(splitFields(packet)) do
        local key, value = field:match("^([^=|]+)=(.*)$")
        if key then
            kv[key] = value
        end
    end
    return kv
end

local function pickServer()
    for port = 24180, 24220 do
        local server = hostedServer.create({
            port = port,
            bindAddress = "127.0.0.1",
            maxPeers = 8,
            groundParams = {
                seed = 9090,
                chunkSize = 48,
                dynamicCraters = {
                    { x = 1, y = 2, z = 3, radius = 4, depth = 5, rim = 0.2 }
                }
            }
        })
        if server then
            return server, port
        end
    end
    error("could not bind local hosted server test port")
end

local function collectPackets(server, host, timeoutSeconds, predicate)
    local packets = {}
    local deadline = love.timer.getTime() + timeoutSeconds
    while love.timer.getTime() < deadline do
        server:update(10)
        local event = host:service(10)
        while event do
            if event.type == "receive" and type(event.data) == "string" then
                packets[#packets + 1] = event.data
            end
            if predicate(packets, event) then
                return packets
            end
            event = host:service()
        end
        if predicate(packets, nil) then
            return packets
        end
    end
    return packets
end

local function hasPacketType(packets, packetType)
    for _, packet in ipairs(packets) do
        if splitFields(packet)[1] == packetType then
            return true, packet
        end
    end
    return false, nil
end

local function packetTypeList(packets)
    local out = {}
    for _, packet in ipairs(packets or {}) do
        out[#out + 1] = splitFields(packet)[1] or "?"
    end
    return table.concat(out, ",")
end

local function run()
    local server, port = pickServer()
    local clientHost = enet.host_create(nil, 1, 4)
    assertTrue(clientHost ~= nil, "client host should create")
    local peer = clientHost:connect("127.0.0.1:" .. tostring(port), 4)
    assertTrue(peer ~= nil, "client peer should connect")

    local connectedPackets = collectPackets(server, clientHost, 1.5, function(_, event)
        return event and event.type == "connect"
    end)
    assertTrue(type(connectedPackets) == "table", "connect wait should complete")

    peer:send(table.concat({
        "HELLO",
        "callsign=AuthoritativeTest",
        "role=plane",
        "planeModelHash=builtin-cube",
        "planeScale=1.0",
        "walkingModelHash=builtin-cube",
        "walkingScale=1.0",
        "radio=1",
        "ptt=0"
    }, "|"), 0)

    local helloPackets = collectPackets(server, clientHost, 1.5, function(packets)
        local hasJoin = hasPacketType(packets, "JOIN_OK")
        local hasInfo = hasPacketType(packets, "WORLD_INFO")
        local hasWorld = hasPacketType(packets, "WORLD_SYNC")
        local hasCloud = hasPacketType(packets, "CLOUD_SNAPSHOT")
        return hasJoin and hasInfo and hasWorld and hasCloud
    end)
    local hasJoin, joinPacket = hasPacketType(helloPackets, "JOIN_OK")
    local hasInfo, worldInfoPacket = hasPacketType(helloPackets, "WORLD_INFO")
    local hasWorld, worldPacket = hasPacketType(helloPackets, "WORLD_SYNC")
    local hasCloud, cloudPacket = hasPacketType(helloPackets, "CLOUD_SNAPSHOT")
    assertTrue(hasJoin, "hosted server should acknowledge hello with JOIN_OK")
    assertTrue(hasInfo, "hosted server should send WORLD_INFO after hello")
    assertTrue(hasWorld, "hosted server should send WORLD_SYNC after hello")
    assertTrue(hasCloud, "hosted server should send CLOUD_SNAPSHOT after hello")
    local worldInfoKv = parseKv(worldInfoPacket)
    assertTrue(worldInfoKv.worldId == "default", "world info should include the world id")
    assertTrue(tonumber(worldInfoKv.horizonRadius) ~= nil, "world info should include horizon radius")
    local worldKv = parseKv(worldPacket)
    assertTrue(type(worldKv.craters) == "string" and worldKv.craters:find("1.0000,2.0000,3.0000", 1, true), "world sync should include serialized crater state")
    assertTrue(worldKv.splitLod == "0", "world sync should advertise single-LOD mode")
    assertTrue(tonumber(worldKv.splitRatio) ~= nil, "world sync should include split LOD ratio")
    local cloudFields = splitFields(cloudPacket)
    assertTrue(tonumber(cloudFields[5]) ~= nil, "cloud snapshot should include group count")

    peer:send("AOI_SUBSCRIBE|cx=0|cz=0|radius=20|snapshotNear=1024|snapshotFar=4096", 0)

    peer:send(table.concat({
        "INPUT",
        "tick=1",
        "role=plane",
        "throttle=0.25",
        "yokePitch=0.0",
        "yokeYaw=0.0",
        "yokeRoll=0.0",
        "planeModelHash=builtin-cube",
        "planeScale=1.0",
        "walkingModelHash=builtin-cube",
        "walkingScale=1.0",
        "radio=1",
        "ptt=0"
    }, "|"), 1)

    local snapshotPackets = collectPackets(server, clientHost, 1.5, function(packets)
        return hasPacketType(packets, "SNAPSHOT")
    end)
    local hasSnapshot, snapshotPacket = hasPacketType(snapshotPackets, "SNAPSHOT")
    assertTrue(hasSnapshot, "hosted server should publish authoritative snapshots (received: " .. packetTypeList(snapshotPackets) .. ")")
    local snapshotKv = parseKv(snapshotPacket)
    assertTrue(tonumber(snapshotKv.ack) == 1, "authoritative snapshot should acknowledge the latest processed input tick")

    peer:send(table.concat({
        "INPUT",
        "tick=2",
        "role=walking",
        "walkYaw=0.0",
        "walkPitch=0.0",
        "walkForward=1",
        "walkBackward=0",
        "walkStrafeLeft=0",
        "walkStrafeRight=0",
        "walkSprint=0",
        "walkJump=0",
        "planeModelHash=builtin-cube",
        "planeScale=1.0",
        "walkingModelHash=builtin-cube",
        "walkingScale=1.0",
        "radio=1",
        "ptt=0"
    }, "|"), 1)

    local walkingSnapshotPackets = collectPackets(server, clientHost, 1.5, function(packets)
        for _, packet in ipairs(packets) do
            if splitFields(packet)[1] == "SNAPSHOT" then
                local kv = parseKv(packet)
                if kv.role == "walking" and tonumber(kv.ack) == 2 then
                    return true
                end
            end
        end
        return false
    end)
    local walkingSnapshotPacket = nil
    for _, packet in ipairs(walkingSnapshotPackets) do
        if splitFields(packet)[1] == "SNAPSHOT" then
            local kv = parseKv(packet)
            if kv.role == "walking" and tonumber(kv.ack) == 2 then
                walkingSnapshotPacket = packet
                break
            end
        end
    end
    assertTrue(walkingSnapshotPacket ~= nil, "walking authoritative snapshot should be emitted")
    local walkingKv = parseKv(walkingSnapshotPacket)
    local walkSpeed = math.abs(tonumber(walkingKv.vx) or 0) + math.abs(tonumber(walkingKv.vy) or 0) + math.abs(tonumber(walkingKv.vz) or 0)
    assertTrue(walkSpeed > 0.001, "walking snapshot should serialize movement velocity")

    peer:send("CLOUD_REQUEST|0.0000", 0)
    local cloudRequestPackets = collectPackets(server, clientHost, 1.5, function(packets)
        return hasPacketType(packets, "CLOUD_SNAPSHOT")
    end)
    local hasRequestedCloud = hasPacketType(cloudRequestPackets, "CLOUD_SNAPSHOT")
    assertTrue(hasRequestedCloud, "server should answer CLOUD_REQUEST with CLOUD_SNAPSHOT")

    peer:send("CRATER_EVENT|x=8|y=1|z=4|radius=6|depth=3|rim=0.1", 0)
    local craterPackets = collectPackets(server, clientHost, 1.5, function(packets)
        local hasPatch = hasPacketType(packets, "CHUNK_PATCH")
        local hasLegacy = hasPacketType(packets, "CRATER_EVENT")
        return hasPatch and hasLegacy
    end)
    local hasPatch, patchPacket = hasPacketType(craterPackets, "CHUNK_PATCH")
    local hasCrater = hasPacketType(craterPackets, "CRATER_EVENT")
    assertTrue(hasPatch, "server should broadcast chunk patch updates for crater edits")
    assertTrue(hasCrater, "server should preserve legacy crater rebroadcast compatibility")
    local patchKv = parseKv(patchPacket)
    assertTrue(tonumber(patchKv.revision) ~= nil and tonumber(patchKv.revision) >= 1, "chunk patch should carry a revision")

    pcall(function()
        peer:disconnect_now()
    end)
    pcall(function()
        clientHost:flush()
    end)
    server:stop()

    print("Hosted server authoritative tests passed")
end

run()
