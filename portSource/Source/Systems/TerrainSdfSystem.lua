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

local function cloneWorkerParamsSnapshot(params)
	local snapshot = shallowCopy(params or {})
	snapshot.dynamicCraters = cloneCraterList(snapshot.dynamicCraters)
	return snapshot
end

local terrainProfileColors = {
	runtime = { 0.95, 0.70, 0.22, 1.0 },
	requiredSet = { 0.91, 0.56, 0.20, 1.0 },
	slotCollapse = { 0.82, 0.50, 0.20, 1.0 },
	missingScan = { 0.96, 0.62, 0.28, 1.0 },
	queueCompaction = { 0.90, 0.52, 0.18, 1.0 },
	queueFill = { 1.00, 0.64, 0.22, 1.0 },
	workerHealth = { 0.82, 0.76, 0.26, 1.0 },
	workerCollect = { 0.98, 0.48, 0.20, 1.0 },
	workerDispatch = { 1.00, 0.44, 0.16, 1.0 },
	syncBuild = { 0.94, 0.34, 0.20, 1.0 },
	prune = { 0.88, 0.40, 0.24, 1.0 },
	stats = { 0.82, 0.72, 0.24, 1.0 },
	workerThreadBuild = { 0.98, 0.28, 0.16, 1.0 }
}

local function beginProfileScope(profiler, metricId, color, label)
	if type(profiler) == "table" and type(profiler.beginScope) == "function" then
		return profiler:beginScope(metricId, color, label)
	end
	return nil
end

local function endProfileScope(profiler, token)
	if token and type(profiler) == "table" and type(profiler.endScope) == "function" then
		return profiler:endScope(token)
	end
	return 0
end

local function addProfileSample(profiler, metricId, valueMs, color, label)
	if type(profiler) == "table" and type(profiler.addSample) == "function" then
		profiler:addSample(metricId, valueMs, color, label)
	end
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

local function resolvePortSourcePath(relativePath)
	if loveLib and loveLib.filesystem and loveLib.filesystem.getInfo and loveLib.filesystem.getInfo(relativePath) then
		return relativePath
	end
	return "portSource/" .. tostring(relativePath)
end

local function getPortSourceAbsoluteRoot()
	local sep = package.config and package.config:sub(1, 1) or "/"
	local source = (loveLib and loveLib.filesystem and loveLib.filesystem.getSource and loveLib.filesystem.getSource()) or "."
	local sourceBase = (loveLib and loveLib.filesystem and loveLib.filesystem.getSourceBaseDirectory and loveLib.filesystem.getSourceBaseDirectory()) or "."
	if loveLib and loveLib.filesystem and loveLib.filesystem.getInfo then
		if loveLib.filesystem.getInfo("Source", "directory") then
			return source
		end
		if loveLib.filesystem.getInfo("portSource/Source", "directory") then
			return tostring(source) .. sep .. "portSource"
		end
	end
	return sourceBase
end

local function resolvePortSourceThreadPath(relativePath)
	local sep = package.config and package.config:sub(1, 1) or "/"
	return tostring(getPortSourceAbsoluteRoot()) .. sep .. tostring(relativePath):gsub("/", sep)
end

local function getPortSourceRootPath()
	return getPortSourceAbsoluteRoot()
end

local function currentTimeSeconds()
	if type(loveLib) == "table" and type(loveLib.timer) == "table" and type(loveLib.timer.getTime) == "function" then
		return loveLib.timer.getTime()
	end
	return os.clock()
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

