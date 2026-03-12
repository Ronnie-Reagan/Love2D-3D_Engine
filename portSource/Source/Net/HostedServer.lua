local enet = require("enet")

local blobCacheModule = require("Source.Net.BlobCache")
local blobSync = require("Source.ModelModule.NetBlobSync")
local flightDynamics = require("Source.Systems.FlightDynamicsSystem")
local cloudSim = require("Source.Systems.CloudSim")
local terrainSdfSystem = require("Source.Systems.TerrainSdfSystem")
local engine = require("Source.Core.Engine")
local gameDefaults = require("Source.Core.GameDefaults")
local objectDefs = require("Source.Core.ObjectDefs")
local packetCodec = require("Source.Net.PacketCodec")
local q = require("Source.Math.Quat")
local vector3 = require("Source.Math.Vector3")
local worldStoreModule = require("Source.World.WorldStore")
local worldWire = require("Source.World.WorldWire")

local hostedServer = {}

local defaults = gameDefaults.create(objectDefs.cubeModel, q)
local defaultGroundParams = terrainSdfSystem.normalizeGroundParams(defaults.defaultGroundParams, defaults.terrainSdf)
local defaultFlightConfig = defaults.flightModel or flightDynamics.defaultConfig()

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function formatFloat(value)
	return string.format("%.4f", tonumber(value) or 0)
end

local function normalizePort(value, fallback)
	local port = math.floor(tonumber(value) or fallback or 1988)
	return clamp(port, 1, 65535)
end

local function normalizeBindAddress(bindAddress, port)
	local host = (type(bindAddress) == "string" and bindAddress ~= "") and bindAddress or "*"
	return string.format("%s:%d", host, port)
end

local function packetType(data)
	return packetCodec.splitFields(data, "|")[1]
end

local function parseKv(data)
	return packetCodec.parseKeyValueFields(packetCodec.splitFields(data, "|"), 2)
end

local function encodePacket(packetTypeId, fields)
	local parts = { packetTypeId }
	for _, entry in ipairs(fields or {}) do
		parts[#parts + 1] = entry[1] .. "=" .. tostring(entry[2] or "")
	end
	return table.concat(parts, "|")
end

local function safeLog(logFn, message)
	if type(logFn) == "function" then
		logFn(message)
	end
end

local function sanitizeRole(token)
	if token == "walking" then
		return "walking"
	end
	return "plane"
end

local function sanitizeCallsign(value)
	if type(value) ~= "string" then
		return "Pilot"
	end
	local text = value:gsub("[^ -~]", "")
	text = text:gsub("|", "")
	text = text:gsub("^%s+", ""):gsub("%s+$", "")
	if text == "" then
		return "Pilot"
	end
	if #text > 20 then
		text = text:sub(1, 20)
	end
	return text
end

local function copyOffset(value)
	local source = type(value) == "table" and value or {}
	return {
		tonumber(source[1] or source.x) or 0,
		tonumber(source[2] or source.y) or 0,
		tonumber(source[3] or source.z) or 0
	}
end

local function copyOrientation(value)
	local source = type(value) == "table" and value or {}
	return {
		yaw = tonumber(source.yaw) or 0,
		pitch = tonumber(source.pitch) or 0,
		roll = tonumber(source.roll) or 0,
		offset = copyOffset(source.offset or { 0, tonumber(source.offsetY) or 0, 0 })
	}
end

local function composeWalkingRotation(yaw, pitch)
	local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, yaw)
	local right = q.rotateVector(yawQuat, { 1, 0, 0 })
	local pitchQuat = q.fromAxisAngle(right, pitch)
	return q.normalize(q.multiply(pitchQuat, yawQuat))
end

local function cloneVec3(value)
	local source = type(value) == "table" and value or {}
	return {
		tonumber(source[1]) or 0,
		tonumber(source[2]) or 0,
		tonumber(source[3]) or 0
	}
end

local function cloneWindState(value)
	local source = type(value) == "table" and value or {}
	return {
		angle = tonumber(source.angle) or 0,
		speed = tonumber(source.speed) or 9,
		targetAngle = tonumber(source.targetAngle) or tonumber(source.angle) or 0,
		targetSpeed = tonumber(source.targetSpeed) or tonumber(source.speed) or 9,
		retargetIn = tonumber(source.retargetIn) or 0
	}
end

local function cloneCloudState(value)
	local source = type(value) == "table" and value or {}
	return {
		groups = {},
		groupCount = math.max(1, math.floor(tonumber(source.groupCount) or 1)),
		minAltitude = tonumber(source.minAltitude) or 1200,
		maxAltitude = tonumber(source.maxAltitude) or 3750,
		spawnRadius = tonumber(source.spawnRadius) or 1800,
		despawnRadius = tonumber(source.despawnRadius) or 1900,
		minGroupSize = tonumber(source.minGroupSize) or 30,
		maxGroupSize = tonumber(source.maxGroupSize) or 100,
		minPuffs = math.max(1, math.floor(tonumber(source.minPuffs) or 8)),
		maxPuffs = math.max(1, math.floor(tonumber(source.maxPuffs) or 30))
	}
end

local function buildPlayerCamera(flightConfig)
	flightConfig = flightConfig or defaultFlightConfig
	return {
		pos = { 0, 0, -5 },
		rot = q.identity(),
		speed = 10,
		fov = math.rad(90),
		baseFov = math.rad(90),
		vel = { 0, 0, 0 },
		onGround = false,
		gravity = -9.80665,
		jumpSpeed = 5,
		throttle = 0,
		throttleAccel = 0.55,
		flightMass = tonumber(flightConfig.massKg) or 1150,
		flightThrustForce = tonumber(flightConfig.maxThrustSeaLevel) or 3200,
		flightThrustAccel = tonumber(flightConfig.maxThrustSeaLevel) or 3200,
		flightGroundFriction = tonumber(flightConfig.groundFriction) or 0.92,
		flightVel = { 0, 0, 0 },
		flightAngVel = { 0, 0, 0 },
		flightRotVel = { pitch = 0, yaw = 0, roll = 0 },
		yoke = { pitch = 0, yaw = 0, roll = 0 },
		yokeKeyboardRate = 2.8,
		yokeAutoCenterRate = 0.5,
		yokeMouseHoldDurationSec = 0.75,
		throttleAccel = 0.55,
		sprintMultiplier = 1.8,
		box = {
			halfSize = { x = 0.5, y = 1.8, z = 0.5 },
			pos = { 0, 0, -5 },
			isSolid = true
		},
		walkYaw = 0,
		walkPitch = 0
	}
