local requestChannelName, responseChannelName, portSourceRoot = ...

local function appendPackagePath(rootPath)
	local normalized = tostring(rootPath or ""):gsub("\\", "/"):gsub("/+$", "")
	if normalized ~= "" then
		normalized = normalized .. "/"
	end
	package.path = table.concat({
		package.path,
		normalized .. "?.lua",
		normalized .. "?/init.lua"
	}, ";")
end

appendPackagePath(portSourceRoot)

local sdfField = require "Source.Sim.SdfTerrainField"
local marching = require "Source.Sim.MarchingCubes"

if type(love) ~= "table" or type(love.thread) ~= "table" then
	return
end

local requestChannel = love.thread.getChannel(requestChannelName)
local responseChannel = love.thread.getChannel(responseChannelName)
local cachedContextKey = nil
local cachedFieldContext = nil
local activeParams = nil

local function nowSeconds()
	if type(love) == "table" and type(love.timer) == "table" and type(love.timer.getTime) == "function" then
		return love.timer.getTime()
	end
	return os.clock()
end

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
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

local function buildChunkMesh(cx, cz, lod, params, fieldContext)
	local chunkWorldSize = (tonumber(params.chunkSize) or 128) * getLodChunkScale(params, lod)
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

	return model, meshMinY, meshMaxY
end

local function encodeChunkModelBinary(model)
	if type(model) ~= "table" then
		return nil
	end
	if not (type(love.data) == "table" and type(love.data.pack) == "function" and type(love.data.compress) == "function") then
		return nil
	end
	local vertices = type(model.vertices) == "table" and model.vertices or {}
	local normals = type(model.vertexNormals) == "table" and model.vertexNormals or {}
	local colors = type(model.vertexColors) == "table" and model.vertexColors or {}
	local faces = type(model.faces) == "table" and model.faces or {}
	local vertexCount = #vertices
	local normalCount = #normals
	local colorCount = #colors
	local faceCount = #faces
	if vertexCount <= 0 or faceCount <= 0 then
		return nil
	end
	if vertexCount > 262144 or normalCount > 262144 or colorCount > 262144 or faceCount > 262144 then
		return nil
	end

	local parts = {}
	parts[#parts + 1] = love.data.pack(
		"string",
		"<iiiii",
		math.floor(vertexCount),
		math.floor(normalCount),
		math.floor(colorCount),
		math.floor(faceCount),
		1
	)
	for i = 1, vertexCount do
		local v = vertices[i] or {}
		parts[#parts + 1] = love.data.pack(
			"string",
			"<fff",
			tonumber(v[1]) or 0,
			tonumber(v[2]) or 0,
			tonumber(v[3]) or 0
		)
	end
	for i = 1, normalCount do
		local n = normals[i] or {}
		parts[#parts + 1] = love.data.pack(
			"string",
			"<fff",
			tonumber(n[1]) or 0,
			tonumber(n[2]) or 1,
			tonumber(n[3]) or 0
		)
	end
	for i = 1, colorCount do
		local c = colors[i] or {}
		parts[#parts + 1] = love.data.pack(
			"string",
			"<ffff",
			tonumber(c[1]) or 1,
			tonumber(c[2]) or 1,
			tonumber(c[3]) or 1,
			tonumber(c[4]) or 1
		)
	end
	for i = 1, faceCount do
		local f = faces[i] or {}
		parts[#parts + 1] = love.data.pack(
			"string",
			"<iii",
			math.max(1, math.floor(tonumber(f[1]) or 1)),
			math.max(1, math.floor(tonumber(f[2]) or 1)),
			math.max(1, math.floor(tonumber(f[3]) or 1))
		)
	end
	local raw = table.concat(parts)
	if raw == "" then
		return nil
	end
	local compressed = love.data.compress("string", "lz4", raw)
	if type(compressed) == "string" and compressed ~= "" then
		return compressed
	end
	return nil
end

local function pushChunkDone(key, cx, cz, lod, generation, meshMinY, meshMaxY, buildMs, model)
	local binaryPayload = nil
	local okBinary = pcall(function()
		binaryPayload = encodeChunkModelBinary(model)
	end)
	if okBinary and type(binaryPayload) == "string" and binaryPayload ~= "" then
		responseChannel:push({
			type = "build_chunk_done_bin",
			key = key,
			cx = cx,
			cz = cz,
			lod = lod,
			generation = generation,
			meshMinY = meshMinY,
			meshMaxY = meshMaxY,
			buildMs = buildMs,
			payload = binaryPayload
		})
		return
	end
	responseChannel:push({
		type = "build_chunk_done",
		key = key,
		cx = cx,
		cz = cz,
		lod = lod,
		generation = generation,
		meshMinY = meshMinY,
		meshMaxY = meshMaxY,
		buildMs = buildMs,
		model = model
	})