local function buildChunkCoordKey(cx, cz, lod)
	return table.concat({
		tostring(math.floor(lod or 0)),
		tostring(cx),
		tostring(cz)
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

local function getLodChunkScale(params, lod)
	if lod <= 0 then
		return math.max(1, math.floor(tonumber(params.lod0ChunkScale) or 1))
	end
	if lod == 1 then
		return math.max(1, math.floor(tonumber(params.lod1ChunkScale) or 4))
	end
	return math.max(1, math.floor(tonumber(params.lod2ChunkScale) or 16))
end

local function getChunkWorldSize(params, lod)
	return math.max(8.0, tonumber(params.chunkSize) or 128) * getLodChunkScale(params, lod)
end

local function chunkBoundsForWorldSize(cx, cz, worldSize)
	local size = math.max(8.0, tonumber(worldSize) or 128)
	local x0 = cx * size
	local z0 = cz * size
	return x0, z0, x0 + size, z0 + size
end

local function boundsMinDistanceSqToPoint(px, pz, x0, z0, x1, z1)
	local dx = 0
	if px < x0 then
		dx = x0 - px
	elseif px > x1 then
		dx = px - x1
	end
	local dz = 0
	if pz < z0 then
		dz = z0 - pz
	elseif pz > z1 then
		dz = pz - z1
	end
	return dx * dx + dz * dz
end

local function boundsMaxDistanceSqToPoint(px, pz, x0, z0, x1, z1)
	local dx = math.max(math.abs(px - x0), math.abs(px - x1))
	local dz = math.max(math.abs(pz - z0), math.abs(pz - z1))
	return dx * dx + dz * dz
end

local function pointInsideBounds(x, z, bounds)
	return x >= (bounds.x0 or 0) and x <= (bounds.x1 or 0) and z >= (bounds.z0 or 0) and z <= (bounds.z1 or 0)
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

local function rebuildTerrainObjectIndex(terrainState, objects)
	if type(terrainState) ~= "table" or type(objects) ~= "table" then
		return
	end
	local indexMap = terrainState.objectIndexByChunkKey or {}
	for key in pairs(indexMap) do
		indexMap[key] = nil
	end
	for i = 1, #objects do
		local obj = objects[i]
		local key = type(obj) == "table" and tostring(obj._terrainChunkKey or "") or ""
		if key ~= "" then
			indexMap[key] = i
		end
	end
	terrainState.objectIndexByChunkKey = indexMap
end

local function removeObjectInstance(terrainState, objects, objectRef)
	if type(objects) ~= "table" or type(objectRef) ~= "table" then
		return false
	end
	local key = tostring(objectRef._terrainChunkKey or "")
	local indexMap = terrainState and terrainState.objectIndexByChunkKey or nil
	local index = nil
	if key ~= "" and type(indexMap) == "table" then
		index = tonumber(indexMap[key])
		if index and objects[index] ~= objectRef then
			rebuildTerrainObjectIndex(terrainState, objects)
			index = tonumber(indexMap[key])
		end
	end
	if not index or objects[index] ~= objectRef then
		for i = #objects, 1, -1 do
			if objects[i] == objectRef then
				index = i
				break
			end
		end
	end
	if not index or objects[index] ~= objectRef then
		if key ~= "" and type(indexMap) == "table" then
			indexMap[key] = nil
		end
		return false
	end
	local lastIndex = #objects
	local tail = objects[lastIndex]
	objects[index] = tail
	objects[lastIndex] = nil
	if index < lastIndex and type(tail) == "table" and type(indexMap) == "table" then
		local movedKey = tostring(tail._terrainChunkKey or "")
		if movedKey ~= "" then
			indexMap[movedKey] = index
		end
	end
	if key ~= "" and type(indexMap) == "table" then
		indexMap[key] = nil
	end
	return true
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

	local limit = clamp(math.floor(tonumber(terrainState.chunkCacheLimit) or 128), 32, 256)
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

local function supportsTerrainTextureTiles()
	return type(loveLib) == "table" and
		type(loveLib.graphics) == "table" and
		type(loveLib.graphics.newImage) == "function" and
		type(loveLib.image) == "table" and
		type(loveLib.image.newImageData) == "function"
end

local function getTerrainTextureResolution(params, lod)
	if lod <= 0 then
		return clamp(math.floor(tonumber(params.lod0TextureResolution) or 512), 64, 512)
	end
	if lod == 1 then
		return clamp(math.floor(tonumber(params.lod1TextureResolution) or 256), 32, 512)
	end
	return clamp(math.floor(tonumber(params.lod2TextureResolution) or 64), 16, 256)
end

local function ensureChunkModelUvSet(model, bounds)
	if type(model) ~= "table" or type(model.vertices) ~= "table" then
		return
	end
	local spanX = math.max(1.0, (tonumber(bounds.x1) or 0) - (tonumber(bounds.x0) or 0))
	local spanZ = math.max(1.0, (tonumber(bounds.z1) or 0) - (tonumber(bounds.z0) or 0))
	model.vertexUVs = model.vertexUVs or {}
	for i = 1, #model.vertices do
		local vertex = model.vertices[i]
		if type(vertex) == "table" then
			model.vertexUVs[i] = {
				clamp(((tonumber(vertex[1]) or 0) - bounds.x0) / spanX, 0, 1),
				clamp(((tonumber(vertex[3]) or 0) - bounds.z0) / spanZ, 0, 1)
			}
		end
	end
end

local function whitenChunkVertexColors(model)
	if type(model) ~= "table" then
		return
	end
	model.vertexColors = model.vertexColors or {}
	for i = 1, #model.vertices do
		model.vertexColors[i] = { 1, 1, 1, 1 }
	end
end

local function buildChunkTextureImage(cx, cz, lod, params, fieldContext)
	if params.textureTilesEnabled == false or not supportsTerrainTextureTiles() or type(fieldContext) ~= "table" then
		return nil
	end
	local resolution = getTerrainTextureResolution(params, lod)
	local chunkWorldSize = getChunkWorldSize(params, lod)
	local x0 = cx * chunkWorldSize
	local z0 = cz * chunkWorldSize
	local imageData = loveLib.image.newImageData(resolution, resolution)
	local denom = math.max(1, resolution - 1)
	for py = 0, resolution - 1 do
		local v = py / denom
		local worldZ = z0 + v * chunkWorldSize
		for px = 0, resolution - 1 do
			local u = px / denom
			local worldX = x0 + u * chunkWorldSize
			local surfaceY = sdfField.sampleSurfaceHeight(worldX, worldZ, fieldContext)
			local color = sdfField.sampleColorAtWorld(worldX, surfaceY, worldZ, fieldContext)
			imageData:setPixel(
				px,
				py,
				clamp(tonumber(color[1]) or 0.45, 0, 1),
				clamp(tonumber(color[2]) or 0.58, 0, 1),
				clamp(tonumber(color[3]) or 0.36, 0, 1),
				1
			)
		end
	end
	local image = loveLib.graphics.newImage(imageData)
	image:setFilter("linear", "linear", 4)
	pcall(function()
		image:setMipmapFilter("linear", 0.25)
	end)
	return image
end

local function applyChunkTextureTile(obj, cx, cz, lod, params, fieldContext)
	if type(obj) ~= "table" or type(obj.model) ~= "table" then
		return
	end
	if params.textureTilesEnabled == false or not supportsTerrainTextureTiles() then
		return
	end
	local chunkWorldSize = getChunkWorldSize(params, lod)
	local bounds = {
		x0 = cx * chunkWorldSize,
		x1 = (cx + 1) * chunkWorldSize,
		z0 = cz * chunkWorldSize,
		z1 = (cz + 1) * chunkWorldSize
	}
	ensureChunkModelUvSet(obj.model, bounds)
	local image = buildChunkTextureImage(cx, cz, lod, params, fieldContext)
	if not image then
		return
	end
	whitenChunkVertexColors(obj.model)
	obj.images = {
		{ image = image }
	}
	obj.materials = {
		{
			baseColorFactor = { 1, 1, 1, 1 },
			metallicFactor = 0,
			roughnessFactor = 1,
			baseColorTexture = {
				imageIndex = 1,
				texCoord = 0
			}
		}
	}
	obj.uvFlipV = false
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

	local params = fieldContext and fieldContext.params or {}
	local spanX = math.max(1.0, (bounds.x1 or 0) - (bounds.x0 or 0))
	local spanZ = math.max(1.0, (bounds.z1 or 0) - (bounds.z0 or 0))
	local requestedStep = math.max(1.0, tonumber(cellSize) or 4.0)
	local maxCellsPerAxis = clamp(math.floor(tonumber(params.maxChunkCellsPerAxis) or 48), 24, 128)
	local stepX = math.max(requestedStep, spanX / maxCellsPerAxis)
	local stepZ = math.max(requestedStep, spanZ / maxCellsPerAxis)
	local nx = math.max(2, math.floor(spanX / stepX))
	local nz = math.max(2, math.floor(spanZ / stepZ))
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

local function createChunkObjectFromMesh(cx, cz, lod, params, qModule, model, meshMinY, meshMaxY, fieldContext)
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
	model._terrainFastPath = true

	local chunkWorldSize = getChunkWorldSize(params, lod)
	local x0 = cx * chunkWorldSize
	local z0 = cz * chunkWorldSize
	local centerX = x0 + chunkWorldSize * 0.5
	local centerZ = z0 + chunkWorldSize * 0.5
	local skirtDepth = math.max(2.0, tonumber(params.skirtDepth) or 24.0)
	local minY = math.min(meshMinY or 0, (meshMinY or 0) - skirtDepth)
	local maxY = math.max(meshMaxY or 0, meshMinY or 0)
	local centerY = (minY + maxY) * 0.5
	local halfY = math.max(2.0, (maxY - minY) * 0.5)

	local obj = {
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
		halfSize = { x = chunkWorldSize * 0.5, y = halfY, z = chunkWorldSize * 0.5 },
		terrainCenter = { centerX, centerY, centerZ }
	}
	applyChunkTextureTile(obj, cx, cz, lod, params, fieldContext)
	return obj
end

local function createChunkObject(cx, cz, lod, params, fieldContext, qModule)
	local chunkWorldSize = getChunkWorldSize(params, lod)
	local x0 = cx * chunkWorldSize
	local x1 = x0 + chunkWorldSize
	local z0 = cz * chunkWorldSize
	local z1 = z0 + chunkWorldSize
	local cellSize = params.lod2CellSize
	if lod == 0 then
		cellSize = params.lod0CellSize
	elseif lod == 1 then
		cellSize = params.lod1CellSize
	end
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

	return createChunkObjectFromMesh(cx, cz, lod, params, qModule, model, meshMinY, meshMaxY, fieldContext)
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
	merged.lod0Radius = clamp(math.floor(tonumber(merged.lod0Radius) or 2), 1, 4)
	merged.lod1Radius = clamp(math.floor(tonumber(merged.lod1Radius) or 4), merged.lod0Radius, 32)
	merged.lod2Radius = clamp(math.floor(tonumber(merged.lod2Radius) or math.max(merged.lod1Radius, 6)), merged.lod1Radius, 96)
	merged.terrainQuality = clamp(tonumber(merged.terrainQuality) or 1.0, 0.75, 6.0)
	merged.autoQualityEnabled = merged.autoQualityEnabled ~= false
	merged.targetFrameMs = clamp(tonumber(merged.targetFrameMs) or 16.6, 8.0, 50.0)
	merged.lod0ChunkScale = math.max(1, math.floor(tonumber(merged.lod0ChunkScale) or 1))
	merged.lod1ChunkScale = math.max(merged.lod0ChunkScale, math.floor(tonumber(merged.lod1ChunkScale) or 4))
	merged.lod2ChunkScale = math.max(merged.lod1ChunkScale, math.floor(tonumber(merged.lod2ChunkScale) or 16))
	merged.textureTilesEnabled = merged.textureTilesEnabled ~= false
	merged.lod0TextureResolution = clamp(math.floor(tonumber(merged.lod0TextureResolution) or 512), 64, 512)
	merged.lod1TextureResolution = clamp(math.floor(tonumber(merged.lod1TextureResolution) or 256), 32, 512)
	merged.lod2TextureResolution = clamp(math.floor(tonumber(merged.lod2TextureResolution) or 64), 16, 256)
	merged.gameplayRadiusMeters = math.max(
		merged.chunkSize,
		tonumber(merged.gameplayRadiusMeters) or math.max(512, merged.chunkSize * math.max(merged.lod0Radius, 4))
	)
	merged.midFieldRadiusMeters = math.max(
		merged.gameplayRadiusMeters + merged.chunkSize,
		tonumber(merged.midFieldRadiusMeters) or math.max(2048, merged.chunkSize * math.max(merged.lod1Radius, 16))
	)
	merged.horizonRadiusMeters = math.max(
		merged.midFieldRadiusMeters + merged.chunkSize,
		tonumber(merged.horizonRadiusMeters) or math.max(8192, merged.chunkSize * math.max(merged.lod2Radius, 64))
	)

	merged.lod0BaseCellSize = tonumber(merged.lod0BaseCellSize) or tonumber(merged.lod0CellSize) or 3.0
	merged.lod1BaseCellSize = tonumber(merged.lod1BaseCellSize) or tonumber(merged.lod1CellSize) or 6.0
	merged.lod2BaseCellSize = tonumber(merged.lod2BaseCellSize) or tonumber(merged.lod2CellSize) or 12.0

	local qualityScale = math.sqrt(merged.terrainQuality)
	local lod0Cell = merged.lod0BaseCellSize / qualityScale
	local lod1Cell = merged.lod1BaseCellSize / qualityScale
	local lod2Cell = merged.lod2BaseCellSize / qualityScale
	merged.lod0CellSize = clamp(lod0Cell, 1.0, 6.0)
	merged.lod1CellSize = clamp(lod1Cell, merged.lod0CellSize, 12.0)
	merged.lod2CellSize = clamp(lod2Cell, merged.lod1CellSize, 24.0)

	merged.meshBuildBudget = clamp(math.floor(tonumber(merged.meshBuildBudget) or 2), 1, 8)
	merged.workerMaxInflight = clamp(math.floor(tonumber(merged.workerMaxInflight) or 2), 1, 6)
	merged.workerResultBudgetPerFrame = clamp(
		math.floor(tonumber(merged.workerResultBudgetPerFrame) or 4),
		1,
		12
	)
	merged.workerResultTimeBudgetMs = clamp(
		tonumber(merged.workerResultTimeBudgetMs) or 3.0,
		0.25,
		20.0
	)
	merged.splitLodEnabled = false
	merged.highResSplitRatio = clamp(tonumber(merged.highResSplitRatio) or 0.5, 0.2, 0.8)
	merged.maxPendingChunks = clamp(math.floor(tonumber(merged.maxPendingChunks) or 96), 16, 192)
	merged.maxStaleChunks = clamp(math.floor(tonumber(merged.maxStaleChunks) or 32), 8, 128)
	merged.maxDisplayedChunksHardCap = clamp(
		math.floor(tonumber(merged.maxDisplayedChunksHardCap) or 8192),
		1536,
		16384
	)
	merged.maxDisplayedChunks = clamp(
		math.floor(tonumber(merged.maxDisplayedChunks) or 512),
		128,
		merged.maxDisplayedChunksHardCap
	)
	merged.drawDistanceOverridesLodRadius = merged.drawDistanceOverridesLodRadius == true
	merged.chunkCacheLimit = clamp(math.floor(tonumber(merged.chunkCacheLimit) or 128), 32, 256)
	merged.farLodConeEnabled = merged.farLodConeEnabled ~= false
	merged.farLodConeDegrees = clamp(tonumber(merged.farLodConeDegrees) or 110, 70, 170)
	merged.rearLod2Radius = clamp(
		math.floor(tonumber(merged.rearLod2Radius) or math.max(merged.lod1Radius, math.floor(merged.lod2Radius * 0.6))),
		merged.lod1Radius,
		merged.lod2Radius
	)
	merged.generatorVersion = math.max(1, math.floor(tonumber(merged.generatorVersion) or 1))
	merged.maxAdaptiveLod1Radius = clamp(
		math.floor(tonumber(merged.maxAdaptiveLod1Radius) or math.max(merged.lod1Radius, 6)),
		merged.lod1Radius,
		32
	)
	merged.maxChunkCellsPerAxis = clamp(
		math.floor(tonumber(merged.maxChunkCellsPerAxis) or 48),
		24,
		128
	)
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
		"lod2Radius",
		"lod0CellSize",
		"lod1CellSize",
		"lod2CellSize",
		"lod0BaseCellSize",
		"lod1BaseCellSize",
		"lod2BaseCellSize",
		"lod0ChunkScale",
		"lod1ChunkScale",
		"lod2ChunkScale",
		"textureTilesEnabled",
		"lod0TextureResolution",
		"lod1TextureResolution",
		"lod2TextureResolution",
		"worldId",
		"worldFormatVersion",
		"tunnelSeedCount",
		"gameplayRadiusMeters",
		"midFieldRadiusMeters",
		"horizonRadiusMeters",
		"meshBuildBudget",
		"workerMaxInflight",
		"workerResultBudgetPerFrame",
		"workerResultTimeBudgetMs",
		"chunkCacheLimit",
		"terrainQuality",
		"autoQualityEnabled",
		"targetFrameMs",
		"maxAdaptiveLod1Radius",
		"maxDisplayedChunksHardCap",
		"drawDistanceOverridesLodRadius",
		"splitLodEnabled",
		"highResSplitRatio",
		"maxChunkCellsPerAxis",
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
		chunkSlotMap = {},
		targetRequiredSet = {},
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
		chunkCacheLimit = 128,
		adaptiveTerrainQuality = nil,
		smoothedFrameMs = nil,
		syncBuildCredit = nil,
		staleChunkKeys = {},
		coverageStableSince = nil,
		displayedChunks = 0,
		missingRequiredChunks = 0,
		staleDisplayedChunks = 0,
		buildQueueSize = 0,
		workerInflightCount = 0,
		workerParamsToken = nil,
		workerUsesBinaryJobs = false,
		workerLastBuildMs = 0,
		workerLastBuildAvgMs = 0,
		workerLastBuildCount = 0,
		objectIndexByChunkKey = {}
	}
	local terrainState = context.terrainState
	terrainState.chunkMap = terrainState.chunkMap or {}
	terrainState.chunkSlotMap = terrainState.chunkSlotMap or {}
	terrainState.targetRequiredSet = terrainState.targetRequiredSet or {}
	terrainState.buildQueue = terrainState.buildQueue or {}
	terrainState.chunkOrder = terrainState.chunkOrder or {}
	terrainState.requiredSet = terrainState.requiredSet or {}
	terrainState.workerInflight = terrainState.workerInflight or {}
	terrainState.chunkCache = terrainState.chunkCache or {}
	terrainState.chunkCacheStamp = terrainState.chunkCacheStamp or {}
	terrainState.chunkCacheCount = tonumber(terrainState.chunkCacheCount) or countTableKeys(terrainState.chunkCache)
	terrainState.chunkCacheTick = tonumber(terrainState.chunkCacheTick) or 0
	terrainState.chunkCacheLimit = clamp(math.floor(tonumber(terrainState.chunkCacheLimit) or 128), 32, 256)
	terrainState.adaptiveTerrainQuality = tonumber(terrainState.adaptiveTerrainQuality)
	terrainState.smoothedFrameMs = tonumber(terrainState.smoothedFrameMs)
	terrainState.syncBuildCredit = tonumber(terrainState.syncBuildCredit)
	terrainState.staleChunkKeys = terrainState.staleChunkKeys or {}
	terrainState.workerLastBuildMs = tonumber(terrainState.workerLastBuildMs) or 0
	terrainState.workerLastBuildAvgMs = tonumber(terrainState.workerLastBuildAvgMs) or 0
	terrainState.workerLastBuildCount = math.max(0, math.floor(tonumber(terrainState.workerLastBuildCount) or 0))
	terrainState.objectIndexByChunkKey = terrainState.objectIndexByChunkKey or {}
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
	local scriptPath = resolvePortSourceThreadPath("Source/Systems/TerrainMeshingWorker.lua")

	local ok, threadOrErr = pcall(function()
		return loveLib.thread.newThread(scriptPath)
	end)
	if not ok or not threadOrErr then
		return false
	end

	local requestChannel = loveLib.thread.getChannel(requestName)
	local responseChannel = loveLib.thread.getChannel(responseName)
	local started = pcall(function()
		threadOrErr:start(requestName, responseName, getPortSourceRootPath())
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
	terrainState.workerParamsToken = nil
	terrainState.workerUsesBinaryJobs = type(loveLib.data) == "table" and type(loveLib.data.pack) == "function"
	return true
end

local function resetWorkerGeneration(terrainState)
	terrainState.workerGeneration = math.max(0, math.floor(tonumber(terrainState.workerGeneration) or 0)) + 1
	terrainState.workerInflight = {}
	terrainState.workerParamsToken = nil
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

local function markDisplayedChunksStale(terrainState)
	terrainState.staleChunkKeys = terrainState.staleChunkKeys or {}
	for key in pairs(terrainState.chunkMap or {}) do
		terrainState.staleChunkKeys[key] = true
	end
end

local function removeChunkByKey(terrainState, objects, key, cacheSafe)
	local obj = terrainState.chunkMap[key]
	if not obj then
		return false
	end
	local cx = tonumber(obj.chunkX)
	local cz = tonumber(obj.chunkZ)
	local lod = tonumber(obj.chunkLod)
	if cx == nil or cz == nil then
		local parsedCx, parsedCz, parsedLod = parseChunkKey(key)
		cx = cx or parsedCx
		cz = cz or parsedCz
		lod = lod or parsedLod
	end
	removeObjectInstance(terrainState, objects, obj)
	terrainState.chunkMap[key] = nil
	terrainState.staleChunkKeys[key] = nil
	terrainState.objectIndexByChunkKey[key] = nil
	local coordKey = buildChunkCoordKey(math.floor(cx or 0), math.floor(cz or 0), math.floor(lod or 0))
	if terrainState.chunkSlotMap and terrainState.chunkSlotMap[coordKey] == key then
		terrainState.chunkSlotMap[coordKey] = nil
	end
	if cacheSafe then
		cacheChunkObject(terrainState, key, obj)
	end
	return true
end

local function replaceChunkAtSlot(terrainState, objects, key, cx, cz, lod, obj)
	local coordKey = buildChunkCoordKey(cx, cz, lod)
	local incumbentKey = terrainState.chunkSlotMap and terrainState.chunkSlotMap[coordKey] or nil
	if incumbentKey and incumbentKey ~= key then
		removeChunkByKey(terrainState, objects, incumbentKey, false)
	end

	local existing = terrainState.chunkMap[key]
	if existing then
		removeObjectInstance(terrainState, objects, existing)
	end

	obj._terrainChunkKey = key
	terrainState.chunkMap[key] = obj
	terrainState.chunkSlotMap[coordKey] = key
	objects[#objects + 1] = obj
	terrainState.objectIndexByChunkKey[key] = #objects
	terrainState.staleChunkKeys[key] = nil
	return true
end

local function countPendingBuilds(terrainState)
	return countTableKeys(terrainState.buildQueue)
end

local function countInflight(terrainState)
	return countTableKeys(terrainState.workerInflight)
end

local function replenishSyncBuildCredit(terrainState, params, dt)
	local baseBudget = math.max(1, math.floor(tonumber(params.meshBuildBudget) or 2))
	local stepDt = clamp(tonumber(dt) or (1 / 60), 1 / 240, 0.25)
	local buildsPerSecond = baseBudget * 60
	local burstLimit = math.max(baseBudget, math.min(baseBudget * 3, 16))
	local credit = tonumber(terrainState.syncBuildCredit)
	if credit == nil then
		credit = baseBudget
	end
	credit = math.min(burstLimit, credit + stepDt * buildsPerSecond)
	if (tonumber(terrainState.displayedChunks) or 0) <= 0 then
		credit = math.max(credit, baseBudget)
	end
	terrainState.syncBuildCredit = credit
	return credit
end

local function deriveRuntimeParams(params, terrainState, frameTimeMs)
	local runtimeParams = shallowCopy(params)
	if not params.autoQualityEnabled then
		terrainState.adaptiveTerrainQuality = tonumber(params.terrainQuality) or 3.5
		terrainState.smoothedFrameMs = tonumber(frameTimeMs) or terrainState.smoothedFrameMs or params.targetFrameMs
		terrainState.coverageStableSince = currentTimeSeconds()
		return terrain.normalizeGroundParams(runtimeParams, runtimeParams)
	end

	local targetFrameMs = clamp(tonumber(params.targetFrameMs) or 16.6, 8.0, 50.0)
	local measuredFrameMs = clamp(tonumber(frameTimeMs) or targetFrameMs, 1.0, 100.0)
	local smoothedFrameMs = terrainState.smoothedFrameMs or measuredFrameMs
	smoothedFrameMs = smoothedFrameMs + (measuredFrameMs - smoothedFrameMs) * 0.12
	terrainState.smoothedFrameMs = smoothedFrameMs

	local adaptiveQuality = tonumber(terrainState.adaptiveTerrainQuality) or (tonumber(params.terrainQuality) or 3.5)
	local now = currentTimeSeconds()
	local missingRequiredChunks = math.max(0, math.floor(tonumber(terrainState.missingRequiredChunks) or 0))
	local buildQueueCount = countPendingBuilds(terrainState)
	local inflightCount = countInflight(terrainState)
	local coverageStableFor = 0
	if missingRequiredChunks > 0 or buildQueueCount > 0 or inflightCount > 0 then
		terrainState.coverageStableSince = nil
	else
		terrainState.coverageStableSince = terrainState.coverageStableSince or now
		coverageStableFor = now - terrainState.coverageStableSince
	end
	if coverageStableFor >= 0.75 then
		if smoothedFrameMs > (targetFrameMs * 1.08) then
			adaptiveQuality = adaptiveQuality - 0.12
		elseif smoothedFrameMs < (targetFrameMs * 0.92) then
			adaptiveQuality = adaptiveQuality + 0.08
		else
			local baseQuality = tonumber(params.terrainQuality) or adaptiveQuality
			adaptiveQuality = adaptiveQuality + (baseQuality - adaptiveQuality) * 0.08
		end
	end
	adaptiveQuality = clamp(adaptiveQuality, 2.0, 6.0)
	terrainState.adaptiveTerrainQuality = adaptiveQuality
	runtimeParams.terrainQuality = adaptiveQuality
	return terrain.normalizeGroundParams(runtimeParams, runtimeParams)
end

local function normalize2(x, z)
	local len = math.sqrt((x * x) + (z * z))
	if len <= 1e-6 then
		return nil
	end
	return x / len, z / len
end

local function cameraForward2D(camera)
	local vel = camera and (camera.flightVel or camera.vel) or nil
	if type(vel) == "table" then
		local vx = tonumber(vel[1]) or 0
		local vz = tonumber(vel[3]) or 0
		local speed2 = vx * vx + vz * vz
		if speed2 > (8.0 * 8.0) then
			local nx, nz = normalize2(vx, vz)
			if nx then
				return nx, nz
			end
		end
	end

	local rot = camera and camera.rot or nil
	if type(rot) == "table" then
		local w = tonumber(rot.w) or 1
		local x = tonumber(rot.x) or 0
		local y = tonumber(rot.y) or 0
		local z = tonumber(rot.z) or 0
		local fx = 2.0 * (x * z + w * y)
		local fz = 1.0 - 2.0 * (x * x + y * y)
		local nx, nz = normalize2(fx, fz)
		if nx then
			return nx, nz
		end
	end

	return 0, 1
end

local function computeRequiredChunkSet(params, camera, drawDistance)
	local chunkSize = math.max(8.0, tonumber(params.chunkSize) or 128)
	local cameraX = (camera.pos and camera.pos[1]) or 0
	local cameraZ = (camera.pos and camera.pos[3]) or 0
	local desiredDrawDistance = tonumber(drawDistance)
	local gameplayRadius = math.max(
		chunkSize,
		tonumber(params.gameplayRadiusMeters) or (chunkSize * math.max(tonumber(params.lod0Radius) or 4, 4))
	)
	local midRadius = math.max(
		gameplayRadius + chunkSize,
		tonumber(params.midFieldRadiusMeters) or (chunkSize * math.max(tonumber(params.lod1Radius) or 16, 16))
	)
	local horizonRadius = math.max(
		midRadius + chunkSize,
		tonumber(params.horizonRadiusMeters) or (chunkSize * math.max(tonumber(params.lod2Radius) or 64, 64))
	)
	if desiredDrawDistance and desiredDrawDistance > 0 then
		horizonRadius = math.max(horizonRadius, desiredDrawDistance)
	end

	local baseCenterCx = math.floor(cameraX / chunkSize)
	local baseCenterCz = math.floor(cameraZ / chunkSize)
	local leadSeconds = 0.75
	local vel = camera.flightVel or camera.vel or { 0, 0, 0 }
	local leadX = clamp((tonumber(vel[1]) or 0) * leadSeconds, -chunkSize * 8, chunkSize * 8)
	local leadZ = clamp((tonumber(vel[3]) or 0) * leadSeconds, -chunkSize * 8, chunkSize * 8)
	local prefetchCx = math.floor((cameraX + leadX) / chunkSize)
	local prefetchCz = math.floor((cameraZ + leadZ) / chunkSize)

	local required = {}
	local function addBand(lod, innerRadius, outerRadius)
		local chunkWorldSize = getChunkWorldSize(params, lod)
		local centerCx = math.floor(cameraX / chunkWorldSize)
		local centerCz = math.floor(cameraZ / chunkWorldSize)
		local searchRadius = math.max(1, math.ceil(outerRadius / chunkWorldSize) + 1)
		local innerRadiusSq = innerRadius * innerRadius
		local outerRadiusSq = outerRadius * outerRadius
		for dz = -searchRadius, searchRadius do
			for dx = -searchRadius, searchRadius do
				local cx = centerCx + dx
				local cz = centerCz + dz
				local x0, z0, x1, z1 = chunkBoundsForWorldSize(cx, cz, chunkWorldSize)
				local minDistSq = boundsMinDistanceSqToPoint(cameraX, cameraZ, x0, z0, x1, z1)
				local maxDistSq = boundsMaxDistanceSqToPoint(cameraX, cameraZ, x0, z0, x1, z1)
				local overlapsOuter = minDistSq <= outerRadiusSq
				local extendsBeyondInner = innerRadius <= 0 or maxDistSq > innerRadiusSq
				if overlapsOuter and extendsBeyondInner then
					local key = buildChunkKey(cx, cz, lod, params.generatorVersion, params.seed)
					required[key] = {
						cx = cx,
						cz = cz,
						lod = lod
					}
				end
			end
		end
	end

	addBand(0, 0, gameplayRadius)
	addBand(1, gameplayRadius, midRadius)
	addBand(2, midRadius, horizonRadius)

	return required, baseCenterCx, baseCenterCz, math.ceil(horizonRadius / chunkSize), prefetchCx, prefetchCz
end

local function flushChunkQueueSync(terrainState, params, objects, qModule)
	local built = 0
	local baseBudget = math.max(1, math.floor(tonumber(params.meshBuildBudget) or 2))
	local availableCredit = tonumber(terrainState.syncBuildCredit) or baseBudget
	local budget = math.max(0, math.floor(availableCredit + 1e-6))
	if budget <= 0 then
		return false
	end
	local maxFinalizeMs = clamp(tonumber(params.syncBuildTimeBudgetMs) or 4.5, 0.5, 16.0)
	local startedAt = currentTimeSeconds()

	local nextOrder = {}
	for _, key in ipairs(terrainState.chunkOrder) do
		if built >= budget or ((currentTimeSeconds() - startedAt) * 1000.0) >= maxFinalizeMs then
			nextOrder[#nextOrder + 1] = key
		elseif terrainState.buildQueue[key] then
			local cx, cz, lod = parseChunkKey(key)
			local obj = createChunkObject(cx, cz, lod, params, terrainState.fieldContext, qModule)
			replaceChunkAtSlot(terrainState, objects, key, cx, cz, lod, obj)
			terrainState.buildQueue[key] = nil
			built = built + 1
		end
	end
	terrainState.chunkOrder = nextOrder
	terrainState.syncBuildCredit = math.max(0, availableCredit - built)
	return built > 0
end

local function dispatchChunkQueueToWorker(terrainState, params)
	if not terrainState.workerAvailable or not terrainState.workerRequestChannel then
		return false
	end

	local paramsChanged = true
	if type(terrainState.workerParamsToken) == "table" then
		paramsChanged = not terrain.groundParamsEqual(terrainState.workerParamsToken, params)
	end
	if paramsChanged then
		local okParams = pcall(function()
			terrainState.workerRequestChannel:push({
				type = "set_params",
				params = params
			})
		end)
		if not okParams then
			return false
		end
		terrainState.workerParamsToken = cloneWorkerParamsSnapshot(params)
	end

	local maxInflight = clamp(math.floor(tonumber(params.workerMaxInflight) or 2), 1, 6)
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
				if terrainState.workerUsesBinaryJobs and loveLib.data and loveLib.data.pack then
					terrainState.workerRequestChannel:push({
						type = "build_chunk_bin",
						key = key,
						payload = loveLib.data.pack(
							"string",
							"<iiii",
							math.floor(cx),
							math.floor(cz),
							math.floor(lod),
							math.floor(terrainState.workerGeneration)
						)
					})
				else
					terrainState.workerRequestChannel:push({
						type = "build_chunk",
						key = key,
						cx = cx,
						cz = cz,
						lod = lod,
						generation = terrainState.workerGeneration
					})
				end
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

local function collectWorkerChunkResults(terrainState, params, objects, qModule, profiler)
	if not terrainState.workerAvailable or not terrainState.workerResponseChannel then
		return false
	end

	local maxResultsPerFrame = clamp(
		math.floor(tonumber(params and params.workerResultBudgetPerFrame) or 4),
		1,
		12
	)
	local maxFinalizeMs = clamp(
		tonumber(params and params.workerResultTimeBudgetMs) or 3.0,
		0.25,
		20.0
	)
	local startedAt = currentTimeSeconds()
	local processedCount = 0
	local changed = false
	local workerBuildTotalMs = 0
	local workerBuildCount = 0
	local function decodeChunkModelBinary(payload)
		if type(payload) ~= "string" or payload == "" then
			return nil
		end
		if not (
			type(loveLib.data) == "table" and
			type(loveLib.data.decompress) == "function" and
			type(loveLib.data.unpack) == "function"
		) then
			return nil
		end
		local okDecompress, raw = pcall(function()
			return loveLib.data.decompress("string", "lz4", payload)
		end)
		if not okDecompress or type(raw) ~= "string" or raw == "" then
			return nil
		end
		local pos = 1
		local rawLen = #raw
		local function unpackAt(fmt, byteWidth)
			if (pos + byteWidth - 1) > rawLen then
				return nil
			end
			local a, b, c, d, e = loveLib.data.unpack(fmt, raw, pos)
			pos = pos + byteWidth
			return a, b, c, d, e
		end
		local vertexCount, normalCount, colorCount, faceCount = unpackAt("<iiiii", 20)
		vertexCount = math.max(0, math.floor(tonumber(vertexCount) or 0))
		normalCount = math.max(0, math.floor(tonumber(normalCount) or 0))
		colorCount = math.max(0, math.floor(tonumber(colorCount) or 0))
		faceCount = math.max(0, math.floor(tonumber(faceCount) or 0))
		if vertexCount <= 0 or faceCount <= 0 then
			return nil
		end
		if vertexCount > 262144 or normalCount > 262144 or colorCount > 262144 or faceCount > 262144 then
			return nil
		end

		local model = {
			vertices = {},
			faces = {},
			vertexNormals = {},
			vertexColors = {},
			faceColors = {},
			isSolid = true,
			_terrainFastPath = true
		}
		for i = 1, vertexCount do
			local x, y, z = unpackAt("<fff", 12)
			if x == nil then
				return nil
			end
			model.vertices[i] = {
				tonumber(x) or 0,
				tonumber(y) or 0,
				tonumber(z) or 0
			}
		end
		for i = 1, normalCount do
			local nx, ny, nz = unpackAt("<fff", 12)
			if nx == nil then
				return nil
			end
			model.vertexNormals[i] = {
				tonumber(nx) or 0,
				tonumber(ny) or 1,
				tonumber(nz) or 0
			}
		end
		for i = 1, colorCount do
			local r, g, b, a = unpackAt("<ffff", 16)
			if r == nil then
				return nil
			end
			model.vertexColors[i] = {
				tonumber(r) or 1,
				tonumber(g) or 1,
				tonumber(b) or 1,
				tonumber(a) or 1
			}
		end
		for i = 1, faceCount do
			local ia, ib, ic = unpackAt("<iii", 12)
			if ia == nil then
				return nil
			end
			model.faces[i] = {
				math.max(1, math.floor(tonumber(ia) or 1)),
				math.max(1, math.floor(tonumber(ib) or 1)),
				math.max(1, math.floor(tonumber(ic) or 1))
			}
		end
		return model
	end
	while processedCount < maxResultsPerFrame do
		if ((currentTimeSeconds() - startedAt) * 1000.0) >= maxFinalizeMs then
			break
		end
		local msg = terrainState.workerResponseChannel:pop()
		if not msg then
			break
		end
		processedCount = processedCount + 1
		if type(msg) == "table" and msg.type == "build_chunk_done" then
			local key = tostring(msg.key or "")
			terrainState.workerInflight[key] = nil
			local generation = math.floor(tonumber(msg.generation) or -1)
			local buildMs = math.max(0, tonumber(msg.buildMs) or 0)
			if buildMs > 0 then
				workerBuildTotalMs = workerBuildTotalMs + buildMs
				workerBuildCount = workerBuildCount + 1
			end
			if generation == terrainState.workerGeneration and terrainState.requiredSet[key] then
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
					tonumber(msg.meshMaxY),
					terrainState.fieldContext
				)
				replaceChunkAtSlot(terrainState, objects, key, cx, cz, lod, obj)
				changed = true
			end
		elseif type(msg) == "table" and msg.type == "build_chunk_done_bin" then
			local key = tostring(msg.key or "")
			terrainState.workerInflight[key] = nil
			local generation = math.floor(tonumber(msg.generation) or -1)
			local buildMs = math.max(0, tonumber(msg.buildMs) or 0)
			if buildMs > 0 then
				workerBuildTotalMs = workerBuildTotalMs + buildMs
				workerBuildCount = workerBuildCount + 1
			end
			if generation == terrainState.workerGeneration and terrainState.requiredSet[key] then
				local model = decodeChunkModelBinary(msg.payload)
				if model then
					local cx = tonumber(msg.cx) or 0
					local cz = tonumber(msg.cz) or 0
					local lod = tonumber(msg.lod) or 0
					local obj = createChunkObjectFromMesh(
						cx,
						cz,
						lod,
						params,
						qModule,
						model,
						tonumber(msg.meshMinY),
						tonumber(msg.meshMaxY),
						terrainState.fieldContext
					)
					replaceChunkAtSlot(terrainState, objects, key, cx, cz, lod, obj)
					changed = true
				elseif key ~= "" then
					queueChunkBuild(terrainState, key)
				end
			end
		elseif type(msg) == "table" and msg.type == "build_chunk_failed" then
			local key = tostring(msg.key or "")
			terrainState.workerInflight[key] = nil
			if key ~= "" and terrainState.requiredSet[key] then
				queueChunkBuild(terrainState, key)
			end
		end
	end
	if workerBuildCount > 0 then
		terrainState.workerLastBuildMs = workerBuildTotalMs
		terrainState.workerLastBuildCount = workerBuildCount
		terrainState.workerLastBuildAvgMs = workerBuildTotalMs / workerBuildCount
		addProfileSample(
			profiler,
			"terrain.worker_thread_build",
			workerBuildTotalMs,
			terrainProfileColors.workerThreadBuild,
			"terrain worker build"
		)
	else
		terrainState.workerLastBuildMs = 0
		terrainState.workerLastBuildCount = 0
		terrainState.workerLastBuildAvgMs = 0
	end
	return changed
end

local function collapseDisplayedChunkSlots(terrainState, objects, required)
	local winners = {}
	local removals = {}
	for key, obj in pairs(terrainState.chunkMap or {}) do
		if obj then
			local cx = tonumber(obj.chunkX)
			local cz = tonumber(obj.chunkZ)
			local lod = tonumber(obj.chunkLod)
			if cx == nil or cz == nil or lod == nil then
				local parsedCx, parsedCz, parsedLod = parseChunkKey(key)
				cx = cx or parsedCx
				cz = cz or parsedCz
				lod = lod or parsedLod
			end
			cx = math.floor(cx or 0)
			cz = math.floor(cz or 0)
			lod = math.floor(lod or 0)
			local coordKey = buildChunkCoordKey(cx, cz, lod)
			local existing = winners[coordKey]
			if not existing then
				winners[coordKey] = {
					key = key,
					lod = lod,
					required = required and required[key] ~= nil
				}
			else
				local currentRequired = required and required[key] ~= nil
				local keepCurrent = false
				if currentRequired and not existing.required then
					keepCurrent = true
				elseif existing.required and not currentRequired then
					keepCurrent = false
				elseif lod < existing.lod then
					keepCurrent = true
				end
				if keepCurrent then
					removals[#removals + 1] = existing.key
					winners[coordKey] = {
						key = key,
						lod = lod,
						required = currentRequired
					}
				else
					removals[#removals + 1] = key
				end
			end
		end
	end

	local changed = false
	for i = 1, #removals do
		if removeChunkByKey(terrainState, objects, removals[i], false) then
			changed = true
		end
	end
	terrainState.chunkSlotMap = terrainState.chunkSlotMap or {}
	for coordKey in pairs(terrainState.chunkSlotMap) do
		terrainState.chunkSlotMap[coordKey] = nil
	end
	for coordKey, winner in pairs(winners) do
		if terrainState.chunkMap[winner.key] then
			terrainState.chunkSlotMap[coordKey] = winner.key
		end
	end
	return changed
end

local function buildDisplayedCoverageEntries(terrainState, required, params)
	local entries = {}
	for key, obj in pairs(terrainState.chunkMap or {}) do
		if required[key] and terrainState.staleChunkKeys[key] ~= true and obj then
			local cx = tonumber(obj.chunkX)
			local cz = tonumber(obj.chunkZ)
			local lod = tonumber(obj.chunkLod)
			if cx == nil or cz == nil or lod == nil then
				local parsedCx, parsedCz, parsedLod = parseChunkKey(key)
				cx = cx or parsedCx
				cz = cz or parsedCz
				lod = lod or parsedLod
			end
			local x0, z0, x1, z1 = chunkBoundsForWorldSize(cx or 0, cz or 0, getChunkWorldSize(params, lod or 0))
			entries[#entries + 1] = {
				x0 = x0,
				z0 = z0,
				x1 = x1,
				z1 = z1
			}
		end
	end
	return entries
end

local function isBoundsCoveredByEntries(bounds, entries)
	if type(bounds) ~= "table" or type(entries) ~= "table" or #entries <= 0 then
		return false
	end
	local spanX = math.max(0.001, (bounds.x1 or 0) - (bounds.x0 or 0))
	local spanZ = math.max(0.001, (bounds.z1 or 0) - (bounds.z0 or 0))
	local xs = {
		(bounds.x0 or 0) + spanX * 0.12,
		((bounds.x0 or 0) + (bounds.x1 or 0)) * 0.5,
		(bounds.x1 or 0) - spanX * 0.12
	}
	local zs = {
		(bounds.z0 or 0) + spanZ * 0.12,
		((bounds.z0 or 0) + (bounds.z1 or 0)) * 0.5,
		(bounds.z1 or 0) - spanZ * 0.12
	}
	for zi = 1, #zs do
		for xi = 1, #xs do
			local covered = false
			for i = 1, #entries do
				if pointInsideBounds(xs[xi], zs[zi], entries[i]) then
					covered = true
					break
				end
			end
			if not covered then
				return false
			end
		end
	end
	return true
end

local function pruneRetainedChunks(terrainState, objects, required, centerCx, centerCz, retentionRadius, params)
	local requiredSlots = {}
	local requiredCount = countTableKeys(required)
	local retainedStale = {}
	local displayedCoverageEntries = buildDisplayedCoverageEntries(terrainState, required, params)
	local removalBudget = clamp(
		math.floor(tonumber(params and params.pruneRemovalBudgetPerFrame) or 96),
		16,
		512
	)
	local removalsUsed = 0
	for key, info in pairs(required or {}) do
		requiredSlots[buildChunkCoordKey(info.cx, info.cz, info.lod)] = key
	end

	local changed = false
	local function removeWithBudget(key, cacheSafe)
		if removalsUsed >= removalBudget then
			return false
		end
		if removeChunkByKey(terrainState, objects, key, cacheSafe) then
			removalsUsed = removalsUsed + 1
			changed = true
			return true
		end
		return false
	end
	for key, obj in pairs(terrainState.chunkMap) do
		if not required[key] and obj then
			local cx, cz, lod = parseChunkKey(key)
			local slotKey = buildChunkCoordKey(cx, cz, lod)
			local replacementKey = requiredSlots[slotKey]
			local hasReplacement = replacementKey and terrainState.chunkMap[replacementKey] ~= nil and
				terrainState.staleChunkKeys[replacementKey] ~= true
			local x0, z0, x1, z1 = chunkBoundsForWorldSize(cx, cz, getChunkWorldSize(params, lod))
			local hasCoverageReplacement = hasReplacement or isBoundsCoveredByEntries({
				x0 = x0,
				z0 = z0,
				x1 = x1,
				z1 = z1
			}, displayedCoverageEntries)
			local tooFar = chunkRingDistance(cx, cz, centerCx, centerCz) > retentionRadius
			local cacheSafe = (terrainState.staleChunkKeys[key] ~= true) and (not hasCoverageReplacement)
			if hasCoverageReplacement or tooFar then
				removeWithBudget(key, cacheSafe and tooFar)
			else
				retainedStale[#retainedStale + 1] = {
					key = key,
					dist = chunkRingDistance(cx, cz, centerCx, centerCz),
					lod = lod,
					requiredCoord = replacementKey ~= nil
				}
			end
		end
	end

	local configuredStaleBudget = math.max(8, math.floor(tonumber(params and params.maxStaleChunks) or 32))
	local staleBudget = math.max(8, math.floor(math.min(configuredStaleBudget, math.max(8, requiredCount * 0.20))))
	local removableStale = {}
	for i = 1, #retainedStale do
		if retainedStale[i].requiredCoord ~= true then
			removableStale[#removableStale + 1] = retainedStale[i]
		end
	end
	if #removableStale > staleBudget then
		table.sort(removableStale, function(a, b)
			if a.dist ~= b.dist then
				return a.dist > b.dist
			end
			return (a.lod or 0) > (b.lod or 0)
		end)
		for i = 1, (#removableStale - staleBudget) do
			if removalsUsed >= removalBudget then
				break
			end
			removeWithBudget(removableStale[i].key, false)
		end
	end

	local maxDisplayed = math.max(
		requiredCount + 16,
		math.floor(tonumber(params and params.maxDisplayedChunks) or (requiredCount + configuredStaleBudget))
	)
	local displayedCount = countTableKeys(terrainState.chunkMap)
	if displayedCount > maxDisplayed then
		table.sort(removableStale, function(a, b)
			if a.dist ~= b.dist then
				return a.dist > b.dist
			end
			return (a.lod or 0) > (b.lod or 0)
		end)
		for i = 1, #removableStale do
			if displayedCount <= maxDisplayed then
				break
			end
			if removeWithBudget(removableStale[i].key, false) then
				displayedCount = displayedCount - 1
			elseif removalsUsed >= removalBudget then
				break
			end
		end
		if displayedCount > maxDisplayed then
			table.sort(retainedStale, function(a, b)
				if a.dist ~= b.dist then
					return a.dist > b.dist
				end
				return (a.lod or 0) > (b.lod or 0)
			end)
			for i = 1, #retainedStale do
				if displayedCount <= maxDisplayed then
					break
				end
				if removeWithBudget(retainedStale[i].key, false) then
					displayedCount = displayedCount - 1
				elseif removalsUsed >= removalBudget then
					break
				end
			end
		end
	end
	return changed
end

local function chunkBoundsForCoords(params, cx, cz, lod)
	local worldSize = getChunkWorldSize(params, lod)
	return chunkBoundsForWorldSize(cx, cz, worldSize)
end

local function boundsOverlap(ax0, az0, ax1, az1, bx0, bz0, bx1, bz1)
	return ax0 < bx1 and ax1 > bx0 and az0 < bz1 and az1 > bz0
end

local function invalidateChunksForWorldEdits(terrainState, params, changedChunks)
	if type(terrainState) ~= "table" or type(params) ~= "table" or type(changedChunks) ~= "table" then
		return false
	end
	local worldChunkSize = math.max(8.0, tonumber(params.chunkSize) or 128)
	local changed = false
	for key, obj in pairs(terrainState.chunkMap or {}) do
		local cx = tonumber(obj and obj.chunkX)
		local cz = tonumber(obj and obj.chunkZ)
		local lod = tonumber(obj and obj.chunkLod)
		if cx == nil or cz == nil or lod == nil then
			local parsedCx, parsedCz, parsedLod = parseChunkKey(key)
			cx = cx or parsedCx
			cz = cz or parsedCz
			lod = lod or parsedLod
		end
		local rx0, rz0, rx1, rz1 = chunkBoundsForCoords(params, cx or 0, cz or 0, lod or 0)
		for i = 1, #changedChunks do
			local chunkState = changedChunks[i]
			local wcx = math.floor(tonumber(chunkState and chunkState.cx) or 0)
			local wcz = math.floor(tonumber(chunkState and chunkState.cz) or 0)
			local wx0 = wcx * worldChunkSize
			local wz0 = wcz * worldChunkSize
			local wx1 = wx0 + worldChunkSize
			local wz1 = wz0 + worldChunkSize
			if boundsOverlap(rx0, rz0, rx1, rz1, wx0, wz0, wx1, wz1) then
				terrainState.staleChunkKeys[key] = true
				queueChunkBuild(terrainState, key)
				changed = true
				break
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
	local profiler = context.profiler
	local runtimeScope = beginProfileScope(
		profiler,
		"terrain.runtime",
		terrainProfileColors.runtime,
		"terrain runtime"
	)
	local runtimeParams = deriveRuntimeParams(
		params,
		terrainState,
		tonumber(context.frameTimeMs) or ((tonumber(context.dt) or 0) * 1000.0)
	)
	local requestedDrawDistance = tonumber(context.drawDistance)
	if runtimeParams.drawDistanceOverridesLodRadius == true and requestedDrawDistance and requestedDrawDistance > 0 then
		local baseDisplayedChunks = math.max(
			128,
			math.floor(
				tonumber(params.maxDisplayedChunks) or
				tonumber(runtimeParams.maxDisplayedChunks) or
				512
			)
		)
		local maxDisplayedChunksHardCap = math.max(
			128,
			math.floor(tonumber(runtimeParams.maxDisplayedChunksHardCap) or 8192)
		)
		local autoScaleDisplayedCap = math.min(
			maxDisplayedChunksHardCap,
			math.max(baseDisplayedChunks, math.floor(baseDisplayedChunks * 2.0))
		)
		local chunkSizeForRadius = math.max(16.0, tonumber(runtimeParams.chunkSize) or 128)
		local derivedRadius = math.max(1, math.ceil(requestedDrawDistance / math.max(16.0, chunkSizeForRadius * 3.6)))
		local desiredDisplayedChunks = math.max(
			baseDisplayedChunks,
			((derivedRadius * 2 + 1) * (derivedRadius * 2 + 1))
		)
		runtimeParams.maxDisplayedChunks = clamp(
			math.floor(desiredDisplayedChunks),
			128,
			autoScaleDisplayedCap
		)
	end
	endProfileScope(profiler, runtimeScope)

	local signatureScope = beginProfileScope(
		profiler,
		"terrain.runtime_signature",
		terrainProfileColors.runtime,
		"terrain signature"
	)
	local runtimeMeshSignature = table.concat({
		tostring(runtimeParams.lod0Radius),
		tostring(runtimeParams.lod1Radius),
		tostring(runtimeParams.lod2Radius),
		tostring(runtimeParams.lod0ChunkScale),
		tostring(runtimeParams.lod1ChunkScale),
		tostring(runtimeParams.lod2ChunkScale),
		string.format("%.1f", tonumber(runtimeParams.gameplayRadiusMeters) or 0),
		string.format("%.1f", tonumber(runtimeParams.midFieldRadiusMeters) or 0),
		string.format("%.1f", tonumber(runtimeParams.horizonRadiusMeters) or 0),
		string.format("%.3f", tonumber(runtimeParams.lod0CellSize) or 0),
		string.format("%.3f", tonumber(runtimeParams.lod1CellSize) or 0),
		string.format("%.3f", tonumber(runtimeParams.lod2CellSize) or 0)
	}, ":")
	local runtimeRebuildSignature
	if runtimeParams.autoQualityEnabled == true then
		-- Adaptive quality can vary continuously; rebuilding every visible chunk on each
		-- quality tick causes severe hitching. Keep topology stable and let new chunks
		-- pick up quality changes incrementally.
		runtimeRebuildSignature = table.concat({
			tostring(runtimeParams.lod0Radius),
			tostring(runtimeParams.lod1Radius),
			tostring(runtimeParams.lod2Radius),
			tostring(runtimeParams.lod0ChunkScale),
			tostring(runtimeParams.lod1ChunkScale),
			tostring(runtimeParams.lod2ChunkScale)
		}, ":")
	else
		runtimeRebuildSignature = runtimeMeshSignature
	end

	if (not terrainState.fieldContext) or (terrainState.activeGroundParams ~= params) then
		local hadFieldContext = terrainState.fieldContext ~= nil
		terrainState.activeGroundParams = params
		terrainState.fieldContext = sdfField.createContext(params)
		terrainState.generatorVersion = params.generatorVersion or 1
		terrainState.chunkCacheLimit = clamp(
			math.floor(tonumber(params.chunkCacheLimit) or terrainState.chunkCacheLimit or 128),
			32,
			256
		)
		terrainState.buildQueue = {}
		terrainState.chunkOrder = {}
		terrainState.requiredSet = {}
		terrainState.targetRequiredSet = {}
		terrainState.syncBuildCredit = nil
		clearChunkCache(terrainState)
		resetWorkerGeneration(terrainState)
		if hadFieldContext then
			markDisplayedChunksStale(terrainState)
		end
		forceRebuild = true
	end
	if terrainState.runtimeMeshSignature ~= runtimeMeshSignature then
		terrainState.runtimeMeshSignature = runtimeMeshSignature
		terrainState.workerParamsToken = nil
		if runtimeParams.autoQualityEnabled ~= true then
			markDisplayedChunksStale(terrainState)
			forceRebuild = true
		end
	end
	if terrainState.runtimeRebuildSignature ~= runtimeRebuildSignature then
		terrainState.runtimeRebuildSignature = runtimeRebuildSignature
		markDisplayedChunksStale(terrainState)
		forceRebuild = true
	end
	endProfileScope(profiler, signatureScope)

	local requiredSetScope = beginProfileScope(
		profiler,
		"terrain.required_set",
		terrainProfileColors.requiredSet,
		"terrain required"
	)
	local required, centerCx, centerCz, requiredRadius, prefetchCx, prefetchCz = computeRequiredChunkSet(
		runtimeParams,
		camera,
		context.drawDistance
	)
	endProfileScope(profiler, requiredSetScope)
	local changed = false
	terrainState.requiredSet = required
	terrainState.targetRequiredSet = required
	terrainState.lastRequiredRadius = requiredRadius
	local slotCollapseScope = beginProfileScope(
		profiler,
		"terrain.slot_collapse",
		terrainProfileColors.slotCollapse,
		"terrain slot collapse"
	)
	if collapseDisplayedChunkSlots(terrainState, objects, required) then
		changed = true
	end
	endProfileScope(profiler, slotCollapseScope)
	local centerChanged = (centerCx ~= terrainState.centerChunkX) or (centerCz ~= terrainState.centerChunkZ)
	terrainState.centerChunkX = centerCx
	terrainState.centerChunkZ = centerCz

	if forceRebuild or centerChanged then
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

	local missingScanScope = beginProfileScope(
		profiler,
		"terrain.missing_scan",
		terrainProfileColors.missingScan,
		"terrain missing scan"
	)
	local missingKeys = {}
	for key, info in pairs(required) do
		if ((not terrainState.chunkMap[key]) or terrainState.staleChunkKeys[key]) and
			(not terrainState.buildQueue[key]) and
			(not terrainState.workerInflight[key]) then
			missingKeys[#missingKeys + 1] = {
				key = key,
				cx = info.cx,
				cz = info.cz,
				lod = info.lod
			}
		end
	end
	table.sort(missingKeys, function(a, b)
		local da = chunkRingDistance(a.cx, a.cz, prefetchCx or centerCx, prefetchCz or centerCz)
		local db = chunkRingDistance(b.cx, b.cz, prefetchCx or centerCx, prefetchCz or centerCz)
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
	endProfileScope(profiler, missingScanScope)

	local requiredCount = countTableKeys(required)
	local configuredPendingLimit = math.max(16, math.floor(tonumber(runtimeParams.maxPendingChunks) or 96))
	local maxPendingBacklog = clamp(
		math.max(
			32,
			math.floor((tonumber(runtimeParams.meshBuildBudget) or 2) * 8),
			math.floor((tonumber(runtimeParams.workerMaxInflight) or 2) * 8)
		),
		16,
		math.min(math.max(32, requiredCount), configuredPendingLimit)
	)

	-- Keep queue bookkeeping coherent and bounded; stale/orphaned keys can otherwise
	-- inflate backlog and stall near-camera chunk builds.
	local queueCompactionScope = beginProfileScope(
		profiler,
		"terrain.queue_compaction",
		terrainProfileColors.queueCompaction,
		"terrain queue compact"
	)
	do
		local compactedOrder = {}
		local seen = {}
		for _, key in ipairs(terrainState.chunkOrder or {}) do
			if terrainState.buildQueue[key] and required[key] and (not seen[key]) then
				seen[key] = true
				compactedOrder[#compactedOrder + 1] = key
			else
				terrainState.buildQueue[key] = nil
			end
		end
		for key in pairs(terrainState.buildQueue) do
			if required[key] and (not seen[key]) then
				seen[key] = true
				compactedOrder[#compactedOrder + 1] = key
			elseif not required[key] then
				terrainState.buildQueue[key] = nil
			end
		end
		if #compactedOrder > maxPendingBacklog then
			for i = maxPendingBacklog + 1, #compactedOrder do
				terrainState.buildQueue[compactedOrder[i]] = nil
			end
			for i = #compactedOrder, maxPendingBacklog + 1, -1 do
				compactedOrder[i] = nil
			end
		end
		terrainState.chunkOrder = compactedOrder
	end
	endProfileScope(profiler, queueCompactionScope)

	local queueFillScope = beginProfileScope(
		profiler,
		"terrain.queue_fill",
		terrainProfileColors.queueFill,
		"terrain queue fill"
	)
	local pendingBacklog = countPendingBuilds(terrainState) + countInflight(terrainState)
	for _, item in ipairs(missingKeys) do
		local key = item.key
		local cachedObj = terrainState.staleChunkKeys[key] and nil or takeCachedChunkObject(terrainState, key)
		if cachedObj then
			replaceChunkAtSlot(terrainState, objects, key, item.cx, item.cz, item.lod, cachedObj)
			changed = true
		elseif pendingBacklog < maxPendingBacklog then
			queueChunkBuild(terrainState, key)
			pendingBacklog = pendingBacklog + 1
			changed = true
		end
	end
	endProfileScope(profiler, queueFillScope)

	local workerHealthScope = beginProfileScope(
		profiler,
		"terrain.worker_health",
		terrainProfileColors.workerHealth,
		"terrain worker health"
	)
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
	endProfileScope(profiler, workerHealthScope)

	local useWorker = (runtimeParams.threadedMeshing ~= false) and (runtimeParams.worldStore == nil) and
		initializeWorker(terrainState)
	if useWorker then
		local collectScope = beginProfileScope(
			profiler,
			"terrain.worker_collect",
			terrainProfileColors.workerCollect,
			"terrain worker collect"
		)
		if collectWorkerChunkResults(terrainState, runtimeParams, objects, context.q, profiler) then
			changed = true
		end
		endProfileScope(profiler, collectScope)

		local dispatchScope = beginProfileScope(
			profiler,
			"terrain.worker_dispatch",
			terrainProfileColors.workerDispatch,
			"terrain worker dispatch"
		)
		if dispatchChunkQueueToWorker(terrainState, runtimeParams) then
			changed = true
		end
		endProfileScope(profiler, dispatchScope)
	else
		local syncBuildScope = beginProfileScope(
			profiler,
			"terrain.sync_build",
			terrainProfileColors.syncBuild,
			"terrain sync build"
		)
		replenishSyncBuildCredit(terrainState, runtimeParams, context.dt)
		if flushChunkQueueSync(terrainState, runtimeParams, objects, context.q) then
			changed = true
		end
		endProfileScope(profiler, syncBuildScope)
	end

	local retentionRadius = math.max(requiredRadius or 0, math.floor(tonumber(runtimeParams.lod2Radius) or 0)) + 1
	local pruneScope = beginProfileScope(
		profiler,
		"terrain.prune",
		terrainProfileColors.prune,
		"terrain prune"
	)
	if pruneRetainedChunks(terrainState, objects, required, centerCx, centerCz, retentionRadius, runtimeParams) then
		changed = true
	end
	endProfileScope(profiler, pruneScope)

	local statsScope = beginProfileScope(
		profiler,
		"terrain.stats",
		terrainProfileColors.stats,
		"terrain stats"
	)
	local displayedChunks = countTableKeys(terrainState.chunkMap)
	local missingRequiredChunks = 0
	local staleDisplayedChunks = 0
	for key in pairs(required) do
		if (not terrainState.chunkMap[key]) or terrainState.staleChunkKeys[key] == true then
			missingRequiredChunks = missingRequiredChunks + 1
		end
	end
	for key in pairs(terrainState.chunkMap) do
		if not required[key] then
			staleDisplayedChunks = staleDisplayedChunks + 1
		end
	end
	terrainState.displayedChunks = displayedChunks
	terrainState.missingRequiredChunks = missingRequiredChunks
	terrainState.staleDisplayedChunks = staleDisplayedChunks
	terrainState.buildQueueSize = countPendingBuilds(terrainState)
	terrainState.workerInflightCount = countInflight(terrainState)
	endProfileScope(profiler, statsScope)

	return changed, terrainState
end

function terrain.rebuildGroundFromParams(params, reason, context)
	local normalized = terrain.normalizeGroundParams(params, context.defaultGroundParams or params or {})
	local terrainState = ensureTerrainState(context)
	if terrainState.activeGroundParams and terrain.groundParamsEqual(terrainState.activeGroundParams, normalized) then
		return false
	end

	terrainState.buildQueue = {}
	terrainState.chunkOrder = {}
	terrainState.centerChunkX = math.huge
	terrainState.centerChunkZ = math.huge
	terrainState.requiredSet = {}
	terrainState.targetRequiredSet = {}
	terrainState.syncBuildCredit = nil
	terrainState.chunkCacheLimit = clamp(
		math.floor(tonumber(normalized.chunkCacheLimit) or terrainState.chunkCacheLimit or 128),
		32,
		256
	)
	clearChunkCache(terrainState)
	terrainState.activeGroundParams = normalized
	terrainState.fieldContext = sdfField.createContext(normalized)
	terrainState.generatorVersion = normalized.generatorVersion or 1
	markDisplayedChunksStale(terrainState)
	resetWorkerGeneration(terrainState)

	local changed = terrain.updateGroundStreaming(true, context)
	local worldHalfExtent = math.max(
		normalized.chunkSize * (normalized.lod2Radius + 1),
		tonumber(normalized.horizonRadiusMeters) or 0
	)

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

	local world = activeParams.worldStore or context.worldStore
	if type(world) == "table" and type(world.applyCrater) == "function" then
		local craterChanged, changedChunks = world:applyCrater(craterSpec)
		if not craterChanged then
			return false
		end
		if context.camera and context.objects then
			invalidateChunksForWorldEdits(terrainState, activeParams, changedChunks)
			local changed, nextTerrainState = terrain.updateGroundStreaming(false, {
				terrainState = terrainState,
				activeGroundParams = activeParams,
				camera = context.camera,
				objects = context.objects,
				q = context.q
			})
			if nextTerrainState then
				terrainState = nextTerrainState
			end
			return true, {
				changed = changed and true or false,
				crater = {
					x = tonumber(craterSpec.x) or 0,
					y = tonumber(craterSpec.y) or 0,
					z = tonumber(craterSpec.z) or 0,
					radius = math.max(1.0, tonumber(craterSpec.radius) or 8.0),
					depth = math.max(0.4, tonumber(craterSpec.depth) or (math.max(1.0, tonumber(craterSpec.radius) or 8.0) * 0.45)),
					rim = clamp(tonumber(craterSpec.rim) or 0.12, 0.0, 0.75)
				},
				activeGroundParams = activeParams,
				terrainState = terrainState,
				changedChunks = changedChunks
			}
		end
		return true, {
			changed = true,
			crater = {
				x = tonumber(craterSpec.x) or 0,
				y = tonumber(craterSpec.y) or 0,
				z = tonumber(craterSpec.z) or 0,
				radius = math.max(1.0, tonumber(craterSpec.radius) or 8.0),
				depth = math.max(0.4, tonumber(craterSpec.depth) or (math.max(1.0, tonumber(craterSpec.radius) or 8.0) * 0.45)),
				rim = clamp(tonumber(craterSpec.rim) or 0.12, 0.0, 0.75)
			},
			activeGroundParams = activeParams,
			terrainState = terrainState,
			changedChunks = changedChunks
		}
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
	terrainState.chunkCacheLimit = clamp(
		math.floor(tonumber(normalized.chunkCacheLimit) or terrainState.chunkCacheLimit or 128),
		32,
		256
	)
	terrainState.buildQueue = {}
	terrainState.chunkOrder = {}
	terrainState.requiredSet = {}
	terrainState.targetRequiredSet = {}
	terrainState.centerChunkX = math.huge
	terrainState.centerChunkZ = math.huge
	terrainState.syncBuildCredit = nil
	clearChunkCache(terrainState)
	markDisplayedChunksStale(terrainState)
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

function terrain.applyWorldChunkStates(chunkStates, context)
	context = context or {}
	local terrainState = ensureTerrainState(context)
	local activeParams = context.activeGroundParams or terrainState.activeGroundParams
	if type(activeParams) ~= "table" then
		return false
	end
	local world = activeParams.worldStore or context.worldStore
	if type(world) ~= "table" or type(world.applyChunkState) ~= "function" then
		return false
	end
	local changedChunks = {}
	local anyChanged = false
	for i = 1, #(chunkStates or {}) do
		local chunkState = chunkStates[i]
		if type(chunkState) == "table" and world:applyChunkState(chunkState) then
			changedChunks[#changedChunks + 1] = {
				cx = math.floor(tonumber(chunkState.cx) or 0),
				cz = math.floor(tonumber(chunkState.cz) or 0),
				revision = math.floor(tonumber(chunkState.revision) or 0)
			}
			anyChanged = true
		end
	end
	if not anyChanged then
		return false
	end
	if context.camera and context.objects then
		invalidateChunksForWorldEdits(terrainState, activeParams, changedChunks)
		local changed, nextTerrainState = terrain.updateGroundStreaming(false, {
			terrainState = terrainState,
			activeGroundParams = activeParams,
			camera = context.camera,
			objects = context.objects,
			q = context.q
		})
		if nextTerrainState then
			terrainState = nextTerrainState
		end
		return true, {
			changed = changed and true or false,
			activeGroundParams = activeParams,
			terrainState = terrainState,
			changedChunks = changedChunks
		}
	end
	return true, {
		changed = true,
		activeGroundParams = activeParams,
		terrainState = terrainState,
		changedChunks = changedChunks
	}
end

return terrain
