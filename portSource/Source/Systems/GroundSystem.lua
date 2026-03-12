local ground = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local defaultStreamingLimits = {
	minGridCount = 24,
	maxGridCount = 512,
	coverageMultiplier = 1.1
}

local function roundUpEven(value)
	local v = math.floor(tonumber(value) or 0)
	if (v % 2) ~= 0 then
		v = v + 1
	end
	return v
end

local function resolveStreamingGridCount(params, drawDistance, limits)
	local tileSize = math.max(0.05, tonumber(params and params.tileSize) or 20)
	local currentGrid = math.max(1, math.floor(tonumber(params and params.gridCount) or 1))
	local distance = tonumber(drawDistance)
	if not distance then
		return currentGrid
	end

	limits = limits or defaultStreamingLimits
	local minGrid = math.max(1, math.floor(tonumber(limits.minGridCount) or defaultStreamingLimits.minGridCount))
	local maxGrid = math.max(minGrid, math.floor(tonumber(limits.maxGridCount) or defaultStreamingLimits.maxGridCount))
	local coverageMultiplier = math.max(1.0,
		tonumber(limits.coverageMultiplier) or defaultStreamingLimits.coverageMultiplier)
	local halfExtent = math.max(tileSize, distance * coverageMultiplier)
	local targetGrid = math.ceil((halfExtent * 2) / tileSize)
	targetGrid = roundUpEven(targetGrid)
	targetGrid = clamp(targetGrid, minGrid, maxGrid)
	return math.max(1, targetGrid)
end

function ground.buildStreamingParams(params, drawDistance, limits)
	local targetGrid = resolveStreamingGridCount(params, drawDistance, limits)
	if targetGrid == (params and params.gridCount) then
		return params, targetGrid
	end
	local derived = {}
	for key, value in pairs(params or {}) do
		derived[key] = value
	end
	derived.gridCount = targetGrid
	return derived, targetGrid
end

function ground.makeRng(seed)
	-- Park-Miller LCG using Schrage method (LuaJIT/Lua 5.1 compatible).
	local m = 2147483647
	local a = 16807
	local q = 127773
	local r = 2836
	local state = math.floor(tonumber(seed) or 1) % m
	if state <= 0 then
		state = state + (m - 1)
	end

	local function u32()
		local hi = math.floor(state / q)
		local lo = state - hi * q
		local test = (a * lo) - (r * hi)
		if test > 0 then
			state = test
		else
			state = test + m
		end
		return state
	end

	return {
		u32 = u32,
		-- [0,1)
		rand01 = function()
			return (u32() - 1) / (m - 1)
		end,
		-- inclusive integers
		randint = function(_, x, y)
			local low = math.floor(tonumber(x) or 0)
			local high = math.floor(tonumber(y) or 0)
			if high < low then
				low, high = high, low
			end
			local span = high - low + 1
			return low + math.floor(((u32() - 1) / (m - 1)) * span)
		end
	}
end

ground.CELL_GRASS = 0
ground.CELL_ROAD = 1
ground.CELL_FIELD = 2