end

local function makePlayer(peerId, peer, opts)
	opts = opts or {}
	return {
		id = peerId,
		peer = peer,
		role = "plane",
		callsign = "Pilot",
		camera = buildPlayerCamera(opts.flightModelConfig),
		planeModelHash = "builtin-cube",
		walkingModelHash = "builtin-cube",
		planeSkinHash = "",
		walkingSkinHash = "",
		planeModelScale = 3.0,
		walkingModelScale = 1.0,
		planeOrientation = copyOrientation({ yaw = -90, pitch = 0, roll = 0, offset = { 0, 0, 0 } }),
		walkingOrientation = copyOrientation({ yaw = 0, pitch = 0, roll = 0, offset = { 0, 0, 0 } }),
		radioChannel = 1,
		radioTx = false,
		aoi = {
			centerChunkX = 0,
			centerChunkZ = 0,
			radiusChunks = 20,
			snapshotNearMeters = 1024,
			snapshotFarMeters = 4096,
			lastChunkStateByKey = {}
		},
		lastInputTick = 0,
		hasReceivedInput = false,
		lastHelloAt = -math.huge,
		input = {
			role = "plane",
			walkYaw = 0,
			walkPitch = 0,
			throttle = 0,
			yokePitch = 0,
			yokeYaw = 0,
			yokeRoll = 0,
			walkForward = false,
			walkBackward = false,
			walkStrafeLeft = false,
			walkStrafeRight = false,
			walkSprint = false,
			walkJump = false,
			flightThrottleUp = false,
			flightThrottleDown = false,
			flightAirBrakes = false,
			flightAfterburner = false
		}
	}
end

local function packetFlag(value)
	return (tonumber(value) or 0) ~= 0
end

local function syncPlayerProfileFromKv(player, kv)
	player.callsign = sanitizeCallsign(kv.callsign or player.callsign)
	player.role = sanitizeRole(kv.role or player.role)
	player.radioChannel = clamp(math.floor(tonumber(kv.radio) or player.radioChannel or 1), 1, 8)
	player.radioTx = packetFlag(kv.ptt)
	player.planeModelHash = kv.planeModelHash or kv.modelHash or player.planeModelHash
	player.walkingModelHash = kv.walkingModelHash or player.walkingModelHash or player.planeModelHash
	player.planeSkinHash = kv.planeSkinHash or player.planeSkinHash or ""
	player.walkingSkinHash = kv.walkingSkinHash or player.walkingSkinHash or ""
	player.planeModelScale = math.max(0.1, tonumber(kv.planeScale) or player.planeModelScale or 3.0)
	player.walkingModelScale = math.max(0.1, tonumber(kv.walkingScale) or player.walkingModelScale or 1.0)
	player.planeOrientation.yaw = tonumber(kv.planeYaw) or player.planeOrientation.yaw
	player.planeOrientation.pitch = tonumber(kv.planePitch) or player.planeOrientation.pitch
	player.planeOrientation.roll = tonumber(kv.planeRoll) or player.planeOrientation.roll
	player.walkingOrientation.yaw = tonumber(kv.walkingYaw) or player.walkingOrientation.yaw
	player.walkingOrientation.pitch = tonumber(kv.walkingPitch) or player.walkingOrientation.pitch
	player.walkingOrientation.roll = tonumber(kv.walkingRoll) or player.walkingOrientation.roll
	player.planeOrientation.offset = {
		tonumber(kv.planeOffX) or player.planeOrientation.offset[1] or 0,
		tonumber(kv.planeOffY) or player.planeOrientation.offset[2] or 0,
		tonumber(kv.planeOffZ) or player.planeOrientation.offset[3] or 0
	}
	player.walkingOrientation.offset = {
		tonumber(kv.walkingOffX) or player.walkingOrientation.offset[1] or 0,
		tonumber(kv.walkingOffY) or player.walkingOrientation.offset[2] or 0,
		tonumber(kv.walkingOffZ) or player.walkingOrientation.offset[3] or 0
	}
end

local function buildAvatarManifestPacket(player)
	return encodePacket("AVATAR_MANIFEST", {
		{ "id", player.id },
		{ "role", player.role },
		{ "callsign", player.callsign },
		{ "planeModelHash", player.planeModelHash },
		{ "planeScale", formatFloat(player.planeModelScale) },
		{ "planeSkinHash", player.planeSkinHash },
		{ "planeYaw", formatFloat(player.planeOrientation.yaw) },
		{ "planePitch", formatFloat(player.planeOrientation.pitch) },
		{ "planeRoll", formatFloat(player.planeOrientation.roll) },
		{ "planeOffX", formatFloat(player.planeOrientation.offset[1]) },
		{ "planeOffY", formatFloat(player.planeOrientation.offset[2]) },
		{ "planeOffZ", formatFloat(player.planeOrientation.offset[3]) },
		{ "walkingModelHash", player.walkingModelHash },
		{ "walkingScale", formatFloat(player.walkingModelScale) },
		{ "walkingSkinHash", player.walkingSkinHash },
		{ "walkingYaw", formatFloat(player.walkingOrientation.yaw) },
		{ "walkingPitch", formatFloat(player.walkingOrientation.pitch) },
		{ "walkingRoll", formatFloat(player.walkingOrientation.roll) },
		{ "walkingOffX", formatFloat(player.walkingOrientation.offset[1]) },
		{ "walkingOffY", formatFloat(player.walkingOrientation.offset[2]) },
		{ "walkingOffZ", formatFloat(player.walkingOrientation.offset[3]) }
	})
