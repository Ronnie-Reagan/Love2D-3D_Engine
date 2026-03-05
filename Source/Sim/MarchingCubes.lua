local marching = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function sub(a, b)
	return { a[1] - b[1], a[2] - b[2], a[3] - b[3] }
end

local function cross(a, b)
	return {
		a[2] * b[3] - a[3] * b[2],
		a[3] * b[1] - a[1] * b[3],
		a[1] * b[2] - a[2] * b[1]
	}
end

local function dot(a, b)
	return a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
end

local function length(v)
	return math.sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
end

local function normalize(v)
	local len = length(v)
	if len <= 1e-8 then
		return { 0, 1, 0 }
	end
	return { v[1] / len, v[2] / len, v[3] / len }
end

local function lerp(a, b, t)
	return a + (b - a) * t
end

local function interpolate(p1, p2, v1, v2, iso)
	local denom = v2 - v1
	local t = 0.5
	if math.abs(denom) > 1e-8 then
		t = clamp((iso - v1) / denom, 0, 1)
	end
	return {
		lerp(p1[1], p2[1], t),
		lerp(p1[2], p2[2], t),
		lerp(p1[3], p2[3], t)
	}
end

local tetrahedra = {
	{ 1, 2, 4, 6 },
	{ 1, 4, 5, 6 },
	{ 2, 3, 4, 6 },
	{ 3, 4, 6, 7 },
	{ 4, 5, 6, 8 },
	{ 4, 6, 7, 8 }
}

local cubeOffsets = {
	{ 0, 0, 0 },
	{ 1, 0, 0 },
	{ 1, 0, 1 },
	{ 0, 0, 1 },
	{ 0, 1, 0 },
	{ 1, 1, 0 },
	{ 1, 1, 1 },
	{ 0, 1, 1 }
}

local function emitTriangle(model, p1, p2, p3, sampleNormal, sampleColor)
	local center = {
		(p1[1] + p2[1] + p3[1]) / 3,
		(p1[2] + p2[2] + p3[2]) / 3,
		(p1[3] + p2[3] + p3[3]) / 3
	}
	local triNormal = normalize(cross(sub(p2, p1), sub(p3, p1)))
	local surfNormal = sampleNormal and sampleNormal(center[1], center[2], center[3]) or triNormal
	if dot(triNormal, surfNormal) < 0 then
		p2, p3 = p3, p2
		triNormal = normalize(cross(sub(p2, p1), sub(p3, p1)))
	end

	local function addVertex(p)
		local idx = #model.vertices + 1
		model.vertices[idx] = { p[1], p[2], p[3] }
		model.vertexNormals[idx] = sampleNormal and sampleNormal(p[1], p[2], p[3]) or triNormal
		local c = sampleColor and sampleColor(p[1], p[2], p[3]) or { 0.45, 0.58, 0.36 }
		model.vertexColors[idx] = { c[1], c[2], c[3], 1.0 }
		return idx
	end

	local i1 = addVertex(p1)
	local i2 = addVertex(p2)
	local i3 = addVertex(p3)
	model.faces[#model.faces + 1] = { i1, i2, i3 }
	local fc = sampleColor and sampleColor(center[1], center[2], center[3]) or { 0.45, 0.58, 0.36 }
	model.faceColors[#model.faceColors + 1] = { fc[1], fc[2], fc[3], 1.0 }
end

local function polygonizeTetra(model, positions, values, iso, sampleNormal, sampleColor)
	local inside = {}
	local outside = {}
	for i = 1, 4 do
		if values[i] <= iso then
			inside[#inside + 1] = i
		else
			outside[#outside + 1] = i
		end
	end

	if #inside == 0 or #inside == 4 then
		return
	end

	if #inside == 1 then
		local a = inside[1]
		local b, c, d = outside[1], outside[2], outside[3]
		local p1 = interpolate(positions[a], positions[b], values[a], values[b], iso)
		local p2 = interpolate(positions[a], positions[c], values[a], values[c], iso)
		local p3 = interpolate(positions[a], positions[d], values[a], values[d], iso)
		emitTriangle(model, p1, p2, p3, sampleNormal, sampleColor)
		return
	end

	if #inside == 3 then
		local a = outside[1]
		local b, c, d = inside[1], inside[2], inside[3]
		local p1 = interpolate(positions[a], positions[b], values[a], values[b], iso)
		local p2 = interpolate(positions[a], positions[c], values[a], values[c], iso)
		local p3 = interpolate(positions[a], positions[d], values[a], values[d], iso)
		emitTriangle(model, p1, p3, p2, sampleNormal, sampleColor)
		return
	end

	-- #inside == 2
	local a, b = inside[1], inside[2]
	local c, d = outside[1], outside[2]
	local p1 = interpolate(positions[a], positions[c], values[a], values[c], iso)
	local p2 = interpolate(positions[a], positions[d], values[a], values[d], iso)
	local p3 = interpolate(positions[b], positions[c], values[b], values[c], iso)
	local p4 = interpolate(positions[b], positions[d], values[b], values[d], iso)
	emitTriangle(model, p1, p3, p2, sampleNormal, sampleColor)
	emitTriangle(model, p2, p3, p4, sampleNormal, sampleColor)
end

function marching.polygonize(opts)
	opts = opts or {}
	local sampleSdf = opts.sampleSdf
	if type(sampleSdf) ~= "function" then
		return { vertices = {}, faces = {}, vertexNormals = {}, vertexColors = {}, faceColors = {}, isSolid = true }
	end

	local sampleNormal = opts.sampleNormal
	local sampleColor = opts.sampleColor
	local iso = tonumber(opts.isoLevel) or 0
	local cellSize = math.max(0.25, tonumber(opts.cellSize) or 4)
	local bounds = opts.bounds or {}
	local x0 = tonumber(bounds.x0) or -32
	local x1 = tonumber(bounds.x1) or 32
	local y0 = tonumber(bounds.y0) or -32
	local y1 = tonumber(bounds.y1) or 32
	local z0 = tonumber(bounds.z0) or -32
	local z1 = tonumber(bounds.z1) or 32

	local nx = math.max(1, math.floor((x1 - x0) / cellSize))
	local ny = math.max(1, math.floor((y1 - y0) / cellSize))
	local nz = math.max(1, math.floor((z1 - z0) / cellSize))

	local model = {
		vertices = {},
		faces = {},
		vertexNormals = {},
		vertexColors = {},
		faceColors = {},
		isSolid = true
	}

	for iy = 0, ny - 1 do
		for ix = 0, nx - 1 do
			for iz = 0, nz - 1 do
				local cubePos = {}
				local cubeVal = {}
				for ci = 1, 8 do
					local off = cubeOffsets[ci]
					local px = x0 + (ix + off[1]) * cellSize
					local py = y0 + (iy + off[2]) * cellSize
					local pz = z0 + (iz + off[3]) * cellSize
					cubePos[ci] = { px, py, pz }
					cubeVal[ci] = sampleSdf(px, py, pz)
				end

				for _, tetra in ipairs(tetrahedra) do
					local pos = {
						cubePos[tetra[1]],
						cubePos[tetra[2]],
						cubePos[tetra[3]],
						cubePos[tetra[4]]
					}
					local val = {
						cubeVal[tetra[1]],
						cubeVal[tetra[2]],
						cubeVal[tetra[3]],
						cubeVal[tetra[4]]
					}
					polygonizeTetra(model, pos, val, iso, sampleNormal, sampleColor)
				end
			end
		end
	end

	return model
end

return marching
