local sdfField = require "Source.Sim.SdfTerrainField"
local marching = require "Source.Sim.MarchingCubes"
local terrainCollision = require "Source.Sim.TerrainCollision"

local terrain = {}
local loveLib = rawget(_G, "love")

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

local function cloneCraterList(list)
	local out = {}
	if type(list) ~= "table" then
		return out
	end
	for i = 1, #list do
		local crater = list[i]
		if type(crater) == "table" then
			out[#out + 1] = {
				x = tonumber(crater.x) or 0,
				y = tonumber(crater.y) or 0,
				z = tonumber(crater.z) or 0,
				radius = math.max(1.0, tonumber(crater.radius) or 8.0),
				depth = math.max(0.4, tonumber(crater.depth) or 3.0),
				rim = clamp(tonumber(crater.rim) or 0.12, 0.0, 0.75)
			}
		end
	end
	return out
end

local function copyColor3(v, fallback)
	local src = type(v) == "table" and v or fallback or { 0.3, 0.4, 0.25 }
	return {
		clamp(tonumber(src[1]) or 0, 0, 1),
		clamp(tonumber(src[2]) or 0, 0, 1),
		clamp(tonumber(src[3]) or 0, 0, 1)
	}
end

local function supportsThreading()
	return type(loveLib) == "table" and
		type(loveLib.thread) == "table" and
		type(loveLib.thread.newThread) == "function" and
		type(loveLib.thread.getChannel) == "function"
end

local function buildChunkKey(cx, cz, lod, generatorVersion, seed)
	return table.concat({
		tostring(seed or 0),
		tostring(cx),
		tostring(cz),
		tostring(lod),
		tostring(generatorVersion or 1)
	}, ":")
end