end

local function buildSnapshotPacket(player, ackTick)
	local camera = player.camera
	local flightTick = (camera.flightSimState and camera.flightSimState.tick) or 0
	local vel = (player.role == "walking") and (camera.vel or camera.flightVel or { 0, 0, 0 }) or
		(camera.flightVel or camera.vel or { 0, 0, 0 })
	local angVel = camera.flightAngVel or { 0, 0, 0 }
	return encodePacket("SNAPSHOT", {
		{ "id", player.id },
		{ "ack", math.floor(tonumber(ackTick) or player.lastInputTick or 0) },
		{ "px", formatFloat(camera.pos[1]) },
		{ "py", formatFloat(camera.pos[2]) },
		{ "pz", formatFloat(camera.pos[3]) },
		{ "rw", formatFloat(camera.rot.w) },
		{ "rx", formatFloat(camera.rot.x) },
		{ "ry", formatFloat(camera.rot.y) },
		{ "rz", formatFloat(camera.rot.z) },
		{ "vx", formatFloat(vel[1]) },
		{ "vy", formatFloat(vel[2]) },
		{ "vz", formatFloat(vel[3]) },
		{ "wx", formatFloat(angVel[1]) },
		{ "wy", formatFloat(angVel[2]) },
		{ "wz", formatFloat(angVel[3]) },
		{ "thr", formatFloat(camera.throttle or 0) },
		{ "elev", formatFloat(camera.yoke and camera.yoke.pitch or 0) },
		{ "ail", formatFloat(camera.yoke and camera.yoke.roll or 0) },
		{ "rud", formatFloat(camera.yoke and camera.yoke.yaw or 0) },
		{ "tick", math.floor(flightTick) },
		{ "ts", formatFloat(os.clock()) },
		{ "role", player.role },
		{ "callsign", player.callsign },
		{ "radio", player.radioChannel },
		{ "ptt", player.radioTx and 1 or 0 },
		{ "planeModelHash", player.planeModelHash },
		{ "planeScale", formatFloat(player.planeModelScale) },
		{ "planeSkinHash", player.planeSkinHash },
		{ "planeYaw", formatFloat(player.planeOrientation.yaw) },
		{ "planePitch", formatFloat(player.planeOrientation.pitch) },
		{ "planeRoll", formatFloat(player.planeOrientation.roll) },
		{ "planeOffX", formatFloat(player.planeOrientation.offset[1]) },
		{ "planeOffY", formatFloat(player.planeOrientation.offset[2]) },
		{ "planeOffZ", formatFloat(player.planeOrientation.offset[3]) },
		{ "walkingModelHash", player.walkingModelHash },
		{ "walkingScale", formatFloat(player.walkingModelScale) },
		{ "walkingSkinHash", player.walkingSkinHash },
		{ "walkingYaw", formatFloat(player.walkingOrientation.yaw) },
		{ "walkingPitch", formatFloat(player.walkingOrientation.pitch) },
		{ "walkingRoll", formatFloat(player.walkingOrientation.roll) },
		{ "walkingOffX", formatFloat(player.walkingOrientation.offset[1]) },
		{ "walkingOffY", formatFloat(player.walkingOrientation.offset[2]) },
		{ "walkingOffZ", formatFloat(player.walkingOrientation.offset[3]) }
	})
end

local function buildCloudSnapshotPacket(world, now)
	local cloudState = world.cloudState
	local windState = world.windState
	local parts = {
		"CLOUD_SNAPSHOT",
		formatFloat(now),
		formatFloat(windState.angle),
		formatFloat(windState.speed),
		tostring(#(cloudState.groups or {})),
		formatFloat(cloudState.minAltitude),
		formatFloat(cloudState.maxAltitude),
		formatFloat(cloudState.spawnRadius),
		formatFloat(cloudState.despawnRadius),
		formatFloat(cloudState.minGroupSize),
		formatFloat(cloudState.maxGroupSize),
		tostring(math.floor(cloudState.minPuffs or 1)),
		tostring(math.floor(cloudState.maxPuffs or 1))
	}

	for _, group in ipairs(cloudState.groups or {}) do
		local center = group.center or { 0, 0, 0 }
		parts[#parts + 1] = table.concat({
			formatFloat(center[1]),
			formatFloat(center[2]),
			formatFloat(center[3]),
			formatFloat(group.radius),
			formatFloat(group.windDrift)
		}, ",")
	end

	return table.concat(parts, "|")
end

local function buildWorldSyncPacket(world)
	local params = world.groundParams
	local craterParts = {}
	for i = 1, #(params.dynamicCraters or {}) do
		local crater = params.dynamicCraters[i]
		craterParts[#craterParts + 1] = table.concat({
			formatFloat(crater.x),
			formatFloat(crater.y),
			formatFloat(crater.z),
			formatFloat(crater.radius),
			formatFloat(crater.depth),
			formatFloat(crater.rim)
		}, ",")
	end
	return encodePacket("WORLD_SYNC", {
		{ "seed", params.seed },
		{ "chunk", params.chunkSize },
		{ "lod0", params.lod0Radius },
		{ "lod1", params.lod1Radius },
		{ "lod2", params.lod2Radius },
		{ "splitLod", (params.splitLodEnabled ~= false) and 1 or 0 },
		{ "splitRatio", formatFloat(params.highResSplitRatio) },
		{ "lod0Cell", formatFloat(params.lod0BaseCellSize or params.lod0CellSize) },
		{ "lod1Cell", formatFloat(params.lod1BaseCellSize or params.lod1CellSize) },
		{ "lod2Cell", formatFloat(params.lod2BaseCellSize or params.lod2CellSize) },
		{ "quality", formatFloat(params.terrainQuality) },
		{ "meshBudget", params.meshBuildBudget },
		{ "inflight", params.workerMaxInflight },
		{ "cache", params.chunkCacheLimit },
		{ "autoQuality", params.autoQualityEnabled and 1 or 0 },
		{ "targetFrameMs", formatFloat(params.targetFrameMs) },
		{ "heightAmp", formatFloat(params.heightAmplitude) },
		{ "heightFreq", formatFloat(params.heightFrequency) },
		{ "caveEnabled", params.caveEnabled and 1 or 0 },
		{ "caveStrength", formatFloat(params.caveStrength) },
		{ "tunnelCount", params.tunnelCount },
		{ "tunnelRadiusMin", formatFloat(params.tunnelRadiusMin) },
		{ "tunnelRadiusMax", formatFloat(params.tunnelRadiusMax) },
		{ "craters", table.concat(craterParts, ";") }
	})
