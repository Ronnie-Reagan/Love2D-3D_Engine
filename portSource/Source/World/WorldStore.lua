local userProfileStore = require("Source.Core.UserProfileStore")
local sdfField = require("Source.Sim.SdfTerrainField")

local worldStore = {}

local FORMAT_VERSION = 1
local DEFAULT_REGION_SIZE = 16
local DEFAULT_CHUNK_RESOLUTION = 16

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function shallowCopy(src)
	local out = {}
	for key, value in pairs(src or {}) do
		out[key] = value
	end
	return out
end

local function deepCopy(value)
	if type(value) ~= "table" then
		return value
	end
	local out = {}
	for key, entry in pairs(value) do
		out[key] = deepCopy(entry)
	end
	return out
end

local function sanitizeWorldName(value)
	local text = tostring(value or "default"):lower()
	text = text:gsub("[^%w_%-]", "_")
	text = text:gsub("_+", "_")
	text = text:gsub("^_+", ""):gsub("_+$", "")
	if text == "" then
		return "default"
	end
	return text
end

local function buildChunkKey(cx, cz)
	return tostring(math.floor(cx or 0)) .. ":" .. tostring(math.floor(cz or 0))
end

local function buildRegionKey(rx, rz)
	return tostring(math.floor(rx or 0)) .. ":" .. tostring(math.floor(rz or 0))
end

local function floorDiv(value, divisor)
	return math.floor((tonumber(value) or 0) / math.max(1, tonumber(divisor) or 1))
end

local function chunkToRegion(cx, cz, regionSize)
	local size = math.max(1, math.floor(tonumber(regionSize) or DEFAULT_REGION_SIZE))
	return floorDiv(cx, size), floorDiv(cz, size)
end

local function regionPath(rootPath, rx, rz)
	return string.format("%s/regions/%d_%d.lua", tostring(rootPath), math.floor(rx or 0), math.floor(rz or 0))
end

local function metaPath(rootPath)
	return tostring(rootPath) .. "/meta.lua"
end

local function ensureDirectory(path)
	local loveLib = rawget(_G, "love")
	if not (loveLib and loveLib.filesystem and loveLib.filesystem.createDirectory) then
		return false, "love filesystem unavailable"
	end
	local ok, err = pcall(function()
		loveLib.filesystem.createDirectory(path)
	end)
	if not ok then
		return false, tostring(err)
	end
	return true
end

local function frac(v)
	return v - math.floor(v)
end

local function hash01(a, b, c, seed)
	local n = math.sin((a * 127.1) + (b * 311.7) + (c * 73.13) + (seed * 19.97)) * 43758.5453123
	return frac(n)
end

local function lerp(a, b, t)
	return a + (b - a) * t
end

local function normalizeVec3(x, y, z)
	local len = math.sqrt(x * x + y * y + z * z)
	if len <= 1e-8 then
		return 0, 0, 1
	end
	return x / len, y / len, z / len
end