local function parseChunkKey(key)
	local parts = {}
	for token in tostring(key):gmatch("[^:]+") do
		parts[#parts + 1] = token
	end
	if #parts >= 5 then
		return tonumber(parts[2]) or 0, tonumber(parts[3]) or 0, tonumber(parts[4]) or 0
	end
	return tonumber(parts[1]) or 0, tonumber(parts[2]) or 0, tonumber(parts[3]) or 0
end

local function countTableKeys(t)
	local n = 0
	for _ in pairs(t or {}) do
		n = n + 1
	end
	return n
end

local function chunkRingDistance(cx, cz, ox, oz)
	local dx = math.abs(cx - ox)
	local dz = math.abs(cz - oz)
	return math.max(dx, dz)
end

local function craterListsEqual(aList, bList)
	local a = type(aList) == "table" and aList or {}
	local b = type(bList) == "table" and bList or {}
	if #a ~= #b then
		return false
	end
	for i = 1, #a do
		local ca = a[i] or {}
		local cb = b[i] or {}
		if math.abs((tonumber(ca.x) or 0) - (tonumber(cb.x) or 0)) > 1e-4 then
			return false
		end
		if math.abs((tonumber(ca.y) or 0) - (tonumber(cb.y) or 0)) > 1e-4 then
			return false
		end
		if math.abs((tonumber(ca.z) or 0) - (tonumber(cb.z) or 0)) > 1e-4 then
			return false
		end
		if math.abs((tonumber(ca.radius) or 0) - (tonumber(cb.radius) or 0)) > 1e-4 then
			return false
		end
		if math.abs((tonumber(ca.depth) or 0) - (tonumber(cb.depth) or 0)) > 1e-4 then
			return false
		end
		if math.abs((tonumber(ca.rim) or 0) - (tonumber(cb.rim) or 0)) > 1e-4 then
			return false
		end
	end
	return true
end

local function removeObjectInstance(objects, objectRef)
	for i = #objects, 1, -1 do
		if objects[i] == objectRef then
			table.remove(objects, i)
			return true
		end
	end
	return false
end

local function clearChunkCache(terrainState)
	terrainState.chunkCache = {}
	terrainState.chunkCacheStamp = {}
	terrainState.chunkCacheCount = 0
	terrainState.chunkCacheTick = 0
end

local function cacheChunkObject(terrainState, key, obj)
	if not terrainState or type(key) ~= "string" or key == "" or type(obj) ~= "table" then
		return
	end
	terrainState.chunkCache = terrainState.chunkCache or {}
	terrainState.chunkCacheStamp = terrainState.chunkCacheStamp or {}
	terrainState.chunkCacheTick = (terrainState.chunkCacheTick or 0) + 1

	local wasCached = terrainState.chunkCache[key] ~= nil
	terrainState.chunkCache[key] = obj
	terrainState.chunkCacheStamp[key] = terrainState.chunkCacheTick
	if not wasCached then
		terrainState.chunkCacheCount = (terrainState.chunkCacheCount or 0) + 1
	end

	local limit = math.max(32, math.floor(tonumber(terrainState.chunkCacheLimit) or 512))
	while (terrainState.chunkCacheCount or 0) > limit do
		local oldestKey = nil
		local oldestStamp = math.huge
		for cachedKey, stamp in pairs(terrainState.chunkCacheStamp) do
			local s = tonumber(stamp) or 0
			if s < oldestStamp then
				oldestStamp = s
				oldestKey = cachedKey
			end
		end
		if not oldestKey then
			break
		end
		terrainState.chunkCache[oldestKey] = nil
		terrainState.chunkCacheStamp[oldestKey] = nil
		terrainState.chunkCacheCount = math.max(0, (terrainState.chunkCacheCount or 0) - 1)
	end
end

local function takeCachedChunkObject(terrainState, key)
	if not terrainState or type(key) ~= "string" or key == "" then
		return nil
	end
	local cache = terrainState.chunkCache
	if type(cache) ~= "table" then
		return nil
	end
	local obj = cache[key]
	if obj then
		cache[key] = nil
		if type(terrainState.chunkCacheStamp) == "table" then
			terrainState.chunkCacheStamp[key] = nil
		end
		terrainState.chunkCacheCount = math.max(0, (terrainState.chunkCacheCount or 0) - 1)
	end
	return obj
end

local function appendSkirtQuads(model, params, bounds, context, step, depth)
	local function addQuad(a, b, c, d, color)
		local base = #model.vertices
		model.vertices[base + 1] = a
		model.vertices[base + 2] = b
		model.vertices[base + 3] = c
		model.vertices[base + 4] = d
		local normal = { 0, 1, 0 }
		model.vertexNormals[base + 1] = normal
		model.vertexNormals[base + 2] = normal
		model.vertexNormals[base + 3] = normal
		model.vertexNormals[base + 4] = normal
		local rgba = { color[1], color[2], color[3], 1.0 }
		model.vertexColors[base + 1] = rgba
		model.vertexColors[base + 2] = rgba
		model.vertexColors[base + 3] = rgba
		model.vertexColors[base + 4] = rgba
		model.faces[#model.faces + 1] = { base + 1, base + 2, base + 3 }
		model.faceColors[#model.faceColors + 1] = rgba
		model.faces[#model.faces + 1] = { base + 1, base + 3, base + 4 }
		model.faceColors[#model.faceColors + 1] = rgba
	end

	local function surfaceY(x, z)
		return sdfField.sampleSurfaceHeight(x, z, context)
	end

	local skirtDepth = math.max(2.0, tonumber(depth) or 24)
	local edgeStep = math.max(2.0, tonumber(step) or 8)

	for x = bounds.x0, bounds.x1 - edgeStep, edgeStep do
		local x2 = math.min(bounds.x1, x + edgeStep)
		local yA = surfaceY(x, bounds.z0)
		local yB = surfaceY(x2, bounds.z0)
		local c = sdfField.sampleColorAtWorld((x + x2) * 0.5, (yA + yB) * 0.5, bounds.z0, context)
		addQuad({ x, yA, bounds.z0 }, { x2, yB, bounds.z0 }, { x2, yB - skirtDepth, bounds.z0 }, { x, yA - skirtDepth, bounds.z0 }, c)

		local yC = surfaceY(x, bounds.z1)
		local yD = surfaceY(x2, bounds.z1)
		local c2 = sdfField.sampleColorAtWorld((x + x2) * 0.5, (yC + yD) * 0.5, bounds.z1, context)
		addQuad({ x2, yD, bounds.z1 }, { x, yC, bounds.z1 }, { x, yC - skirtDepth, bounds.z1 }, { x2, yD - skirtDepth, bounds.z1 }, c2)
	end

	for z = bounds.z0, bounds.z1 - edgeStep, edgeStep do
		local z2 = math.min(bounds.z1, z + edgeStep)
		local yA = surfaceY(bounds.x0, z)
		local yB = surfaceY(bounds.x0, z2)
		local c = sdfField.sampleColorAtWorld(bounds.x0, (yA + yB) * 0.5, (z + z2) * 0.5, context)
		addQuad({ bounds.x0, yB, z2 }, { bounds.x0, yA, z }, { bounds.x0, yA - skirtDepth, z }, { bounds.x0, yB - skirtDepth, z2 }, c)

		local yC = surfaceY(bounds.x1, z)
		local yD = surfaceY(bounds.x1, z2)
		local c2 = sdfField.sampleColorAtWorld(bounds.x1, (yC + yD) * 0.5, (z + z2) * 0.5, context)
		addQuad({ bounds.x1, yC, z }, { bounds.x1, yD, z2 }, { bounds.x1, yD - skirtDepth, z2 }, { bounds.x1, yC - skirtDepth, z }, c2)
	end
end

local function averageVertexColor(model, ia, ib, ic)
	local ca = (model.vertexColors and model.vertexColors[ia]) or { 0.45, 0.58, 0.36, 1.0 }
	local cb = (model.vertexColors and model.vertexColors[ib]) or ca
	local cc = (model.vertexColors and model.vertexColors[ic]) or ca
	return {
		((ca[1] or 0) + (cb[1] or 0) + (cc[1] or 0)) / 3,
		((ca[2] or 0) + (cb[2] or 0) + (cc[2] or 0)) / 3,
		((ca[3] or 0) + (cb[3] or 0) + (cc[3] or 0)) / 3,
		1.0
	}
end

local function buildSurfaceChunkModel(bounds, cellSize, fieldContext)
	local model = {
		vertices = {},
		faces = {},
		vertexNormals = {},
		vertexColors = {},
		faceColors = {},
		isSolid = true
	}

	local spanX = math.max(1.0, (bounds.x1 or 0) - (bounds.x0 or 0))
	local spanZ = math.max(1.0, (bounds.z1 or 0) - (bounds.z0 or 0))
	local step = math.max(1.0, tonumber(cellSize) or 4.0)
	local nx = math.max(2, math.floor(spanX / step))
	local nz = math.max(2, math.floor(spanZ / step))
	local xStep = spanX / nx
	local zStep = spanZ / nz

	local grid = {}
	local minY = math.huge
	local maxY = -math.huge

	for iz = 0, nz do
		local row = {}
		local z = bounds.z0 + iz * zStep
		for ix = 0, nx do
			local x = bounds.x0 + ix * xStep
			local y = sdfField.sampleSurfaceHeight(x, z, fieldContext)
			local normal = sdfField.sampleNormal(x, y, z, fieldContext)
			local color = sdfField.sampleColorAtWorld(x, y, z, fieldContext)

			local idx = #model.vertices + 1
			model.vertices[idx] = { x, y, z }
			model.vertexNormals[idx] = { normal[1] or 0, normal[2] or 1, normal[3] or 0 }
			model.vertexColors[idx] = { color[1] or 0.45, color[2] or 0.58, color[3] or 0.36, 1.0 }
			row[ix + 1] = idx

			if y < minY then
				minY = y
			end
			if y > maxY then
				maxY = y
			end
		end
		grid[iz + 1] = row
	end

	for iz = 1, nz do
		local r0 = grid[iz]
		local r1 = grid[iz + 1]
		for ix = 1, nx do
			local i00 = r0[ix]
			local i10 = r0[ix + 1]
			local i01 = r1[ix]
			local i11 = r1[ix + 1]

			-- Winding is chosen so default backface culling keeps the top surface visible.
			model.faces[#model.faces + 1] = { i00, i11, i10 }
			model.faceColors[#model.faceColors + 1] = averageVertexColor(model, i00, i11, i10)
			model.faces[#model.faces + 1] = { i00, i01, i11 }
			model.faceColors[#model.faceColors + 1] = averageVertexColor(model, i00, i01, i11)
		end
	end

	local params = fieldContext and fieldContext.params or {}
	local waterLevel = tonumber(params.waterLevel)
	local shorelineBand = math.max(0.2, tonumber(params.shorelineBand) or 5.0)
	if waterLevel then
		for iz = 1, nz do
			local r0 = grid[iz]
			local r1 = grid[iz + 1]
			for ix = 1, nx do
				local i00 = r0[ix]
				local i10 = r0[ix + 1]
				local i01 = r1[ix]
				local i11 = r1[ix + 1]

				local v00 = model.vertices[i00]
				local v10 = model.vertices[i10]
				local v01 = model.vertices[i01]
				local v11 = model.vertices[i11]
				local yMin = math.min(v00[2], v10[2], v01[2], v11[2])
				if yMin <= (waterLevel + shorelineBand) then
					local w00 = sdfField.sampleWaterHeight(v00[1], v00[3], fieldContext)
					local w10 = sdfField.sampleWaterHeight(v10[1], v10[3], fieldContext)
					local w01 = sdfField.sampleWaterHeight(v01[1], v01[3], fieldContext)
					local w11 = sdfField.sampleWaterHeight(v11[1], v11[3], fieldContext)
					if w00 > (v00[2] + 0.12) or w10 > (v10[2] + 0.12) or w01 > (v01[2] + 0.12) or w11 > (v11[2] + 0.12) then
						local centerX = (v00[1] + v10[1] + v01[1] + v11[1]) * 0.25
						local centerZ = (v00[3] + v10[3] + v01[3] + v11[3]) * 0.25
						local centerY = (w00 + w10 + w01 + w11) * 0.25
						local color = sdfField.sampleColorAtWorld(centerX, centerY, centerZ, fieldContext)
						local rgba = { color[1], color[2], color[3], 1.0 }
						local base = #model.vertices

						model.vertices[base + 1] = { v00[1], w00, v00[3] }
						model.vertices[base + 2] = { v10[1], w10, v10[3] }
						model.vertices[base + 3] = { v11[1], w11, v11[3] }
						model.vertices[base + 4] = { v01[1], w01, v01[3] }
						model.vertexNormals[base + 1] = { 0, 1, 0 }
						model.vertexNormals[base + 2] = { 0, 1, 0 }
						model.vertexNormals[base + 3] = { 0, 1, 0 }
						model.vertexNormals[base + 4] = { 0, 1, 0 }
						model.vertexColors[base + 1] = rgba
						model.vertexColors[base + 2] = rgba
						model.vertexColors[base + 3] = rgba
						model.vertexColors[base + 4] = rgba
						model.faces[#model.faces + 1] = { base + 1, base + 3, base + 2 }
						model.faceColors[#model.faceColors + 1] = rgba
						model.faces[#model.faces + 1] = { base + 1, base + 4, base + 3 }
						model.faceColors[#model.faceColors + 1] = rgba

						if w00 > maxY then
							maxY = w00
						end
						if w10 > maxY then
							maxY = w10
						end
						if w01 > maxY then
							maxY = w01
						end
						if w11 > maxY then
							maxY = w11
						end
					end
				end
			end
		end
	end

	if minY == math.huge or maxY == -math.huge then
		minY = 0
		maxY = 0
	end
	return model, minY, maxY
end

local function createChunkObjectFromMesh(cx, cz, lod, params, qModule, model, meshMinY, meshMaxY)
	model = type(model) == "table" and model or {
		vertices = {},
		faces = {},
		vertexNormals = {},
		vertexColors = {},
		faceColors = {},
		isSolid = true
	}
	model.vertices = model.vertices or {}
	model.faces = model.faces or {}
	model.vertexNormals = model.vertexNormals or {}
	model.vertexColors = model.vertexColors or {}
	model.faceColors = model.faceColors or {}
	model.isSolid = model.isSolid ~= false

	local chunkSize = params.chunkSize
	local x0 = cx * chunkSize
	local z0 = cz * chunkSize
	local centerX = x0 + chunkSize * 0.5
	local centerZ = z0 + chunkSize * 0.5
	local skirtDepth = math.max(2.0, tonumber(params.skirtDepth) or 24.0)
	local minY = math.min(meshMinY or 0, (meshMinY or 0) - skirtDepth)
	local maxY = math.max(meshMaxY or 0, meshMinY or 0)
	local centerY = (minY + maxY) * 0.5
	local halfY = math.max(2.0, (maxY - minY) * 0.5)

	return {
		model = model,
		pos = { 0, 0, 0 },
		rot = qModule.identity(),
		scale = { 1, 1, 1 },
		color = { 1, 1, 1, 1 },
		isSolid = false,
		isGround = true,
		isTerrainChunk = true,
		chunkX = cx,
		chunkZ = cz,
		chunkLod = lod,
		halfSize = { x = chunkSize * 0.5, y = halfY, z = chunkSize * 0.5 },
		terrainCenter = { centerX, centerY, centerZ }
	}
end

local function createChunkObject(cx, cz, lod, params, fieldContext, qModule)
	local chunkSize = params.chunkSize
	local x0 = cx * chunkSize
	local x1 = x0 + chunkSize
	local z0 = cz * chunkSize
	local z1 = z0 + chunkSize
	local cellSize = (lod == 0) and params.lod0CellSize or params.lod1CellSize
	local useSurfaceOnlyMeshing = params.surfaceOnlyMeshing ~= false
	local model
	local meshMinY
	local meshMaxY

	if useSurfaceOnlyMeshing then
		model, meshMinY, meshMaxY = buildSurfaceChunkModel({
			x0 = x0,
			x1 = x1,
			z0 = z0,
			z1 = z1
		}, cellSize, fieldContext)
	else
		local overlap = cellSize
		local bounds = {
			x0 = x0 - overlap,
			x1 = x1 + overlap,
			y0 = params.minY,
			y1 = params.maxY,
			z0 = z0 - overlap,
			z1 = z1 + overlap
		}
		model = marching.polygonize({
			sampleSdf = function(x, y, z)
				return sdfField.sampleSdf(x, y, z, fieldContext)
			end,
			sampleNormal = function(x, y, z)
				return sdfField.sampleNormal(x, y, z, fieldContext)
			end,
			sampleColor = function(x, y, z)
				return sdfField.sampleColorAtWorld(x, y, z, fieldContext)
			end,
			isoLevel = 0,
			cellSize = cellSize,
			bounds = bounds
		})
		meshMinY = tonumber(params.minY) or -180
		meshMaxY = tonumber(params.maxY) or 320
	end

	if params.enableSkirts ~= false then
		appendSkirtQuads(model, params, {
			x0 = x0,
			x1 = x1,
			z0 = z0,
			z1 = z1
		}, fieldContext, cellSize, params.skirtDepth)
	end

	return createChunkObjectFromMesh(cx, cz, lod, params, qModule, model, meshMinY, meshMaxY)
end

function terrain.cloneColor3(value)
	return { value[1], value[2], value[3] }
end

function terrain.sanitizeColor3(value, fallback)
	return copyColor3(value, fallback)
end

function terrain.normalizeGroundParams(params, defaultGroundParams)
	local merged = shallowCopy(defaultGroundParams or {})
	for key, value in pairs(params or {}) do
		merged[key] = value
	end
	merged.chunkSize = tonumber(merged.chunkSize) or tonumber(merged.tileSize) or 64
	merged.lod0Radius = clamp(math.floor(tonumber(merged.lod0Radius) or 2), 1, 3)
	merged.lod1Radius = clamp(math.floor(tonumber(merged.lod1Radius) or 4), merged.lod0Radius, 6)
	merged.lod0CellSize = clamp(tonumber(merged.lod0CellSize) or 3.0, 1.0, 6.0)
	merged.lod1CellSize = clamp(tonumber(merged.lod1CellSize) or 6.0, merged.lod0CellSize, 12.0)
	merged.meshBuildBudget = clamp(math.floor(tonumber(merged.meshBuildBudget) or 2), 1, 3)
	merged.workerMaxInflight = clamp(math.floor(tonumber(merged.workerMaxInflight) or 2), 1, 4)
	merged.chunkCacheLimit = math.max(32, math.floor(tonumber(merged.chunkCacheLimit) or 768))
	merged.generatorVersion = math.max(1, math.floor(tonumber(merged.generatorVersion) or 1))
	merged.enableSkirts = merged.enableSkirts ~= false
	merged.skirtDepth = math.max(2.0, tonumber(merged.skirtDepth) or 24.0)
	local surfaceOnlyRequested = merged.surfaceOnlyMeshing ~= false
	local caveEnabled = merged.caveEnabled ~= false
	local tunnelCount = math.max(0, math.floor(tonumber(merged.tunnelCount) or 0))
	if (not caveEnabled) and tunnelCount <= 0 then
		-- Without volumetric carving, marching generates chunk boundary shells.
		merged.surfaceOnlyMeshing = true
	else
		merged.surfaceOnlyMeshing = surfaceOnlyRequested
	end
	merged.threadedMeshing = merged.threadedMeshing ~= false

	-- Keep compatibility color fields used by minimap and networking.
	merged.grassColor = copyColor3(merged.grassColor, { 0.20, 0.62, 0.22 })
	merged.roadColor = copyColor3(merged.roadColor, { 0.10, 0.10, 0.10 })
	merged.fieldColor = copyColor3(merged.fieldColor, { 0.35, 0.45, 0.20 })
	merged.waterColor = copyColor3(merged.waterColor or merged.waterColour, { 0.10, 0.10, 0.50 })
	merged.grassVar = copyColor3(merged.grassVar, { 0.05, 0.10, 0.05 })
	merged.roadVar = copyColor3(merged.roadVar, { 0.02, 0.02, 0.02 })
	merged.fieldVar = copyColor3(merged.fieldVar, { 0.04, 0.06, 0.04 })
	merged.waterVar = copyColor3(merged.waterVar, { 0.02, 0.02, 0.02 })
	merged.waterRatio = clamp(tonumber(merged.waterRatio) or 0.26, 0, 1)

	return sdfField.normalizeParams(merged, merged)
end

function terrain.groundParamsEqual(a, b)
	if not a or not b then
		return false
	end
	local keys = {
		"seed",
		"chunkSize",
		"worldRadius",
		"minY",
		"maxY",
		"baseHeight",
		"heightAmplitude",
		"heightFrequency",
		"heightOctaves",
		"heightLacunarity",
		"heightGain",
		"surfaceDetailAmplitude",
		"surfaceDetailFrequency",
		"ridgeAmplitude",
		"ridgeFrequency",
		"ridgeSharpness",
		"macroWarpAmplitude",
		"macroWarpFrequency",
		"terraceStrength",
		"terraceStep",
		"waterLevel",
		"shorelineBand",
		"waterWaveAmplitude",
		"waterWaveFrequency",
		"biomeFrequency",
		"snowLine",
		"caveEnabled",
		"caveFrequency",
		"caveThreshold",
		"caveStrength",
		"caveMinY",
		"caveMaxY",
		"tunnelCount",
		"tunnelRadiusMin",
		"tunnelRadiusMax",
		"craterHistoryLimit",
		"lod0Radius",
		"lod1Radius",
		"lod0CellSize",
		"lod1CellSize",
		"meshBuildBudget",
		"workerMaxInflight",
		"chunkCacheLimit",
		"surfaceOnlyMeshing",
		"threadedMeshing",
		"generatorVersion"
	}
	for _, key in ipairs(keys) do
		if a[key] ~= b[key] then
			return false
		end
	end
	if not craterListsEqual(a.dynamicCraters, b.dynamicCraters) then
		return false
	end
	return true
end

function terrain.sampleSdfAtWorld(x, y, z, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		return math.huge
	end
	return sdfField.sampleSdf(x, y, z, context)
end

function terrain.sampleNormalAtWorld(x, y, z, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		return { 0, 1, 0 }
	end
	return sdfField.sampleNormal(x, y, z, context)
end

function terrain.raycast(origin, dir, maxDist, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		return nil
	end
	return terrainCollision.raycast(
		origin,
		dir,
		maxDist,
		function(x, y, z)
			return sdfField.sampleSdf(x, y, z, context)
		end,
		function(x, y, z)
			return sdfField.sampleNormal(x, y, z, context)
		end
	)
end

function terrain.queryGroundHeight(x, z, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		return 0
	end
	return terrainCollision.queryGroundHeight(
		x,
		z,
		function(px, py, pz)
			return sdfField.sampleSdf(px, py, pz, context)
		end,
		function(px, py, pz)
			return sdfField.sampleNormal(px, py, pz, context)
		end,
		{
			minY = context.params.minY,
			maxY = context.params.maxY + 600
		}
	)
end

function terrain.sampleGroundHeightAtWorld(worldX, worldZ, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		return 0
	end
	return sdfField.sampleSurfaceHeight(worldX, worldZ, context) or 0
end

function terrain.sampleGroundColorAtWorld(worldX, worldZ, terrainStateOrParams)
	local context
	if type(terrainStateOrParams) == "table" and terrainStateOrParams.fieldContext then
		context = terrainStateOrParams.fieldContext
	elseif type(terrainStateOrParams) == "table" then
		context = sdfField.createContext(terrainStateOrParams)
	else
		context = sdfField.createContext({})
	end
	local y = sdfField.sampleSurfaceHeight(worldX, worldZ, context) or context.params.baseHeight
	return sdfField.sampleColorAtWorld(worldX, y, worldZ, context)
end

local function ensureTerrainState(context)
	context.terrainState = context.terrainState or {
		chunkMap = {},
		buildQueue = {},
		chunkOrder = {},
		centerChunkX = math.huge,
		centerChunkZ = math.huge,
		fieldContext = nil,
		activeGroundParams = nil,
		generatorVersion = 1,
		requiredSet = {},
		workerThread = nil,
		workerRequestChannel = nil,
		workerResponseChannel = nil,
		workerInflight = {},
		workerGeneration = 0,
		workerAvailable = false,
		chunkCache = {},
		chunkCacheStamp = {},
		chunkCacheCount = 0,
		chunkCacheTick = 0,
		chunkCacheLimit = 768
	}
	local terrainState = context.terrainState
	terrainState.chunkMap = terrainState.chunkMap or {}
	terrainState.buildQueue = terrainState.buildQueue or {}
	terrainState.chunkOrder = terrainState.chunkOrder or {}
	terrainState.requiredSet = terrainState.requiredSet or {}
	terrainState.workerInflight = terrainState.workerInflight or {}
	terrainState.chunkCache = terrainState.chunkCache or {}
	terrainState.chunkCacheStamp = terrainState.chunkCacheStamp or {}
	terrainState.chunkCacheCount = tonumber(terrainState.chunkCacheCount) or countTableKeys(terrainState.chunkCache)
	terrainState.chunkCacheTick = tonumber(terrainState.chunkCacheTick) or 0
	terrainState.chunkCacheLimit = math.max(32, math.floor(tonumber(terrainState.chunkCacheLimit) or 768))
	return terrainState
end

local function initializeWorker(terrainState)
	if terrainState.workerAvailable then
		return true
	end
	if not supportsThreading() then
		return false
	end
	if terrainState.workerThread then
		terrainState.workerAvailable = true
		return true
	end

	local workerId = tostring(os.time()) .. "_" .. tostring(math.random(100000, 999999))
	local requestName = "terrain_mesh_req_" .. workerId
	local responseName = "terrain_mesh_rsp_" .. workerId
	local scriptPath = "Source/Systems/TerrainMeshingWorker.lua"

	local ok, threadOrErr = pcall(function()
		return loveLib.thread.newThread(scriptPath)
	end)
	if not ok or not threadOrErr then
		return false
	end

	local requestChannel = loveLib.thread.getChannel(requestName)
	local responseChannel = loveLib.thread.getChannel(responseName)
	local started = pcall(function()
		threadOrErr:start(requestName, responseName)
	end)
	if not started then
		return false
	end

	terrainState.workerThread = threadOrErr
	terrainState.workerRequestChannel = requestChannel
	terrainState.workerResponseChannel = responseChannel
	terrainState.workerInflight = {}
	terrainState.workerGeneration = 0
	terrainState.workerAvailable = true
	return true
end

local function resetWorkerGeneration(terrainState)
	terrainState.workerGeneration = math.max(0, math.floor(tonumber(terrainState.workerGeneration) or 0)) + 1
	terrainState.workerInflight = {}
	if terrainState.workerResponseChannel then
		while true do
			local msg = terrainState.workerResponseChannel:pop()
			if not msg then
				break
			end
		end
	end
end

local function queueChunkBuild(terrainState, key)
	if terrainState.buildQueue[key] then
		return
	end
	terrainState.buildQueue[key] = true
	terrainState.chunkOrder[#terrainState.chunkOrder + 1] = key
end

local function computeRequiredChunkSet(params, camera, drawDistance)
	local chunkSize = params.chunkSize
	local centerCx = math.floor((camera.pos[1] or 0) / chunkSize)
	local centerCz = math.floor((camera.pos[3] or 0) / chunkSize)
	local lod0Radius = math.max(1, math.floor(tonumber(params.lod0Radius) or 2))
	local lod1Radius = math.max(lod0Radius, math.floor(tonumber(params.lod1Radius) or 4))
	local desiredDrawDistance = tonumber(drawDistance)
	if desiredDrawDistance then
		local derived = math.ceil(desiredDrawDistance / math.max(8.0, chunkSize * 2.8))
		local maxAdaptive = math.max(lod1Radius, math.floor(tonumber(params.maxAdaptiveLod1Radius) or 24))
		lod1Radius = clamp(derived, lod1Radius, maxAdaptive)
	end

	local required = {}
	for dx = -lod1Radius, lod1Radius do
		for dz = -lod1Radius, lod1Radius do
			local cx = centerCx + dx
			local cz = centerCz + dz
			local ring = chunkRingDistance(cx, cz, centerCx, centerCz)
			local lod = (ring <= lod0Radius) and 0 or 1
			local key = buildChunkKey(cx, cz, lod, params.generatorVersion, params.seed)
			required[key] = {
				cx = cx,
				cz = cz,
				lod = lod
			}
		end
	end
	return required, centerCx, centerCz, lod1Radius
end

local function flushChunkQueueSync(terrainState, params, objects, qModule)
	local built = 0
	local budget = math.max(1, math.floor(tonumber(params.meshBuildBudget) or 2))

	local nextOrder = {}
	for _, key in ipairs(terrainState.chunkOrder) do
		if built >= budget then
			nextOrder[#nextOrder + 1] = key
		elseif terrainState.buildQueue[key] then
			local cx, cz, lod = parseChunkKey(key)
			local obj = createChunkObject(cx, cz, lod, params, terrainState.fieldContext, qModule)
			terrainState.chunkMap[key] = obj
			objects[#objects + 1] = obj
			terrainState.buildQueue[key] = nil
			built = built + 1
		end
	end
	terrainState.chunkOrder = nextOrder
	return built > 0
end

local function dispatchChunkQueueToWorker(terrainState, params)
	if not terrainState.workerAvailable or not terrainState.workerRequestChannel then
		return false
	end

	local maxInflight = clamp(math.floor(tonumber(params.workerMaxInflight) or 2), 1, 4)
	local inflight = 0
	for _ in pairs(terrainState.workerInflight) do
		inflight = inflight + 1
	end

	if inflight >= maxInflight then
		return false
	end

	local dispatched = false
	local nextOrder = {}
	for _, key in ipairs(terrainState.chunkOrder) do
		if inflight >= maxInflight then
			nextOrder[#nextOrder + 1] = key
		elseif terrainState.buildQueue[key] then
			local cx, cz, lod = parseChunkKey(key)
			local ok = pcall(function()
				terrainState.workerRequestChannel:push({
					type = "build_chunk",
					key = key,
					cx = cx,
					cz = cz,
					lod = lod,
					generation = terrainState.workerGeneration,
					params = params
				})
			end)
			if ok then
				terrainState.buildQueue[key] = nil
				terrainState.workerInflight[key] = true
				inflight = inflight + 1
				dispatched = true
			else
				nextOrder[#nextOrder + 1] = key
			end
		end
	end
	terrainState.chunkOrder = nextOrder
	return dispatched
end

local function collectWorkerChunkResults(terrainState, params, objects, qModule)
	if not terrainState.workerAvailable or not terrainState.workerResponseChannel then
		return false
	end

	local changed = false
	while true do
		local msg = terrainState.workerResponseChannel:pop()
		if not msg then
			break
		end
		if type(msg) == "table" and msg.type == "build_chunk_done" then
			local key = tostring(msg.key or "")
			terrainState.workerInflight[key] = nil
			local generation = math.floor(tonumber(msg.generation) or -1)
			if generation == terrainState.workerGeneration and terrainState.requiredSet[key] and not terrainState.chunkMap[key] then
				local cx = tonumber(msg.cx) or 0
				local cz = tonumber(msg.cz) or 0
				local lod = tonumber(msg.lod) or 0
				local obj = createChunkObjectFromMesh(
					cx,
					cz,
					lod,
					params,
					qModule,
					msg.model,
					tonumber(msg.meshMinY),
					tonumber(msg.meshMaxY)
				)
				terrainState.chunkMap[key] = obj
				objects[#objects + 1] = obj
				changed = true
			end
		elseif type(msg) == "table" and msg.type == "build_chunk_failed" then
			local key = tostring(msg.key or "")
			terrainState.workerInflight[key] = nil
			if key ~= "" and terrainState.requiredSet[key] and not terrainState.chunkMap[key] then
				queueChunkBuild(terrainState, key)
			end
		end
	end
	return changed
end

function terrain.updateGroundStreaming(forceRebuild, context)
	local camera = context.camera
	local objects = context.objects
	if not camera or not objects then
		return false, context.terrainState
	end

	local terrainState = ensureTerrainState(context)
	local params = context.activeGroundParams or terrainState.activeGroundParams
	if not params then
		return false, terrainState
	end

	if (not terrainState.fieldContext) or (terrainState.activeGroundParams ~= params) then
		terrainState.activeGroundParams = params
		terrainState.fieldContext = sdfField.createContext(params)
		terrainState.generatorVersion = params.generatorVersion or 1
		terrainState.chunkCacheLimit = math.max(32, math.floor(tonumber(params.chunkCacheLimit) or terrainState.chunkCacheLimit or 768))
		for key in pairs(terrainState.chunkMap) do
			removeObjectInstance(objects, terrainState.chunkMap[key])
		end
		terrainState.chunkMap = {}
		terrainState.buildQueue = {}
		terrainState.chunkOrder = {}
		terrainState.requiredSet = {}
		clearChunkCache(terrainState)
		resetWorkerGeneration(terrainState)
		forceRebuild = true
	end

	local required, centerCx, centerCz, requiredRadius = computeRequiredChunkSet(params, camera, context.drawDistance)
	terrainState.requiredSet = required
	terrainState.lastRequiredRadius = requiredRadius
	local centerChanged = (centerCx ~= terrainState.centerChunkX) or (centerCz ~= terrainState.centerChunkZ)
	terrainState.centerChunkX = centerCx
	terrainState.centerChunkZ = centerCz

	local changed = false
	if forceRebuild or centerChanged then
		for key, obj in pairs(terrainState.chunkMap) do
			if not required[key] then
				removeObjectInstance(objects, obj)
				terrainState.chunkMap[key] = nil
				cacheChunkObject(terrainState, key, obj)
				changed = true
			end
		end

		local nextOrder = {}
		for _, key in ipairs(terrainState.chunkOrder) do
			if required[key] and terrainState.buildQueue[key] then
				nextOrder[#nextOrder + 1] = key
			else
				terrainState.buildQueue[key] = nil
			end
		end
		terrainState.chunkOrder = nextOrder
		for key in pairs(terrainState.workerInflight) do
			if not required[key] then
				terrainState.workerInflight[key] = nil
			end
		end
	end

	local missingKeys = {}
	for key, info in pairs(required) do
		if (not terrainState.chunkMap[key]) and (not terrainState.buildQueue[key]) and (not terrainState.workerInflight[key]) then
			missingKeys[#missingKeys + 1] = {
				key = key,
				cx = info.cx,
				cz = info.cz,
				lod = info.lod
			}
		end
	end
	table.sort(missingKeys, function(a, b)
		local da = chunkRingDistance(a.cx, a.cz, centerCx, centerCz)
		local db = chunkRingDistance(b.cx, b.cz, centerCx, centerCz)
		if da ~= db then
			return da < db
		end
		if a.lod ~= b.lod then
			return (a.lod or 0) < (b.lod or 0)
		end
		if a.cx ~= b.cx then
			return a.cx < b.cx
		end
		return a.cz < b.cz
	end)

	for _, item in ipairs(missingKeys) do
		local key = item.key
		local cachedObj = takeCachedChunkObject(terrainState, key)
		if cachedObj then
			terrainState.chunkMap[key] = cachedObj
			objects[#objects + 1] = cachedObj
			changed = true
		else
			queueChunkBuild(terrainState, key)
			changed = true
		end
	end

	if terrainState.workerAvailable and terrainState.workerThread and type(terrainState.workerThread.getError) == "function" then
		local workerError = terrainState.workerThread:getError()
		if workerError then
			terrainState.workerAvailable = false
			for key in pairs(terrainState.workerInflight) do
				terrainState.workerInflight[key] = nil
				queueChunkBuild(terrainState, key)
			end
		end
	end

	local useWorker = (params.threadedMeshing ~= false) and initializeWorker(terrainState)
	if useWorker then
		if collectWorkerChunkResults(terrainState, params, objects, context.q) then
			changed = true
		end
		if dispatchChunkQueueToWorker(terrainState, params) then
			changed = true
		end
	else
		if flushChunkQueueSync(terrainState, params, objects, context.q) then
			changed = true
		end
	end

	if useWorker and collectWorkerChunkResults(terrainState, params, objects, context.q) then
		changed = true
	end

	return changed, terrainState
end

function terrain.rebuildGroundFromParams(params, reason, context)
	local normalized = terrain.normalizeGroundParams(params, context.defaultGroundParams or params or {})
	local terrainState = ensureTerrainState(context)
	if terrainState.activeGroundParams and terrain.groundParamsEqual(terrainState.activeGroundParams, normalized) then
		return false
	end

	for key, obj in pairs(terrainState.chunkMap or {}) do
		removeObjectInstance(context.objects, obj)
		terrainState.chunkMap[key] = nil
	end
	terrainState.buildQueue = {}
	terrainState.chunkOrder = {}
	terrainState.centerChunkX = math.huge
	terrainState.centerChunkZ = math.huge
	terrainState.requiredSet = {}
	terrainState.chunkCacheLimit = math.max(32, math.floor(tonumber(normalized.chunkCacheLimit) or terrainState.chunkCacheLimit or 768))
	clearChunkCache(terrainState)
	terrainState.activeGroundParams = normalized
	terrainState.fieldContext = sdfField.createContext(normalized)
	terrainState.generatorVersion = normalized.generatorVersion or 1
	resetWorkerGeneration(terrainState)

	local changed = terrain.updateGroundStreaming(true, context)
	local worldHalfExtent = (normalized.chunkSize * (normalized.lod1Radius + 1))

	if reason and reason ~= "" and context.log then
		context.log(string.format(
			"Terrain rebuilt (%s): seed=%d chunk=%d lod0=%d lod1=%d",
			reason,
			normalized.seed,
			normalized.chunkSize,
			normalized.lod0Radius,
			normalized.lod1Radius
		))
	end

	return changed and true or true, {
		activeGroundParams = normalized,
		terrainState = terrainState,
		worldHalfExtent = worldHalfExtent,
		groundObject = nil
	}
end

function terrain.addCrater(craterSpec, context)
	context = context or {}
	if type(craterSpec) ~= "table" then
		return false
	end

	local terrainState = ensureTerrainState(context)
	local activeParams = context.activeGroundParams or terrainState.activeGroundParams
	if type(activeParams) ~= "table" then
		return false
	end

	local normalized = terrain.normalizeGroundParams(activeParams, context.defaultGroundParams or activeParams)
	local radius = math.max(1.0, tonumber(craterSpec.radius) or 8.0)
	local depth = math.max(0.4, tonumber(craterSpec.depth) or (radius * 0.45))
	local crater = {
		x = tonumber(craterSpec.x) or 0,
		y = tonumber(craterSpec.y) or 0,
		z = tonumber(craterSpec.z) or 0,
		radius = radius,
		depth = depth,
		rim = clamp(tonumber(craterSpec.rim) or 0.12, 0.0, 0.75)
	}

	local craterList = cloneCraterList(normalized.dynamicCraters)
	craterList[#craterList + 1] = crater
	local limit = math.max(0, math.floor(tonumber(normalized.craterHistoryLimit) or 64))
	if limit > 0 and #craterList > limit then
		local trimmed = {}
		local first = #craterList - limit + 1
		for i = first, #craterList do
			trimmed[#trimmed + 1] = craterList[i]
		end
		craterList = trimmed
	end

	normalized.dynamicCraters = craterList
	normalized.generatorVersion = math.max(1, math.floor(tonumber(normalized.generatorVersion) or 1) + 1)

	terrainState.activeGroundParams = normalized
	terrainState.fieldContext = sdfField.createContext(normalized)
	terrainState.generatorVersion = normalized.generatorVersion
	terrainState.chunkCacheLimit = math.max(32, math.floor(tonumber(normalized.chunkCacheLimit) or terrainState.chunkCacheLimit or 768))
	if type(context.objects) == "table" then
		for key, obj in pairs(terrainState.chunkMap or {}) do
			removeObjectInstance(context.objects, obj)
			terrainState.chunkMap[key] = nil
		end
	end
	terrainState.buildQueue = {}
	terrainState.chunkOrder = {}
	terrainState.requiredSet = {}
	terrainState.centerChunkX = math.huge
	terrainState.centerChunkZ = math.huge
	clearChunkCache(terrainState)
	resetWorkerGeneration(terrainState)

	if context.camera and context.objects then
		local changed, nextTerrainState = terrain.updateGroundStreaming(true, {
			terrainState = terrainState,
			activeGroundParams = normalized,
			camera = context.camera,
			objects = context.objects,
			q = context.q
		})
		if nextTerrainState then
			terrainState = nextTerrainState
		end
		return true, {
			changed = changed and true or false,
			crater = crater,
			activeGroundParams = normalized,
			terrainState = terrainState
		}
	end

	return true, {
		changed = true,
		crater = crater,
		activeGroundParams = normalized,
		terrainState = terrainState
	}
end

return terrain