end

local function paramsSignature(params)
	params = params or {}
	return table.concat({
		tostring(math.floor(tonumber(params.seed) or 0)),
		tostring(math.floor(tonumber(params.generatorVersion) or 1)),
		tostring(tonumber(params.chunkSize) or 64),
		tostring(tonumber(params.heightAmplitude) or 120),
		tostring(tonumber(params.heightFrequency) or 0.0018),
		tostring(tonumber(params.ridgeAmplitude) or 38),
		tostring(tonumber(params.ridgeFrequency) or 0.0042),
		tostring(tonumber(params.terraceStrength) or 0.16),
		tostring(tonumber(params.waterLevel) or -12),
		tostring(tonumber(params.waterWaveAmplitude) or 1.6),
		tostring(math.floor(tonumber(params.tunnelCount) or 0)),
		tostring((params.caveEnabled ~= false) and 1 or 0),
		tostring(tonumber(params.caveFrequency) or 0.018),
		tostring(tonumber(params.caveThreshold) or 0.68),
		tostring(math.floor(tonumber(params.lod0ChunkScale) or 1)),
		tostring(math.floor(tonumber(params.lod1ChunkScale) or 4)),
		tostring(math.floor(tonumber(params.lod2ChunkScale) or 16)),
		tostring(tonumber(params.gameplayRadiusMeters) or 512),
		tostring(tonumber(params.midFieldRadiusMeters) or 2048),
		tostring(tonumber(params.horizonRadiusMeters) or 8192)
	}, "|")
end

while true do
	local job = requestChannel:demand()
	if type(job) == "table" then
		if job.type == "quit" then
			break
		elseif job.type == "set_params" then
			activeParams = type(job.params) == "table" and job.params or activeParams
		elseif job.type == "build_chunk" then
			local params = type(activeParams) == "table" and activeParams or {}
			local cx = tonumber(job.cx) or 0
			local cz = tonumber(job.cz) or 0
			local lod = clamp(math.floor(tonumber(job.lod) or 0), 0, 8)
			local generation = math.floor(tonumber(job.generation) or 0)
			local key = tostring(job.key or "")
			local contextKey = paramsSignature(params)
			local startedAt = nowSeconds()

			local ok, model, meshMinY, meshMaxY = pcall(function()
				if cachedContextKey ~= contextKey or not cachedFieldContext then
					cachedFieldContext = sdfField.createContext(params)
					cachedContextKey = contextKey
				end
				return buildChunkMesh(cx, cz, lod, params, cachedFieldContext)
			end)
			local buildMs = math.max(0, (nowSeconds() - startedAt) * 1000.0)

			if ok then
				pushChunkDone(key, cx, cz, lod, generation, meshMinY, meshMaxY, buildMs, model)
			else
				responseChannel:push({
					type = "build_chunk_failed",
					key = key,
					cx = cx,
					cz = cz,
					lod = lod,
					generation = generation,
					buildMs = buildMs
				})
			end
		elseif job.type == "build_chunk_bin" then
			local params = type(activeParams) == "table" and activeParams or {}
			local key = tostring(job.key or "")
			local cx, cz, lod, generation = 0, 0, 0, 0
			if type(love.data) == "table" and type(love.data.unpack) == "function" and type(job.payload) == "string" then
				local ux, uz, ulod, ugeneration = love.data.unpack("<iiii", job.payload)
				cx = tonumber(ux) or 0
				cz = tonumber(uz) or 0
				lod = clamp(math.floor(tonumber(ulod) or 0), 0, 8)
				generation = math.floor(tonumber(ugeneration) or 0)
			end
			local contextKey = paramsSignature(params)
			local startedAt = nowSeconds()

			local ok, model, meshMinY, meshMaxY = pcall(function()
				if cachedContextKey ~= contextKey or not cachedFieldContext then
					cachedFieldContext = sdfField.createContext(params)
					cachedContextKey = contextKey
				end
				return buildChunkMesh(cx, cz, lod, params, cachedFieldContext)
			end)
			local buildMs = math.max(0, (nowSeconds() - startedAt) * 1000.0)

			if ok then
				pushChunkDone(key, cx, cz, lod, generation, meshMinY, meshMaxY, buildMs, model)
			else
				responseChannel:push({
					type = "build_chunk_failed",
					key = key,
					cx = cx,
					cz = cz,
					lod = lod,
					generation = generation,
					buildMs = buildMs
				})
			end
		end
	end
end