local function buildHillTunnelSeeds(params)
	params = params or {}
	local count = math.max(0, math.floor(tonumber(params.tunnelCount) or 0))
	if count <= 0 then
		return {}
	end

	local baseParams = shallowCopy(params)
	baseParams.tunnelCount = 0
	baseParams.dynamicCraters = {}
	local context = sdfField.createContext(baseParams)
	local seed = math.floor(tonumber(baseParams.seed) or 1)
	local worldRadius = math.max(512, tonumber(baseParams.worldRadius) or 2048)
	local minRadius = math.max(4, tonumber(baseParams.tunnelRadiusMin) or 9)
	local maxRadius = math.max(minRadius, tonumber(baseParams.tunnelRadiusMax) or 18)
	local minLength = math.max(128, tonumber(baseParams.tunnelLengthMin) or 240)
	local maxLength = math.max(minLength, tonumber(baseParams.tunnelLengthMax) or 520)
	local baseHeight = tonumber(baseParams.baseHeight) or 0
	local waterLevel = tonumber(baseParams.waterLevel) or -12
	local chunkSize = math.max(16, tonumber(baseParams.chunkSize) or 128)
	local out = {}
	local attempts = math.max(12, count * 18)

	for i = 1, attempts do
		if #out >= count then
			break
		end
		local idx = i * 17
		local cx = lerp(-worldRadius * 0.78, worldRadius * 0.78, hash01(idx, 2, 5, seed))
		local cz = lerp(-worldRadius * 0.78, worldRadius * 0.78, hash01(idx, 7, 11, seed))
		local surfaceY = sdfField.sampleSurfaceHeight(cx, cz, context)
		local hillHeight = surfaceY - math.max(baseHeight, waterLevel)
		if hillHeight >= 30 then
			local heading = lerp(0, math.pi * 2, hash01(idx, 13, 17, seed))
			local length = lerp(minLength, maxLength, hash01(idx, 19, 23, seed))
			local radius = lerp(minRadius, maxRadius, hash01(idx, 29, 31, seed))
			local depth = lerp(radius * 1.8, radius * 3.4, hash01(idx, 37, 41, seed))
			local wobbleAmp = lerp(radius * 0.4, radius * 1.4, hash01(idx, 43, 47, seed))
			local wobbleFreq = lerp(0.006, 0.018, hash01(idx, 53, 59, seed))
			local points = {}
			local steps = math.max(5, math.floor(length / math.max(24, chunkSize * 0.35)))
			local covered = 0

			for step = 0, steps do
				local t = step / steps
				local dist = (t - 0.5) * length
				local curve = math.sin((dist / math.max(1, length)) * math.pi)
				local wobbleX = math.sin(dist * wobbleFreq + i * 0.91) * wobbleAmp
				local wobbleY = math.sin(dist * wobbleFreq * 0.57 + i * 1.13) * (radius * 0.35)
				local wobbleZ = math.cos(dist * wobbleFreq + i * 0.47) * wobbleAmp
				local dirX, _, dirZ = normalizeVec3(math.cos(heading), 0, math.sin(heading))
				local px = cx + dirX * dist + wobbleX
				local pz = cz + dirZ * dist + wobbleZ
				local py = surfaceY - depth + wobbleY + curve * radius * 0.22
				local localSurface = sdfField.sampleSurfaceHeight(px, pz, context)
				points[#points + 1] = { px, py, pz }
				if localSurface >= (py + radius + 6.0) then
					covered = covered + 1
				end
			end

			if covered >= math.max(4, math.floor(#points * 0.65)) then
				out[#out + 1] = {
					radius = radius,
					points = points,
					hillAttached = true
				}
			end
		end
	end

	return out
end

local function normalizeChunkResolution(value)
	return clamp(math.floor(tonumber(value) or DEFAULT_CHUNK_RESOLUTION), 4, 64)
end

local function deltaGridLength(resolution)
	local axis = resolution + 1
	return axis * axis
end

local function buildEmptyChunk(cx, cz, resolution)
	local res = normalizeChunkResolution(resolution)
	local deltas = {}
	for i = 1, deltaGridLength(res) do
		deltas[i] = 0
	end
	return {
		cx = math.floor(cx or 0),
		cz = math.floor(cz or 0),
		resolution = res,
		revision = 0,
		materialRevision = 0,
		heightDeltas = deltas,
		volumetricOverrides = {}
	}
end

local function normalizeChunkState(chunk, fallbackResolution)
	local resolution = normalizeChunkResolution(chunk and chunk.resolution or fallbackResolution)
	local normalized = buildEmptyChunk(
		chunk and chunk.cx or 0,
		chunk and chunk.cz or 0,
		resolution
	)
	normalized.revision = math.max(0, math.floor(tonumber(chunk and chunk.revision) or 0))
	normalized.materialRevision = math.max(0, math.floor(tonumber(chunk and chunk.materialRevision) or 0))
	if type(chunk and chunk.heightDeltas) == "table" then
		for i = 1, math.min(#normalized.heightDeltas, #chunk.heightDeltas) do
			normalized.heightDeltas[i] = tonumber(chunk.heightDeltas[i]) or 0
		end
	end
	if type(chunk and chunk.volumetricOverrides) == "table" then
		for i = 1, #chunk.volumetricOverrides do
			local override = chunk.volumetricOverrides[i]
			if type(override) == "table" then
				normalized.volumetricOverrides[#normalized.volumetricOverrides + 1] = deepCopy(override)
			end
		end
	end
	return normalized
end

local function chunkHasMeaningfulData(chunk)
	if type(chunk) ~= "table" then
		return false
	end
	if math.max(0, math.floor(tonumber(chunk.revision) or 0)) > 0 then
		return true
	end
	if type(chunk.heightDeltas) == "table" then
		for i = 1, #chunk.heightDeltas do
			if math.abs(tonumber(chunk.heightDeltas[i]) or 0) > 1e-6 then
				return true
			end
		end
	end
	return type(chunk.volumetricOverrides) == "table" and #chunk.volumetricOverrides > 0
end

local function markRegionDirty(world, rx, rz)
	local key = buildRegionKey(rx, rz)
	world.dirtyRegions[key] = { rx = rx, rz = rz }
end

local function loadRegionPayload(path)
	local payload = userProfileStore.load(path)
	if type(payload) ~= "table" then
		return {
			formatVersion = FORMAT_VERSION,
			chunks = {}
		}
	end
	payload.formatVersion = math.max(1, math.floor(tonumber(payload.formatVersion) or FORMAT_VERSION))
	payload.chunks = type(payload.chunks) == "table" and payload.chunks or {}
	return payload
end

local function saveRegionPayload(path, payload)
	payload = payload or {}
	payload.formatVersion = FORMAT_VERSION
	payload.chunks = payload.chunks or {}
	return userProfileStore.save(payload, path)
end

local function chunkBounds(chunkSize, cx, cz)
	local size = math.max(1, tonumber(chunkSize) or 128)
	local x0 = cx * size
	local z0 = cz * size
	return x0, z0, x0 + size, z0 + size
end

local function clamp01(value)
	return clamp(tonumber(value) or 0, 0, 1)
end

local function sampleVolumetricOverrideSdf(x, y, z, overrides)
	if type(overrides) ~= "table" then
		return math.huge
	end
	local minDistance = math.huge
	for i = 1, #overrides do
		local override = overrides[i]
		if type(override) == "table" then
			local kind = tostring(override.kind or "sphere")
			if kind == "sphere" then
				local dx = x - (tonumber(override.x) or 0)
				local dy = y - (tonumber(override.y) or 0)
				local dz = z - (tonumber(override.z) or 0)
				local radius = math.max(0.1, tonumber(override.radius) or 1.0)
				local distance = math.sqrt(dx * dx + dy * dy + dz * dz) - radius
				if distance < minDistance then
					minDistance = distance
				end
			end
		end
	end
	return minDistance
end

function worldStore.open(opts)
	opts = opts or {}
	local rootName = sanitizeWorldName(opts.name or os.getenv("L2D3D_WORLD_NAME") or "default")
	local rootPath = "worlds/" .. rootName
	local regionSize = clamp(math.floor(tonumber(opts.regionSize) or DEFAULT_REGION_SIZE), 4, 64)
	local chunkResolution = normalizeChunkResolution(opts.chunkResolution or DEFAULT_CHUNK_RESOLUTION)
	local createIfMissing = opts.createIfMissing
	if createIfMissing == nil then
		createIfMissing = tostring(os.getenv("L2D3D_WORLD_CREATE") or "1") ~= "0"
	end

	ensureDirectory("worlds")
	ensureDirectory(rootPath)
	ensureDirectory(rootPath .. "/regions")

	local loadedMeta = userProfileStore.load(metaPath(rootPath))
	local meta = type(loadedMeta) == "table" and loadedMeta or nil
	local groundParams = shallowCopy(opts.groundParams or {})
	if not meta then
		if not createIfMissing then
			return nil, "world not found"
		end
		local tunnelSeeds = buildHillTunnelSeeds(groundParams)
		meta = {
			worldId = rootName,
			formatVersion = FORMAT_VERSION,
			seed = math.floor(tonumber(groundParams.seed) or 1),
			terrainProfile = {
				chunkSize = math.max(8, tonumber(groundParams.chunkSize) or 128),
				worldRadius = math.max(512, tonumber(groundParams.worldRadius) or 2048),
				heightAmplitude = tonumber(groundParams.heightAmplitude) or 120,
				heightFrequency = tonumber(groundParams.heightFrequency) or 0.0018,
				waterLevel = tonumber(groundParams.waterLevel) or -12
			},
			spawn = deepCopy(opts.spawn or { x = 0, y = 0, z = 0 }),
			tunnelProfile = {
				count = #tunnelSeeds,
				radiusMin = tonumber(groundParams.tunnelRadiusMin) or 9,
				radiusMax = tonumber(groundParams.tunnelRadiusMax) or 18,
				lengthMin = tonumber(groundParams.tunnelLengthMin) or 240,
				lengthMax = tonumber(groundParams.tunnelLengthMax) or 520,
				hillAttached = true
			},
			tunnelSeeds = tunnelSeeds,
			chunkResolution = chunkResolution,
			regionSize = regionSize,
			createdAt = os.date("!%Y-%m-%dT%H:%M:%SZ"),
			updatedAt = os.date("!%Y-%m-%dT%H:%M:%SZ")
		}
		userProfileStore.save(meta, metaPath(rootPath))
	end

	meta.formatVersion = math.max(1, math.floor(tonumber(meta.formatVersion) or FORMAT_VERSION))
	meta.chunkResolution = normalizeChunkResolution(meta.chunkResolution or chunkResolution)
	meta.regionSize = clamp(math.floor(tonumber(meta.regionSize) or regionSize), 4, 64)
	meta.terrainProfile = type(meta.terrainProfile) == "table" and meta.terrainProfile or {}
	meta.spawn = type(meta.spawn) == "table" and meta.spawn or { x = 0, y = 0, z = 0 }
	meta.tunnelProfile = type(meta.tunnelProfile) == "table" and meta.tunnelProfile or {}
	meta.tunnelSeeds = type(meta.tunnelSeeds) == "table" and meta.tunnelSeeds or buildHillTunnelSeeds(groundParams)

	local world = {
		name = rootName,
		rootPath = rootPath,
		meta = meta,
		regionSize = meta.regionSize,
		chunkResolution = meta.chunkResolution,
		chunkSize = math.max(8, tonumber((opts.groundParams and opts.groundParams.chunkSize) or meta.terrainProfile.chunkSize) or 128),
		chunks = {},
		loadedRegions = {},
		dirtyRegions = {},
		lastFlushAt = -math.huge
	}

	local function loadRegion(rx, rz)
		local key = buildRegionKey(rx, rz)
		if world.loadedRegions[key] then
			return world.loadedRegions[key]
		end
		local path = regionPath(world.rootPath, rx, rz)
		local payload = loadRegionPayload(path)
		local region = {
			rx = rx,
			rz = rz,
			path = path,
			chunks = {}
		}
		for chunkKey, chunkState in pairs(payload.chunks or {}) do
			local chunk = normalizeChunkState(chunkState, world.chunkResolution)
			region.chunks[chunkKey] = chunk
			world.chunks[chunkKey] = chunk
		end
		world.loadedRegions[key] = region
		return region
	end

	local function getChunk(cx, cz, create)
		local key = buildChunkKey(cx, cz)
		local chunk = world.chunks[key]
		if chunk then
			return chunk
		end
		local rx, rz = chunkToRegion(cx, cz, world.regionSize)
		local region = loadRegion(rx, rz)
		chunk = region.chunks[key]
		if chunk then
			world.chunks[key] = chunk
			return chunk
		end
		if not create then
			return nil
		end
		chunk = buildEmptyChunk(cx, cz, world.chunkResolution)
		region.chunks[key] = chunk
		world.chunks[key] = chunk
		markRegionDirty(world, rx, rz)
		return chunk
	end

	local function serializeChunk(chunk)
		local normalized = normalizeChunkState(chunk, world.chunkResolution)
		return {
			cx = normalized.cx,
			cz = normalized.cz,
			resolution = normalized.resolution,
			revision = normalized.revision,
			materialRevision = normalized.materialRevision,
			heightDeltas = deepCopy(normalized.heightDeltas),
			volumetricOverrides = deepCopy(normalized.volumetricOverrides)
		}
	end

	function world:getMeta()
		return deepCopy(self.meta)
	end

	function world:getChunk(cx, cz, create)
		return getChunk(cx, cz, create)
	end

	function world:getChunkState(cx, cz)
		local chunk = getChunk(cx, cz, false)
		if not chunk then
			return nil
		end
		return serializeChunk(chunk)
	end

	function world:applyWorldInfo(info)
		if type(info) ~= "table" then
			return false
		end
		self.meta.worldId = tostring(info.worldId or self.meta.worldId or self.name)
		self.meta.formatVersion = math.max(1, math.floor(tonumber(info.formatVersion) or self.meta.formatVersion or FORMAT_VERSION))
		self.meta.seed = math.floor(tonumber(info.seed) or self.meta.seed or 1)
		self.meta.terrainProfile = type(self.meta.terrainProfile) == "table" and self.meta.terrainProfile or {}
		self.meta.terrainProfile.chunkSize = math.max(
			8,
			tonumber(info.chunkSize) or tonumber(self.meta.terrainProfile.chunkSize) or self.chunkSize or 128
		)
		self.meta.terrainProfile.worldRadius = math.max(
			self.meta.terrainProfile.chunkSize * 4,
			tonumber(info.horizonRadiusMeters) or tonumber(self.meta.terrainProfile.worldRadius) or 2048
		)
		self.meta.terrainProfile.heightAmplitude = tonumber(info.heightAmplitude) or
			tonumber(self.meta.terrainProfile.heightAmplitude) or 120
		self.meta.terrainProfile.heightFrequency = tonumber(info.heightFrequency) or
			tonumber(self.meta.terrainProfile.heightFrequency) or 0.0018
		self.meta.terrainProfile.waterLevel = tonumber(info.waterLevel) or
			tonumber(self.meta.terrainProfile.waterLevel) or -12
		self.meta.spawn = {
			x = tonumber(info.spawnX) or tonumber(self.meta.spawn and self.meta.spawn.x) or 0,
			y = tonumber(info.spawnY) or tonumber(self.meta.spawn and self.meta.spawn.y) or 0,
			z = tonumber(info.spawnZ) or tonumber(self.meta.spawn and self.meta.spawn.z) or 0
		}
		if type(info.tunnelSeeds) == "table" then
			self.meta.tunnelSeeds = deepCopy(info.tunnelSeeds)
		end
		self.meta.tunnelProfile = type(self.meta.tunnelProfile) == "table" and self.meta.tunnelProfile or {}
		self.meta.tunnelProfile.count = math.floor(tonumber(info.tunnelCount) or #(self.meta.tunnelSeeds or {}))
		self.meta.tunnelProfile.hillAttached = true
		self.chunkSize = math.max(8, tonumber(self.meta.terrainProfile.chunkSize) or self.chunkSize or 128)
		self.meta.updatedAt = os.date("!%Y-%m-%dT%H:%M:%SZ")
		userProfileStore.save(self.meta, metaPath(self.rootPath))
		return true
	end

	function world:applyChunkState(chunkState)
		if type(chunkState) ~= "table" then
			return false
		end
		local cx = math.floor(tonumber(chunkState.cx) or 0)
		local cz = math.floor(tonumber(chunkState.cz) or 0)
		local current = getChunk(cx, cz, false)
		local incomingRevision = math.max(0, math.floor(tonumber(chunkState.revision) or 0))
		local currentRevision = math.max(0, math.floor(tonumber(current and current.revision) or 0))
		if current and incomingRevision < currentRevision then
			return false
		end
		local chunk = normalizeChunkState(chunkState, self.chunkResolution)
		local key = buildChunkKey(cx, cz)
		local rx, rz = chunkToRegion(cx, cz, self.regionSize)
		local region = loadRegion(rx, rz)
		region.chunks[key] = chunk
		self.chunks[key] = chunk
		markRegionDirty(self, rx, rz)
		self.meta.updatedAt = os.date("!%Y-%m-%dT%H:%M:%SZ")
		return true
	end

	function world:sampleHeightDelta(x, z)
		local chunkSize = self.chunkSize
		local resolution = self.chunkResolution
		local axis = resolution + 1
		local cx = math.floor((tonumber(x) or 0) / chunkSize)
		local cz = math.floor((tonumber(z) or 0) / chunkSize)
		local chunk = getChunk(cx, cz, false)
		if not chunk then
			return 0
		end
		local localX = ((tonumber(x) or 0) - (cx * chunkSize)) / chunkSize
		local localZ = ((tonumber(z) or 0) - (cz * chunkSize)) / chunkSize
		local fx = clamp01(localX) * resolution
		local fz = clamp01(localZ) * resolution
		local ix = clamp(math.floor(fx), 0, resolution)
		local iz = clamp(math.floor(fz), 0, resolution)
		local tx = clamp01(fx - ix)
		local tz = clamp01(fz - iz)
		local ix1 = math.min(resolution, ix + 1)
		local iz1 = math.min(resolution, iz + 1)
		local function gridValue(gx, gz)
			local index = gz * axis + gx + 1
			return tonumber(chunk.heightDeltas[index]) or 0
		end
		local v00 = gridValue(ix, iz)
		local v10 = gridValue(ix1, iz)
		local v01 = gridValue(ix, iz1)
		local v11 = gridValue(ix1, iz1)
		local a = v00 + (v10 - v00) * tx
		local b = v01 + (v11 - v01) * tx
		return a + (b - a) * tz
	end

	function world:sampleVolumetricOverrideSdf(x, y, z)
		local cx = math.floor((tonumber(x) or 0) / self.chunkSize)
		local cz = math.floor((tonumber(z) or 0) / self.chunkSize)
		local minDistance = math.huge
		for dz = -1, 1 do
			for dx = -1, 1 do
				local chunk = getChunk(cx + dx, cz + dz, false)
				if chunk and type(chunk.volumetricOverrides) == "table" and #chunk.volumetricOverrides > 0 then
					local d = sampleVolumetricOverrideSdf(x, y, z, chunk.volumetricOverrides)
					if d < minDistance then
						minDistance = d
					end
				end
			end
		end
		return minDistance
	end

	function world:collectEditedChunks(centerCx, centerCz, radiusChunks)
		local out = {}
		local radius = math.max(0, math.floor(tonumber(radiusChunks) or 0))
		for dz = -radius, radius do
			for dx = -radius, radius do
				local cx = math.floor(centerCx or 0) + dx
				local cz = math.floor(centerCz or 0) + dz
				local chunk = getChunk(cx, cz, false)
				if chunk and chunkHasMeaningfulData(chunk) then
					out[#out + 1] = serializeChunk(chunk)
				end
			end
		end
		return out
	end

	function world:applyCrater(craterSpec)
		if type(craterSpec) ~= "table" then
			return false, {}
		end
		local chunkSize = self.chunkSize
		local radius = math.max(1.0, tonumber(craterSpec.radius) or 8.0)
		local depth = math.max(0.4, tonumber(craterSpec.depth) or (radius * 0.45))
		local rim = clamp(tonumber(craterSpec.rim) or 0.12, 0.0, 0.75)
		local centerX = tonumber(craterSpec.x) or 0
		local centerZ = tonumber(craterSpec.z) or 0
		local minCx = math.floor((centerX - radius * 1.4) / chunkSize)
		local maxCx = math.floor((centerX + radius * 1.4) / chunkSize)
		local minCz = math.floor((centerZ - radius * 1.4) / chunkSize)
		local maxCz = math.floor((centerZ + radius * 1.4) / chunkSize)
		local changed = {}

		for cz = minCz, maxCz do
			for cx = minCx, maxCx do
				local chunk = getChunk(cx, cz, true)
				local resolution = chunk.resolution or self.chunkResolution
				local axis = resolution + 1
				local x0, z0 = chunkBounds(chunkSize, cx, cz)
				local touched = false
				for gz = 0, resolution do
					local worldZ = z0 + (gz / resolution) * chunkSize
					for gx = 0, resolution do
						local worldX = x0 + (gx / resolution) * chunkSize
						local dx = worldX - centerX
						local dz = worldZ - centerZ
						local dist = math.sqrt(dx * dx + dz * dz)
						local t = dist / radius
						local delta = 0
						if t < 1.0 then
							local bowl = 1.0 - (t * t)
							delta = -depth * bowl * bowl
						elseif t < 1.24 then
							local rimT = (t - 1.0) / 0.24
							local rimAlpha = 1.0 - rimT
							delta = radius * rim * rimAlpha * rimAlpha
						end
						if math.abs(delta) > 1e-6 then
							local index = gz * axis + gx + 1
							chunk.heightDeltas[index] = (tonumber(chunk.heightDeltas[index]) or 0) + delta
							touched = true
						end
					end
				end
				if touched then
					chunk.revision = math.max(0, math.floor(tonumber(chunk.revision) or 0)) + 1
					chunk.materialRevision = math.max(0, math.floor(tonumber(chunk.materialRevision) or 0)) + 1
					local rx, rz = chunkToRegion(cx, cz, self.regionSize)
					markRegionDirty(self, rx, rz)
					changed[#changed + 1] = serializeChunk(chunk)
				end
			end
		end

		if #changed > 0 then
			self.meta.updatedAt = os.date("!%Y-%m-%dT%H:%M:%SZ")
			userProfileStore.save(self.meta, metaPath(self.rootPath))
			return true, changed
		end
		return false, {}
	end

	function world:flushDirty()
		local flushed = 0
		for key, regionInfo in pairs(self.dirtyRegions) do
			local rx = regionInfo.rx
			local rz = regionInfo.rz
			local region = loadRegion(rx, rz)
			local payload = {
				formatVersion = FORMAT_VERSION,
				chunks = {}
			}
			for chunkKey, chunk in pairs(region.chunks or {}) do
				if chunkHasMeaningfulData(chunk) then
					payload.chunks[chunkKey] = serializeChunk(chunk)
				end
			end
			saveRegionPayload(region.path, payload)
			self.dirtyRegions[key] = nil
			flushed = flushed + 1
		end
		if flushed > 0 then
			userProfileStore.save(self.meta, metaPath(self.rootPath))
		end
		self.lastFlushAt = os.clock()
		return flushed
	end

	function world:buildGroundParams(baseParams)
		local out = shallowCopy(baseParams or {})
		out.seed = math.floor(tonumber(self.meta.seed) or tonumber(out.seed) or 1)
		out.chunkSize = math.max(8, tonumber(self.meta.terrainProfile.chunkSize) or tonumber(out.chunkSize) or 128)
		out.worldRadius = math.max(out.chunkSize * 4, tonumber(self.meta.terrainProfile.worldRadius) or tonumber(out.worldRadius) or 2048)
		out.heightAmplitude = tonumber(self.meta.terrainProfile.heightAmplitude) or tonumber(out.heightAmplitude) or 120
		out.heightFrequency = tonumber(self.meta.terrainProfile.heightFrequency) or tonumber(out.heightFrequency) or 0.0018
		out.waterLevel = tonumber(self.meta.terrainProfile.waterLevel) or tonumber(out.waterLevel) or -12
		out.worldStore = self
		out.worldId = tostring(self.meta.worldId or self.name)
		out.worldFormatVersion = math.max(1, math.floor(tonumber(self.meta.formatVersion) or FORMAT_VERSION))
		out.tunnelSeedCount = #(self.meta.tunnelSeeds or {})
		out.tunnelSeeds = deepCopy(self.meta.tunnelSeeds)
		return out
	end

	return world
end

return worldStore