end

local function buildWorldInfoPacket(world)
	local params = world.groundParams or {}
	local meta = world.worldStore and world.worldStore:getMeta() or {}
	local spawn = type(meta.spawn) == "table" and meta.spawn or { x = 0, y = 0, z = 0 }
	return encodePacket("WORLD_INFO", {
		{ "worldId", meta.worldId or "default" },
		{ "formatVersion", meta.formatVersion or 1 },
		{ "seed", params.seed },
		{ "chunkSize", params.chunkSize },
		{ "lod0Scale", params.lod0ChunkScale or 1 },
		{ "lod1Scale", params.lod1ChunkScale or 4 },
		{ "lod2Scale", params.lod2ChunkScale or 16 },
		{ "gameplayRadius", formatFloat(params.gameplayRadiusMeters or 512) },
		{ "midRadius", formatFloat(params.midFieldRadiusMeters or 2048) },
		{ "horizonRadius", formatFloat(params.horizonRadiusMeters or 8192) },
		{ "lod0Cell", formatFloat(params.lod0BaseCellSize or params.lod0CellSize) },
		{ "lod1Cell", formatFloat(params.lod1BaseCellSize or params.lod1CellSize) },
		{ "lod2Cell", formatFloat(params.lod2BaseCellSize or params.lod2CellSize) },
		{ "lod0Tex", params.lod0TextureResolution or 256 },
		{ "lod1Tex", params.lod1TextureResolution or 128 },
		{ "lod2Tex", params.lod2TextureResolution or 32 },
		{ "meshBudget", params.meshBuildBudget or 4 },
		{ "heightAmp", formatFloat(params.heightAmplitude) },
		{ "heightFreq", formatFloat(params.heightFrequency) },
		{ "waterLevel", formatFloat(params.waterLevel) },
		{ "caveEnabled", params.caveEnabled and 1 or 0 },
		{ "caveStrength", formatFloat(params.caveStrength) },
		{ "worldRadius", formatFloat(params.worldRadius or 2048) },
		{ "tunnelCount", params.tunnelCount or 0 },
		{ "tunnelSeeds", worldWire.encodeTunnelSeeds(meta.tunnelSeeds) },
		{ "spawnX", formatFloat(spawn.x or spawn[1] or 0) },
		{ "spawnY", formatFloat(spawn.y or spawn[2] or 0) },
		{ "spawnZ", formatFloat(spawn.z or spawn[3] or 0) }
	})
end