function ground.generateCellGrid(params)
	-- params: seed, gridCount, roadCount, roadDensity(0..1), fieldCount, fieldMinSize, fieldMaxSize
	local rng = ground.makeRng(params.seed or 1)
	local n = params.gridCount
	local grid = {}
	for x = 1, n do
		grid[x] = {}
		for z = 1, n do
			grid[x][z] = ground.CELL_GRASS
		end
	end

	local function inBounds(x, z)
		return x >= 1 and x <= n and z >= 1 and z <= n
	end

	-- --- Roads ---
	local roadCount = math.max(0, math.floor(params.roadCount or 3))
	local roadDensity = clamp(params.roadDensity or 0.15, 0, 1)

	-- total carves roughly proportional to density * cells
	local totalSteps = math.floor(roadDensity * n * n)
	local stepsPerRoad = (roadCount > 0) and math.max(1, math.floor(totalSteps / roadCount)) or 0

	for _ = 1, roadCount do
		-- start on a random edge
		local side = rng:randint(1, 4)
		local x, z
		if side == 1 then x, z = 1, rng:randint(1, n)
		elseif side == 2 then x, z = n, rng:randint(1, n)
		elseif side == 3 then x, z = rng:randint(1, n), 1
		else x, z = rng:randint(1, n), n
		end

		local dirX, dirZ = 0, 0
		local function randomDir()
			local d = rng:randint(1, 4)
			if d == 1 then return 1, 0 end
			if d == 2 then return -1, 0 end
			if d == 3 then return 0, 1 end
			return 0, -1
		end
		dirX, dirZ = randomDir()

		for _ = 1, stepsPerRoad do
			if inBounds(x, z) then
				grid[x][z] = ground.CELL_ROAD
			end

			-- occasionally turn; higher density -> slightly straighter looks better, so keep turn chance modest
			local turnChance = 0.18
			if rng:rand01() < turnChance then
				dirX, dirZ = randomDir()
			end

			x, z = x + dirX, z + dirZ
			if not inBounds(x, z) then
				-- bounce back in bounds
				x = clamp(x, 1, n)
				z = clamp(z, 1, n)
				dirX, dirZ = randomDir()
			end
		end
	end

	-- --- Fields (patches) ---
	local fieldCount = params.fieldCount or 6
	local fieldMinSize = params.fieldMinSize or math.floor(n * 0.8)
	local fieldMaxSize = params.fieldMaxSize or math.floor(n * 2.0)

	for _ = 1, fieldCount do
		local sx, sz = rng:randint(1, n), rng:randint(1, n)
		local targetSize = rng:randint(fieldMinSize, fieldMaxSize)

		local queue = { { sx, sz } }
		local qi = 1
		local placed = 0

		while qi <= #queue and placed < targetSize do
			local x, z = queue[qi][1], queue[qi][2]
			qi = qi + 1

			if inBounds(x, z) and grid[x][z] ~= ground.CELL_ROAD and grid[x][z] ~= ground.CELL_FIELD then
				grid[x][z] = ground.CELL_FIELD
				placed = placed + 1

				-- grow outward randomly
				if rng:rand01() < 0.80 then queue[#queue + 1] = { x + 1, z } end
				if rng:rand01() < 0.80 then queue[#queue + 1] = { x - 1, z } end
				if rng:rand01() < 0.80 then queue[#queue + 1] = { x, z + 1 } end
				if rng:rand01() < 0.80 then queue[#queue + 1] = { x, z - 1 } end
			end
		end
	end

	return grid, rng
end

function ground.cloneColor3(value)
	return { value[1], value[2], value[3] }
end

function ground.sanitizeColor3(value, fallback)
	local out = ground.cloneColor3(fallback)
	if type(value) == "table" then
		out[1] = clamp(tonumber(value[1]) or out[1], 0, 1)
		out[2] = clamp(tonumber(value[2]) or out[2], 0, 1)
		out[3] = clamp(tonumber(value[3]) or out[3], 0, 1)
	end
	return out
end

function ground.normalizeGroundParams(params, defaultGroundParams)
	local src = params or defaultGroundParams
	local defaultWaterRatio = clamp(tonumber(defaultGroundParams.waterRatio) or 0.15, 0, 1)
	local defaultWaterColor = defaultGroundParams.waterColor or defaultGroundParams.waterColour or { 0.1, 0.1, 0.3 }
	local defaultWaterVar = defaultGroundParams.waterVar or { 0.02, 0.02, 0.02 }
	local normalized = {
		seed = math.floor(tonumber(src.seed) or defaultGroundParams.seed),
		tileSize = math.max(0.05, tonumber(src.tileSize) or defaultGroundParams.tileSize),
		gridCount = math.max(1, math.floor(tonumber(src.gridCount) or defaultGroundParams.gridCount)),
		baseHeight = tonumber(src.baseHeight) or defaultGroundParams.baseHeight,
		tileThickness = math.max(0.0001, tonumber(src.tileThickness) or defaultGroundParams.tileThickness),
		curvature = math.max(0, tonumber(src.curvature) or defaultGroundParams.curvature),
		recenterStep = math.max(1, tonumber(src.recenterStep) or defaultGroundParams.recenterStep),
		roadCount = math.max(0, math.floor(tonumber(src.roadCount) or defaultGroundParams.roadCount)),
		waterRatio = clamp(tonumber(src.waterRatio) or defaultWaterRatio, 0, 1),
		roadDensity = clamp(tonumber(src.roadDensity) or defaultGroundParams.roadDensity, 0, 1),
		fieldCount = math.max(0, math.floor(tonumber(src.fieldCount) or defaultGroundParams.fieldCount)),
		fieldMinSize = math.max(1, math.floor(tonumber(src.fieldMinSize) or defaultGroundParams.fieldMinSize)),
		fieldMaxSize = math.max(1, math.floor(tonumber(src.fieldMaxSize) or defaultGroundParams.fieldMaxSize)),
		grassColor = ground.sanitizeColor3(src.grassColor, defaultGroundParams.grassColor),
		waterColor = ground.sanitizeColor3(src.waterColor or src.waterColour, defaultWaterColor),
		roadColor = ground.sanitizeColor3(src.roadColor, defaultGroundParams.roadColor),
		fieldColor = ground.sanitizeColor3(src.fieldColor, defaultGroundParams.fieldColor),
		grassVar = ground.sanitizeColor3(src.grassVar, defaultGroundParams.grassVar),
		waterVar = ground.sanitizeColor3(src.waterVar, defaultWaterVar),
		roadVar = ground.sanitizeColor3(src.roadVar, defaultGroundParams.roadVar),
		fieldVar = ground.sanitizeColor3(src.fieldVar, defaultGroundParams.fieldVar)
	}
	normalized.fieldMaxSize = math.max(normalized.fieldMinSize, normalized.fieldMaxSize)
	return normalized
end

function ground.colorsEqual(a, b)
	local eps = 1e-6
	for i = 1, 3 do
		if math.abs((a[i] or 0) - (b[i] or 0)) > eps then
			return false
		end
	end
	return true
end

function ground.groundParamsEqual(a, b)
	if not a or not b then
		return false
	end
	return a.seed == b.seed and
		a.tileSize == b.tileSize and
		a.gridCount == b.gridCount and
		a.baseHeight == b.baseHeight and
		a.tileThickness == b.tileThickness and
		a.curvature == b.curvature and
		a.recenterStep == b.recenterStep and
		a.roadCount == b.roadCount and
		a.waterRatio == b.waterRatio and
		a.roadDensity == b.roadDensity and
		a.fieldCount == b.fieldCount and
		a.fieldMinSize == b.fieldMinSize and
		a.fieldMaxSize == b.fieldMaxSize and
		ground.colorsEqual(a.grassColor, b.grassColor) and
		ground.colorsEqual(a.waterColor, b.waterColor) and
		ground.colorsEqual(a.roadColor, b.roadColor) and
		ground.colorsEqual(a.fieldColor, b.fieldColor) and
		ground.colorsEqual(a.grassVar, b.grassVar) and
		ground.colorsEqual(a.waterVar, b.waterVar) and
		ground.colorsEqual(a.roadVar, b.roadVar) and
		ground.colorsEqual(a.fieldVar, b.fieldVar)
end

function ground.sampleGroundHeightAtWorld(worldX, worldZ, params)
	local groundParams = params
	local curvature = tonumber(groundParams.curvature) or 0
	local baseHeight = tonumber(groundParams.baseHeight) or 0
	return baseHeight - ((worldX * worldX + worldZ * worldZ) * curvature)
end

local function frac(v)
	return v - math.floor(v)
end

local function hash01(ix, iz, salt, seed)
	local h = math.sin(ix * 127.1 + iz * 311.7 + seed * 13.17 + salt * 17.3) * 43758.5453123
	return frac(h)
end

local function variedColor(base, var, ix, iz, salt, seed)
	return {
		clamp(base[1] + (hash01(ix, iz, salt, seed) * 2 - 1) * var[1], 0, 1),
		clamp(base[2] + (hash01(ix, iz, salt + 11, seed) * 2 - 1) * var[2], 0, 1),
		clamp(base[3] + (hash01(ix, iz, salt + 23, seed) * 2 - 1) * var[3], 0, 1)
	}
end

function ground.sampleGroundColorAtWorld(worldX, worldZ, params)
	if not params then
		return { 0.2, 0.62, 0.22 }
	end

	local tileSize = tonumber(params.tileSize) or 20
	local seed = tonumber(params.seed) or 1
	local tileIndexX = math.floor(worldX / tileSize)
	local tileIndexZ = math.floor(worldZ / tileSize)
	local roadWidth = 0.03 + clamp(tonumber(params.roadDensity) or 0, 0, 1) * 0.28
	local roadFreq = 0.06 + ((tonumber(params.roadCount) or 0) * 0.012)
	local waterChance = clamp(tonumber(params.waterRatio) or 0, 0, 1)
	local fieldChance = clamp((tonumber(params.fieldCount) or 0) / 35, 0.08, 0.45)

	local roadSignalX = math.abs(math.sin((tileIndexX + seed * 0.13) * roadFreq))
	local roadSignalZ = math.abs(math.sin((tileIndexZ - seed * 0.21) * (roadFreq * 1.11)))
	local regionNoise = hash01(math.floor(tileIndexX / 7), math.floor(tileIndexZ / 7), 41, seed)
	local waterRegionNoise = hash01(math.floor(tileIndexX / 5), math.floor(tileIndexZ / 5), 59, seed)
	local waterDetailNoise = hash01(tileIndexX, tileIndexZ, 73, seed)
	local waterSignal = waterRegionNoise * 0.8 + waterDetailNoise * 0.2

	local grass = params.grassColor or { 0.2, 0.62, 0.22 }
	local water = params.waterColor or params.waterColour or { 0.1, 0.1, 0.1 }
	local road = params.roadColor or { 0.1, 0.1, 0.1 }
	local field = params.fieldColor or { 0.35, 0.45, 0.2 }
	local grassVar = params.grassVar or { 0.05, 0.1, 0.05 }
	local waterVar = params.waterVar or { 0.02, 0.02, 0.02 }
	local roadVar = params.roadVar or { 0.02, 0.02, 0.02 }
	local fieldVar = params.fieldVar or { 0.04, 0.06, 0.04 }

	if roadSignalX < roadWidth or roadSignalZ < roadWidth then
		return variedColor(road, roadVar, tileIndexX, tileIndexZ, 3, seed)
	end
	if waterSignal < waterChance then
		return variedColor(water, waterVar, tileIndexX, tileIndexZ, 17, seed)
	end
	if regionNoise < fieldChance then
		return variedColor(field, fieldVar, tileIndexX, tileIndexZ, 7, seed)
	end
	return variedColor(grass, grassVar, tileIndexX, tileIndexZ, 13, seed)
end

function ground.generateGroundMeshModel(params, centerX, centerZ)
	local tileSize = params.tileSize
	local gridCount = params.gridCount
	local half = tileSize / 2
	centerX = centerX or 0
	centerZ = centerZ or 0

	local vertices, faces = {}, {}
	local vertexColors, faceColors = {}, {}

	for gx = 1, gridCount do
		for gz = 1, gridCount do
			local localTileX = (gx - 1) - gridCount / 2
			local localTileZ = (gz - 1) - gridCount / 2
			local posX = localTileX * tileSize + half
			local posZ = localTileZ * tileSize + half
			local worldCenterX = centerX + posX
			local worldCenterZ = centerZ + posZ
			local c = ground.sampleGroundColorAtWorld(worldCenterX, worldCenterZ, params)
			local rgba = { c[1], c[2], c[3], 1.0 }

			local x0 = posX - half
			local x1 = posX + half
			local z0 = posZ - half
			local z1 = posZ + half
			local y00 = ground.sampleGroundHeightAtWorld(centerX + x0, centerZ + z0, params)
			local y10 = ground.sampleGroundHeightAtWorld(centerX + x1, centerZ + z0, params)
			local y11 = ground.sampleGroundHeightAtWorld(centerX + x1, centerZ + z1, params)
			local y01 = ground.sampleGroundHeightAtWorld(centerX + x0, centerZ + z1, params)

			local base = #vertices
			vertices[base + 1] = { x0, y00, z0 }
			vertices[base + 2] = { x1, y10, z0 }
			vertices[base + 3] = { x1, y11, z1 }
			vertices[base + 4] = { x0, y01, z1 }

			vertexColors[base + 1] = rgba
			vertexColors[base + 2] = rgba
			vertexColors[base + 3] = rgba
			vertexColors[base + 4] = rgba

			faces[#faces + 1] = { base + 1, base + 2, base + 3 }
			faceColors[#faceColors + 1] = rgba
			faces[#faces + 1] = { base + 1, base + 3, base + 4 }
			faceColors[#faceColors + 1] = rgba
		end
	end

	return {
		vertices = vertices,
		faces = faces,
		vertexColors = vertexColors,
		faceColors = faceColors,
		isSolid = true
	}
end

function ground.createGroundObject(params, centerX, centerZ, q)
	centerX = centerX or 0
	centerZ = centerZ or 0
	local halfExtent = (params.gridCount * params.tileSize) * 0.5
	return {
		model = ground.generateGroundMeshModel(params, centerX, centerZ),
		pos = { centerX, 0, centerZ },
		rot = q.identity(),
		scale = { 1, 1, 1 },
		color = { 1, 1, 1, 1 },
		isSolid = true,
		isGround = true,
		halfSize = { x = halfExtent, y = params.tileThickness, z = halfExtent }
	}
end

function ground.updateGroundStreaming(forceRebuild, context)
	local groundObject = context.groundObject
	local activeGroundParams = context.activeGroundParams
	local camera = context.camera

	if not groundObject or not activeGroundParams or not camera then
		return false, groundObject
	end

	local streamParams, streamGridCount = ground.buildStreamingParams(
		activeGroundParams,
		context.drawDistance,
		context.streamingLimits
	)

	local centerX = groundObject.pos[1] or 0
	local centerZ = groundObject.pos[3] or 0
	local halfExtent = (streamParams.gridCount * streamParams.tileSize) * 0.5
	local threshold = halfExtent * 0.3
	local currentStreamGrid = math.max(
		1,
		math.floor(tonumber(groundObject.streamGridCount) or tonumber(activeGroundParams.gridCount) or 1)
	)
	local streamGridChanged = streamGridCount ~= currentStreamGrid
	local needRecentering = forceRebuild or
		streamGridChanged or
		(math.abs(camera.pos[1] - centerX) > threshold) or
		(math.abs(camera.pos[3] - centerZ) > threshold)
	if not needRecentering then
		return false, groundObject
	end

	local step = math.max(streamParams.tileSize, streamParams.recenterStep or 1)
	local snappedX = math.floor((camera.pos[1] / step) + 0.5) * step
	local snappedZ = math.floor((camera.pos[3] / step) + 0.5) * step
	if (not forceRebuild) and (not streamGridChanged) and
		math.abs(snappedX - centerX) < 1e-6 and
		math.abs(snappedZ - centerZ) < 1e-6 then
		return false, groundObject
	end

	groundObject.model = ground.generateGroundMeshModel(streamParams, snappedX, snappedZ)
	groundObject.pos[1] = snappedX
	groundObject.pos[2] = 0
	groundObject.pos[3] = snappedZ
	groundObject.streamGridCount = streamGridCount
	groundObject.halfSize.x = halfExtent
	groundObject.halfSize.z = halfExtent
	return true, groundObject
end

function ground.rebuildGroundFromParams(params, reason, context)
	local normalized = ground.normalizeGroundParams(params, context.defaultGroundParams)
	if context.activeGroundParams and ground.groundParamsEqual(context.activeGroundParams, normalized) then
		return false
	end

	if context.groundObject then
		for i = #context.objects, 1, -1 do
			if context.objects[i] == context.groundObject or context.objects[i].isGround then
				table.remove(context.objects, i)
			end
		end
	end

	local step = math.max(normalized.tileSize, normalized.recenterStep or 1)
	local centerX = 0
	local centerZ = 0
	if context.camera and context.camera.pos then
		centerX = math.floor((context.camera.pos[1] / step) + 0.5) * step
		centerZ = math.floor((context.camera.pos[3] / step) + 0.5) * step
	end

	local streamParams, streamGridCount = ground.buildStreamingParams(
		normalized,
		context.drawDistance,
		context.streamingLimits
	)
	local groundObject = ground.createGroundObject(streamParams, centerX, centerZ, context.q)
	groundObject.streamGridCount = streamGridCount
	local insertIndex = 1
	if context.localPlayerObject and context.objects[1] == context.localPlayerObject then
		insertIndex = 2
	end
	table.insert(context.objects, insertIndex, groundObject)

	local worldHalfExtent = (streamParams.tileSize * streamParams.gridCount) * 0.5
	context.mapState.zoomExtents = { 160, 420, math.max(worldHalfExtent, 420) }
	context.mapState.logicalCamera = nil
	if reason and reason ~= "" and context.log then
		context.log(string.format(
			"Ground rebuilt (%s): seed=%d tile=%.3f grid=%d (stream=%d)",
			reason,
			normalized.seed,
			normalized.tileSize,
			normalized.gridCount,
			streamGridCount
		))
	end

	return true, {
		activeGroundParams = normalized,
		groundObject = groundObject,
		worldHalfExtent = worldHalfExtent
	}
end

return ground