local function buildChunkPacket(packetTypeId, chunkState, extraFields)
	local fields = worldWire.buildChunkStateFields(chunkState)
	for _, entry in ipairs(extraFields or {}) do
		fields[#fields + 1] = entry
	end
	return encodePacket(packetTypeId, fields)
end

local function distanceSq2(a, b)
	local dx = (tonumber(a and a[1]) or 0) - (tonumber(b and b[1]) or 0)
	local dz = (tonumber(a and a[3]) or 0) - (tonumber(b and b[3]) or 0)
	return dx * dx + dz * dz
end

local function buildCraterPacket(crater)
	return encodePacket("CRATER_EVENT", {
		{ "x", formatFloat(crater.x) },
		{ "y", formatFloat(crater.y) },
		{ "z", formatFloat(crater.z) },
		{ "radius", formatFloat(crater.radius) },
		{ "depth", formatFloat(crater.depth) },
		{ "rim", formatFloat(crater.rim) }
	})
end

function hostedServer.create(opts)
	opts = opts or {}
	local port = normalizePort(opts.port, 1988)
	local bindAddress = normalizeBindAddress(opts.bindAddress, port)
	local maxPeers = clamp(math.floor(tonumber(opts.maxPeers) or 64), 1, 4096)
	local channels = clamp(math.floor(tonumber(opts.channels) or 4), 1, 8)
	local logFn = opts.log
	local world, worldErr = worldStoreModule.open({
		name = opts.worldName or os.getenv("L2D3D_WORLD_NAME") or "default",
		createIfMissing = opts.createWorld,
		groundParams = terrainSdfSystem.normalizeGroundParams(opts.groundParams or defaultGroundParams, defaultGroundParams)
	})
	if not world then
		return nil, tostring(worldErr or "world open failed")
	end

	local okHost, hostOrErr = pcall(function()
		return enet.host_create(bindAddress, maxPeers, channels)
	end)
	if not okHost or not hostOrErr then
		return nil, tostring(hostOrErr or "host create failed")
	end

	local blobState = blobSync.newState()
	local cache = blobCacheModule.create(opts.cachePath or "server_avatar_cache.lua")
	for _, entry in pairs(cache.entries or {}) do
		if type(entry) == "table" and type(entry.kind) == "string" and type(entry.hash) == "string" and type(entry.raw) == "string" then
			blobSync.prepareOutgoing(blobState, entry.kind, entry.hash, entry.raw, { chunkSize = 720 })
		end
	end

	local now = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
	local cloudState = cloneCloudState(opts.cloudState or defaults.cloudState)
	local windState = cloneWindState(opts.windState or defaults.windState)
	local cloudAnchor = { pos = cloneVec3((opts.cloudAnchor and opts.cloudAnchor.pos) or { 0, 0, 0 }) }
	local cloudObjects = {}
	cloudSim.spawnCloudField(
		cloudState,
		cloudObjects,
		cloudAnchor,
		windState,
		objectDefs.cloudPuffModel or objectDefs.cubeModel,
		q,
		cloudState.groupCount,
		now
	)
	local server = {
		host = hostOrErr,
		port = port,
		bindAddress = bindAddress,
		clients = {},
		players = {},
		log = logFn,
		lastUpdateAt = now,
		accumulator = 0,
		snapshotAccumulator = 0,
		snapshotSequence = 0,
		blobState = blobState,
		blobCache = cache,
		pendingBlobRequests = {},
		lastCloudSnapshotAt = -math.huge,
		cloudSnapshotInterval = clamp(tonumber(opts.cloudSnapshotInterval) or 2.0, 0.2, 30.0),
		worldFlushInterval = clamp(tonumber(opts.worldFlushInterval) or 1.0, 0.1, 30.0),
		lastWorldFlushAt = now,
		world = {
			worldStore = world,
			groundParams = terrainSdfSystem.normalizeGroundParams(world:buildGroundParams(opts.groundParams or defaultGroundParams), defaultGroundParams),
			flightModelConfig = opts.flightModelConfig or defaultFlightConfig,
			wind = cloneVec3(opts.wind or { 0, 0, 0 }),
			windState = windState,
			cloudState = cloudState,
			cloudObjects = cloudObjects,
			cloudAnchor = cloudAnchor,
			cloudAuthorityState = { role = "authority" }
		}
	}

	function server:sendToPeer(peer, payload, channel)
		if not peer or type(payload) ~= "string" or payload == "" then
			return false
		end
		local ok = pcall(function()
			if channel ~= nil then
				peer:send(payload, channel)
			else
				peer:send(payload)
			end
		end)
		return ok and true or false
	end

	function server:broadcast(payload, channel, exceptPeer)
		for _, player in pairs(self.players) do
			if player.peer and player.peer ~= exceptPeer then
				self:sendToPeer(player.peer, payload, channel)
			end
		end
	end

	function server:sendWorldInfo(peer)
		return self:sendToPeer(peer, buildWorldInfoPacket(self.world), 0)
	end

	function server:sendWorldSync(peer)
		self:sendToPeer(peer, buildWorldSyncPacket(self.world), 0)
	end

	function server:sendChunkState(peer, chunkState, reason)
		if not peer or type(chunkState) ~= "table" then
			return false
		end
		return self:sendToPeer(peer, buildChunkPacket("CHUNK_STATE", chunkState, {
			{ "reason", reason or "sync" }
		}), 0)
	end

	function server:broadcastChunkPatch(chunkState, exceptPeer, reason)
		local payload = buildChunkPacket("CHUNK_PATCH", chunkState, {
			{ "reason", reason or "patch" }
		})
		for _, player in pairs(self.players) do
			if player.peer and player.peer ~= exceptPeer and self:playerWantsChunk(player, chunkState.cx, chunkState.cz) then
				self:sendToPeer(player.peer, payload, 0)
			end
		end
	end

	function server:playerWantsChunk(player, cx, cz)
		local aoi = player and player.aoi
		if type(aoi) ~= "table" then
			return true
		end
		local dx = math.abs((tonumber(cx) or 0) - (tonumber(aoi.centerChunkX) or 0))
		local dz = math.abs((tonumber(cz) or 0) - (tonumber(aoi.centerChunkZ) or 0))
		return math.max(dx, dz) <= math.max(0, math.floor(tonumber(aoi.radiusChunks) or 0))
	end

	function server:playerWithinAoiRange(targetPlayer, sourcePlayer, maxMeters)
		if not targetPlayer or not sourcePlayer then
			return false
		end
		local limit = math.max(
			128,
			tonumber(maxMeters) or tonumber(targetPlayer.aoi and targetPlayer.aoi.snapshotFarMeters) or 4096
		)
		return distanceSq2(
			targetPlayer.camera and targetPlayer.camera.pos,
			sourcePlayer.camera and sourcePlayer.camera.pos
		) <= (limit * limit)
	end

	function server:sendChunksForPlayer(player, reason)
		if not player or not player.peer then
			return false
		end
		local aoi = player.aoi or {}
		local chunks = self.world.worldStore:collectEditedChunks(
			math.floor(tonumber(aoi.centerChunkX) or 0),
			math.floor(tonumber(aoi.centerChunkZ) or 0),
			math.max(0, math.floor(tonumber(aoi.radiusChunks) or 0))
		)
		local changed = false
		player.aoi.lastChunkStateByKey = player.aoi.lastChunkStateByKey or {}
		for i = 1, #chunks do
			local chunkState = chunks[i]
			local key = tostring(chunkState.cx) .. ":" .. tostring(chunkState.cz)
			local revision = math.floor(tonumber(chunkState.revision) or 0)
			if player.aoi.lastChunkStateByKey[key] ~= revision then
				self:sendChunkState(player.peer, chunkState, reason or "aoi")
				player.aoi.lastChunkStateByKey[key] = revision
				changed = true
			end
		end
		return changed
	end

	function server:sendCloudSnapshotToPeer(peer)
		local nowTime = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
		return self:sendToPeer(peer, buildCloudSnapshotPacket(self.world, nowTime), 0)
	end

	function server:broadcastAvatarManifest(player, exceptPeer)
		self:broadcast(buildAvatarManifestPacket(player), 0, exceptPeer)
	end

	function server:broadcastCloudSnapshot(forceSend)
		local nowTime = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
		if (not forceSend) and (nowTime - (self.lastCloudSnapshotAt or -math.huge)) < self.cloudSnapshotInterval then
			return false
		end
		local packet = buildCloudSnapshotPacket(self.world, nowTime)
		self:broadcast(packet, 0)
		self.lastCloudSnapshotAt = nowTime
		return true
	end

	function server:fulfillBlobRequest(peer, kind, hash)
		local transfer = self.blobState.outgoing[kind .. "|" .. hash]
		if not transfer then
			return false
		end
		local meta = blobSync.buildMetaPacket(transfer)
		self:sendToPeer(peer, encodePacket("BLOB_META", {
			{ "kind", meta.kind },
			{ "hash", meta.hash },
			{ "rawBytes", meta.rawBytes },
			{ "encodedBytes", meta.encodedBytes },
			{ "chunkSize", meta.chunkSize },
			{ "chunkCount", meta.chunkCount }
		}), 2)
		for chunkIndex = 1, transfer.chunkCount do
			self:sendToPeer(peer, encodePacket("BLOB_CHUNK", {
				{ "kind", kind },
				{ "hash", hash },
				{ "idx", chunkIndex },
				{ "data", blobSync.getOutgoingChunk(transfer, chunkIndex) }
			}), 2)
		end
		return true
	end

	function server:addCrater(crater)
		local craterSpec = {
			x = tonumber(crater.x) or 0,
			y = tonumber(crater.y) or 0,
			z = tonumber(crater.z) or 0,
			radius = math.max(1.0, tonumber(crater.radius) or 8.0),
			depth = math.max(0.4, tonumber(crater.depth) or 3.0),
			rim = clamp(tonumber(crater.rim) or 0.12, 0.0, 0.75)
		}
		local changed, changedChunks = self.world.worldStore:applyCrater(craterSpec)
		if not changed then
			return false
		end
		self.world.groundParams = terrainSdfSystem.normalizeGroundParams(
			self.world.worldStore:buildGroundParams(self.world.groundParams),
			defaultGroundParams
		)
		for i = 1, #changedChunks do
			self:broadcastChunkPatch(changedChunks[i], nil, "crater")
		end
		self:broadcast(buildCraterPacket(craterSpec), 0)
		return true
	end

	function server:setGroundParams(params)
		self.world.groundParams = terrainSdfSystem.normalizeGroundParams(
			self.world.worldStore:buildGroundParams(params),
			defaultGroundParams
		)
		self:broadcast(buildWorldInfoPacket(self.world), 0)
	end

	function server:handleHello(player, kv)
		syncPlayerProfileFromKv(player, kv)
		player.lastHelloAt = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
		self:sendToPeer(player.peer, encodePacket("JOIN_OK", { { "id", player.id } }), 0)
		self:sendWorldInfo(player.peer)
		self:sendWorldSync(player.peer)
		self:sendCloudSnapshotToPeer(player.peer)
		for _, other in pairs(self.players) do
			self:sendToPeer(player.peer, buildAvatarManifestPacket(other), 0)
		end
		self:broadcastAvatarManifest(player, player.peer)
		self:sendChunksForPlayer(player, "hello")
	end

	function server:handleAoiSubscribe(player, kv)
		player.aoi = player.aoi or {
			centerChunkX = 0,
			centerChunkZ = 0,
			radiusChunks = 20,
			snapshotNearMeters = 1024,
			snapshotFarMeters = 4096,
			lastChunkStateByKey = {}
		}
		player.aoi.centerChunkX = math.floor(tonumber(kv.cx) or player.aoi.centerChunkX or 0)
		player.aoi.centerChunkZ = math.floor(tonumber(kv.cz) or player.aoi.centerChunkZ or 0)
		player.aoi.radiusChunks = clamp(math.floor(tonumber(kv.radius) or player.aoi.radiusChunks or 20), 2, 96)
		player.aoi.snapshotNearMeters = math.max(128, tonumber(kv.snapshotNear) or player.aoi.snapshotNearMeters or 1024)
		player.aoi.snapshotFarMeters = math.max(player.aoi.snapshotNearMeters, tonumber(kv.snapshotFar) or player.aoi.snapshotFarMeters or 4096)
		player.aoi.lastChunkStateByKey = player.aoi.lastChunkStateByKey or {}
		self:sendChunksForPlayer(player, "subscribe")
		return true
	end

	function server:handleInput(player, kv)
		syncPlayerProfileFromKv(player, kv)
		player.lastInputTick = math.max(player.lastInputTick, math.floor(tonumber(kv.tick) or 0))
		player.hasReceivedInput = true
		player.input.role = sanitizeRole(kv.role or player.input.role)
		player.input.walkYaw = tonumber(kv.walkYaw) or player.input.walkYaw
		player.input.walkPitch = tonumber(kv.walkPitch) or player.input.walkPitch
		player.input.throttle = clamp(tonumber(kv.throttle) or player.input.throttle or 0, 0, 1)
		player.input.yokePitch = clamp(tonumber(kv.yokePitch) or player.input.yokePitch or 0, -1, 1)
		player.input.yokeYaw = clamp(tonumber(kv.yokeYaw) or player.input.yokeYaw or 0, -1, 1)
		player.input.yokeRoll = clamp(tonumber(kv.yokeRoll) or player.input.yokeRoll or 0, -1, 1)
		player.input.walkForward = packetFlag(kv.walkForward)
		player.input.walkBackward = packetFlag(kv.walkBackward)
		player.input.walkStrafeLeft = packetFlag(kv.walkStrafeLeft)
		player.input.walkStrafeRight = packetFlag(kv.walkStrafeRight)
		player.input.walkSprint = packetFlag(kv.walkSprint)
		player.input.walkJump = packetFlag(kv.walkJump)
		player.input.flightThrottleUp = packetFlag(kv.flightThrottleUp)
		player.input.flightThrottleDown = packetFlag(kv.flightThrottleDown)
		player.input.flightAirBrakes = packetFlag(kv.flightAirBrakes)
		player.input.flightAfterburner = packetFlag(kv.flightAfterburner)
	end

	function server:handleBlobMeta(peer, kv)
		local ok = blobSync.acceptMeta(self.blobState, {
			kind = kv.kind,
			hash = kv.hash,
			rawBytes = tonumber(kv.rawBytes),
			encodedBytes = tonumber(kv.encodedBytes),
			chunkSize = tonumber(kv.chunkSize),
			chunkCount = tonumber(kv.chunkCount)
		})
		return ok
	end

	function server:handleBlobChunk(peer, kv)
		local result, err = blobSync.acceptChunk(
			self.blobState,
			kv.kind,
			kv.hash,
			math.floor(tonumber(kv.idx) or 0),
			kv.data
		)
		if err then
			return false
		end
		if result and result.kind and result.hash then
			local kind = result.kind
			local hash = result.hash
			local raw = result.raw
			if type(raw) == "string" and raw ~= "" then
				self.blobCache:put(kind, hash, raw, {})
				blobSync.prepareOutgoing(self.blobState, kind, hash, raw, { chunkSize = 720 })
				local key = kind .. "|" .. hash
				local waiters = self.pendingBlobRequests[key]
				if type(waiters) == "table" then
					for waiterPeerId in pairs(waiters) do
						local waiter = self.players[waiterPeerId]
						if waiter and waiter.peer then
							self:fulfillBlobRequest(waiter.peer, kind, hash)
						end
					end
					self.pendingBlobRequests[key] = nil
				end
			end
		end
		return true
	end

	function server:serviceClientEvent(event)
		if event.type == "connect" then
			local peerId = event.peer:index()
			local player = makePlayer(peerId, event.peer, { flightModelConfig = self.world.flightModelConfig })
			self.players[peerId] = player
			self.clients[peerId] = event.peer
			safeLog(self.log, string.format("[server] client connected id=%d", peerId))
			return
		end

		if event.type == "disconnect" then
			local peerId = event.peer:index()
			self.players[peerId] = nil
			self.clients[peerId] = nil
			self:broadcast(encodePacket("PEER_LEAVE", { { "id", peerId } }), 0)
			safeLog(self.log, string.format("[server] client disconnected id=%d", peerId))
			return
		end

		if event.type ~= "receive" or type(event.data) ~= "string" then
			return
		end

		local peerId = event.peer:index()
		local player = self.players[peerId]
		if not player then
			player = makePlayer(peerId, event.peer, { flightModelConfig = self.world.flightModelConfig })
			self.players[peerId] = player
			self.clients[peerId] = event.peer
		end

		local pType = packetType(event.data)
		local kv = parseKv(event.data)
		if pType == "HELLO" then
			self:handleHello(player, kv)
		elseif pType == "AOI_SUBSCRIBE" then
			self:handleAoiSubscribe(player, kv)
		elseif pType == "INPUT" then
			self:handleInput(player, kv)
		elseif pType == "BLOB_REQUEST" then
			local kind = kv.kind or "model"
			local hash = kv.hash
			if not self:fulfillBlobRequest(event.peer, kind, hash) then
				local key = kind .. "|" .. tostring(hash or "")
				self.pendingBlobRequests[key] = self.pendingBlobRequests[key] or {}
				self.pendingBlobRequests[key][peerId] = true
				self:broadcast(event.data, 2, event.peer)
			end
		elseif pType == "BLOB_META" then
			self:handleBlobMeta(event.peer, kv)
		elseif pType == "BLOB_CHUNK" then
			self:handleBlobChunk(event.peer, kv)
		elseif pType == "VOICE_FRAME" then
			for _, other in pairs(self.players) do
				if other.peer and other.id ~= peerId and other.radioChannel == player.radioChannel and
					self:playerWithinAoiRange(other, player, other.aoi and other.aoi.snapshotFarMeters) then
					self:sendToPeer(other.peer, event.data, 3)
				end
			end
		elseif pType == "VOICE_STATE" then
			player.radioChannel = clamp(math.floor(tonumber(kv.channel) or player.radioChannel or 1), 1, 8)
			player.radioTx = packetFlag(kv.tx)
			self:broadcast(encodePacket("VOICE_STATE", {
				{ "id", player.id },
				{ "channel", player.radioChannel },
				{ "tx", player.radioTx and 1 or 0 }
			}), 0, event.peer)
		elseif pType == "CLOUD_REQUEST" then
			self:sendCloudSnapshotToPeer(event.peer)
		elseif pType == "GROUND_REQUEST" then
			self:sendWorldInfo(event.peer)
			self:sendWorldSync(event.peer)
			self:sendChunksForPlayer(player, "ground_request")
		elseif pType == "CRATER_EVENT" then
			self:addCrater(kv)
		end
	end

	function server:simulatePlayer(player, dt)
		local camera = player.camera
		local params = self.world.groundParams
		local env = {
			wind = self.world.wind,
			groundHeightAt = function(x, z)
				return terrainSdfSystem.sampleGroundHeightAtWorld(x, z, params) or 0
			end,
			queryGroundHeight = function(x, z)
				return terrainSdfSystem.queryGroundHeight(x, z, params)
			end,
			sampleSdf = function(x, y, z)
				return terrainSdfSystem.sampleSdfAtWorld(x, y, z, params)
			end,
			sampleNormal = function(x, y, z)
				return terrainSdfSystem.sampleNormalAtWorld(x, y, z, params)
			end,
			groundClearance = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or 1.0,
			collisionRadius = math.max(
				(camera.box and camera.box.halfSize and camera.box.halfSize.x) or 0,
				(camera.box and camera.box.halfSize and camera.box.halfSize.y) or 0,
				(camera.box and camera.box.halfSize and camera.box.halfSize.z) or 0,
				1.0
			)
		}
		local input = player.input
		player.role = sanitizeRole(input.role or player.role)
		if player.role == "plane" then
			camera.throttle = input.throttle or camera.throttle
			camera.yoke.pitch = input.yokePitch or camera.yoke.pitch
			camera.yoke.yaw = input.yokeYaw or camera.yoke.yaw
			camera.yoke.roll = input.yokeRoll or camera.yoke.roll
			camera.yokeLastMouseInputAt = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
			camera = flightDynamics.step(camera, dt, {
				flightThrottleUp = input.flightThrottleUp,
				flightThrottleDown = input.flightThrottleDown,
				flightAirBrakes = input.flightAirBrakes,
				flightAfterburner = input.flightAfterburner
			}, env, self.world.flightModelConfig)
		else
			camera.walkYaw = tonumber(input.walkYaw) or camera.walkYaw or 0
			camera.walkPitch = tonumber(input.walkPitch) or camera.walkPitch or 0
			camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
			camera = engine.processMovement(camera, dt, false, vector3, q, {}, {
				walkForward = input.walkForward,
				walkBackward = input.walkBackward,
				walkStrafeLeft = input.walkStrafeLeft,
				walkStrafeRight = input.walkStrafeRight,
				walkSprint = input.walkSprint,
				walkJump = input.walkJump
			}, env)
		end
		player.camera = camera
	end

	function server:broadcastSnapshots()
		self.snapshotSequence = math.max(0, math.floor(tonumber(self.snapshotSequence) or 0)) + 1
		for _, target in pairs(self.players) do
			if target.peer then
				local nearSq = math.max(128, tonumber(target.aoi and target.aoi.snapshotNearMeters) or 1024)
				nearSq = nearSq * nearSq
				local farSq = math.max(
					math.sqrt(nearSq),
					tonumber(target.aoi and target.aoi.snapshotFarMeters) or 4096
				)
				farSq = farSq * farSq
				for _, player in pairs(self.players) do
					if player.hasReceivedInput then
						local shouldSend = player.id == target.id
						if not shouldSend then
							local distSq = distanceSq2(target.camera and target.camera.pos, player.camera and player.camera.pos)
							if distSq <= nearSq then
								shouldSend = true
							elseif distSq <= farSq then
								shouldSend = (self.snapshotSequence % 3) == 0
							end
						end
						if shouldSend then
							self:sendToPeer(target.peer, buildSnapshotPacket(player, player.lastInputTick), 1)
						end
					end
				end
			end
		end
	end

	function server:resolveCloudCamera()
		for _, player in pairs(self.players) do
			if player and player.camera and player.camera.pos then
				return player.camera
			end
		end
		return self.world.cloudAnchor
	end

	function server:updateCloudAuthority(dt, nowTime)
		local cloudCamera = self:resolveCloudCamera()
		local anchorPos = self.world.cloudAnchor.pos
		local sourcePos = (cloudCamera and cloudCamera.pos) or anchorPos
		anchorPos[1] = tonumber(sourcePos[1]) or anchorPos[1]
		anchorPos[2] = tonumber(sourcePos[2]) or anchorPos[2]
		anchorPos[3] = tonumber(sourcePos[3]) or anchorPos[3]
		self.world.wind = cloudSim.getWindVector3(self.world.windState)
		cloudSim.updateClouds(
			dt,
			self.world.cloudState,
			self.world.cloudAuthorityState,
			self.world.cloudAnchor,
			self.world.windState,
			nowTime,
			q
		)
	end

	function server:update(timeoutMs)
		if not self.host then
			return
		end

		local timeout = clamp(math.floor(tonumber(timeoutMs) or 0), 0, 1000)
		local event = self.host:service(timeout)
		while event do
			self:serviceClientEvent(event)
			event = self.host:service()
		end

		local nowTime = (love and love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
		local dt = clamp(nowTime - (self.lastUpdateAt or nowTime), 0, 0.25)
		self.lastUpdateAt = nowTime
		self.accumulator = self.accumulator + dt
		self.snapshotAccumulator = self.snapshotAccumulator + dt

		local fixedDt = 1 / 60
		local simSteps = 0
		while self.accumulator >= fixedDt and simSteps < 8 do
			self.accumulator = self.accumulator - fixedDt
			simSteps = simSteps + 1
			for _, player in pairs(self.players) do
				self:simulatePlayer(player, fixedDt)
			end
			self:updateCloudAuthority(fixedDt, nowTime)
		end

		if self.snapshotAccumulator >= (1 / 30) then
			self.snapshotAccumulator = self.snapshotAccumulator % (1 / 30)
			self:broadcastSnapshots()
			self:broadcastCloudSnapshot(false)
		end

		if (nowTime - (self.lastWorldFlushAt or nowTime)) >= self.worldFlushInterval then
			local flushed = self.world.worldStore:flushDirty()
			if flushed > 0 then
				safeLog(self.log, string.format("[server] flushed %d dirty world region(s)", flushed))
			end
			self.lastWorldFlushAt = nowTime
		end
	end

	function server:stop()
		if not self.host then
			return
		end
		if self.world and self.world.worldStore and type(self.world.worldStore.flushDirty) == "function" then
			self.world.worldStore:flushDirty()
		end
		for _, player in pairs(self.players) do
			if player.peer then
				pcall(function()
					player.peer:disconnect_now()
				end)
			end
		end
		self.players = {}
		self.clients = {}
		self.host = nil
	end

	safeLog(server.log, string.format("[server] listening on %s", bindAddress))
	safeLog(server.log, string.format(
		"[server] world=%s seed=%d chunk=%d",
		tostring(server.world.worldStore.name),
		math.floor(tonumber(server.world.groundParams.seed) or 0),
		math.floor(tonumber(server.world.groundParams.chunkSize) or 0)
	))
	return server
end

return hostedServer
