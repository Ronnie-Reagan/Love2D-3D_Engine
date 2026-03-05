local tunnelGenerator = require "Source.Sim.TunnelGenerator"

local field = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function frac(v)
	return v - math.floor(v)
end

local function hash01(ix, iy, iz, seed)
	local n = math.sin(ix * 127.1 + iy * 311.7 + iz * 73.13 + seed * 19.97) * 43758.5453123
	return frac(n)
end

local function smoothstep(t)
	return t * t * (3 - 2 * t)
end

local function valueNoise2(x, z, seed)
	local ix = math.floor(x)
	local iz = math.floor(z)
	local fx = x - ix
	local fz = z - iz

	local v00 = hash01(ix, 0, iz, seed)
	local v10 = hash01(ix + 1, 0, iz, seed)
	local v01 = hash01(ix, 0, iz + 1, seed)
	local v11 = hash01(ix + 1, 0, iz + 1, seed)

	local sx = smoothstep(fx)
	local sz = smoothstep(fz)
	local a = v00 + (v10 - v00) * sx
	local b = v01 + (v11 - v01) * sx
	return a + (b - a) * sz
end

local function valueNoise3(x, y, z, seed)
	local ix = math.floor(x)
	local iy = math.floor(y)
	local iz = math.floor(z)
	local fx = x - ix
	local fy = y - iy
	local fz = z - iz

	local function corner(dx, dy, dz)
		return hash01(ix + dx, iy + dy, iz + dz, seed)
	end

	local sx = smoothstep(fx)
	local sy = smoothstep(fy)
	local sz = smoothstep(fz)

	local c000 = corner(0, 0, 0)
	local c100 = corner(1, 0, 0)
	local c010 = corner(0, 1, 0)
	local c110 = corner(1, 1, 0)
	local c001 = corner(0, 0, 1)
	local c101 = corner(1, 0, 1)
	local c011 = corner(0, 1, 1)
	local c111 = corner(1, 1, 1)

	local x00 = c000 + (c100 - c000) * sx
	local x10 = c010 + (c110 - c010) * sx
	local x01 = c001 + (c101 - c001) * sx
	local x11 = c011 + (c111 - c011) * sx
	local y0 = x00 + (x10 - x00) * sy
	local y1 = x01 + (x11 - x01) * sy
	return y0 + (y1 - y0) * sz
end

local function fbm2(x, z, octaves, lacunarity, gain, seed)
	local amp = 1.0
	local freq = 1.0
	local sum = 0
	local weight = 0
	for i = 1, octaves do
		sum = sum + valueNoise2(x * freq, z * freq, seed + i * 101) * amp
		weight = weight + amp
		amp = amp * gain
		freq = freq * lacunarity
	end
	if weight <= 1e-6 then
		return 0
	end
	return sum / weight
end

local function fbm3(x, y, z, octaves, lacunarity, gain, seed)
	local amp = 1.0
	local freq = 1.0
	local sum = 0
	local weight = 0
	for i = 1, octaves do
		sum = sum + valueNoise3(x * freq, y * freq, z * freq, seed + i * 157) * amp
		weight = weight + amp
		amp = amp * gain
		freq = freq * lacunarity
	end
	if weight <= 1e-6 then
		return 0
	end
	return sum / weight
end

local defaultParams = {
	seed = 1,
	chunkSize = 64,
	worldRadius = 2048,
	minY = -180,
	maxY = 320,
	baseHeight = 0,
	heightAmplitude = 120,
	heightFrequency = 0.0018,
	heightOctaves = 5,
	heightLacunarity = 2.05,
	heightGain = 0.52,
	surfaceDetailAmplitude = 14,
	surfaceDetailFrequency = 0.013,
	waterLevel = -12,
	caveEnabled = false,
	caveFrequency = 0.018,
	caveThreshold = 0.68,
	caveStrength = 42,
	caveOctaves = 3,
	caveLacunarity = 2.1,
	caveGain = 0.5,
	caveMinY = -120,
	caveMaxY = 220,
	tunnelCount = 0,
	tunnelRadiusMin = 9,
	tunnelRadiusMax = 18,
	tunnelLengthMin = 240,
	tunnelLengthMax = 520,
	tunnelSegmentLength = 18
}

function field.normalizeParams(params, defaults)
	local base = {}
	for key, value in pairs(defaultParams) do
		base[key] = value
	end
	if type(defaults) == "table" then
		for key, value in pairs(defaults) do
			base[key] = value
		end
	end
	if type(params) == "table" then
		for key, value in pairs(params) do
			base[key] = value
		end
	end

	base.seed = math.floor(tonumber(base.seed) or 1)
	base.chunkSize = math.max(8, tonumber(base.chunkSize) or 64)
	base.worldRadius = math.max(base.chunkSize * 4, tonumber(base.worldRadius) or 2048)
	base.minY = tonumber(base.minY) or -180
	base.maxY = math.max(base.minY + 16, tonumber(base.maxY) or 320)
	base.baseHeight = tonumber(base.baseHeight) or 0
	base.heightAmplitude = math.max(0, tonumber(base.heightAmplitude) or 120)
	base.heightFrequency = math.max(0.00001, tonumber(base.heightFrequency) or 0.0018)
	base.heightOctaves = math.max(1, math.floor(tonumber(base.heightOctaves) or 5))
	base.heightLacunarity = math.max(1.1, tonumber(base.heightLacunarity) or 2.05)
	base.heightGain = clamp(tonumber(base.heightGain) or 0.52, 0.1, 0.95)
	base.surfaceDetailAmplitude = math.max(0, tonumber(base.surfaceDetailAmplitude) or 14)
	base.surfaceDetailFrequency = math.max(0.0001, tonumber(base.surfaceDetailFrequency) or 0.013)
	base.waterLevel = tonumber(base.waterLevel) or -12
	base.caveEnabled = base.caveEnabled ~= false
	base.caveFrequency = math.max(0.0001, tonumber(base.caveFrequency) or 0.018)
	base.caveThreshold = clamp(tonumber(base.caveThreshold) or 0.68, 0.05, 0.95)
	base.caveStrength = math.max(1, tonumber(base.caveStrength) or 42)
	base.caveOctaves = math.max(1, math.floor(tonumber(base.caveOctaves) or 3))
	base.caveLacunarity = math.max(1.1, tonumber(base.caveLacunarity) or 2.1)
	base.caveGain = clamp(tonumber(base.caveGain) or 0.5, 0.1, 0.95)
	base.caveMinY = tonumber(base.caveMinY) or -120
	base.caveMaxY = tonumber(base.caveMaxY) or 220
	base.tunnelCount = math.max(0, math.floor(tonumber(base.tunnelCount) or 12))
	base.tunnelRadiusMin = math.max(1, tonumber(base.tunnelRadiusMin) or 9)
	base.tunnelRadiusMax = math.max(base.tunnelRadiusMin, tonumber(base.tunnelRadiusMax) or 18)
	base.tunnelLengthMin = math.max(32, tonumber(base.tunnelLengthMin) or 240)
	base.tunnelLengthMax = math.max(base.tunnelLengthMin, tonumber(base.tunnelLengthMax) or 520)
	base.tunnelSegmentLength = math.max(6, tonumber(base.tunnelSegmentLength) or 18)
	return base
end

function field.createContext(params)
	local normalized = field.normalizeParams(params)
	local tunnelSeeds = tunnelGenerator.buildTunnelSeeds(normalized)
	return {
		params = normalized,
		tunnelSeeds = tunnelSeeds
	}
end

function field.sampleSurfaceHeight(x, z, context)
	local params = context.params
	local nx = x * params.heightFrequency
	local nz = z * params.heightFrequency
	local heightNoise = fbm2(nx, nz, params.heightOctaves, params.heightLacunarity, params.heightGain, params.seed) * 2 - 1
	local detailNoise = valueNoise2(x * params.surfaceDetailFrequency, z * params.surfaceDetailFrequency, params.seed + 907) * 2
		- 1
	return params.baseHeight + (heightNoise * params.heightAmplitude) + (detailNoise * params.surfaceDetailAmplitude)
end

function field.sampleSdf(x, y, z, context)
	local params = context.params
	local surface = field.sampleSurfaceHeight(x, z, context)

	-- Ground solid: y below surface => negative distance.
	local sdf = y - surface

	if params.caveEnabled and y >= params.caveMinY and y <= params.caveMaxY then
		local caveNoise = fbm3(
			x * params.caveFrequency,
			y * params.caveFrequency,
			z * params.caveFrequency,
			params.caveOctaves,
			params.caveLacunarity,
			params.caveGain,
			params.seed + 1701
		)
		local caveDensity = caveNoise - params.caveThreshold
		local caveSdf = -caveDensity * params.caveStrength
		-- CSG subtraction: terrain \ cave.
		sdf = math.max(sdf, -caveSdf)
	end

	if context.tunnelSeeds and #context.tunnelSeeds > 0 then
		local tunnelSdf = tunnelGenerator.sampleDistance(x, y, z, context.tunnelSeeds)
		sdf = math.max(sdf, -tunnelSdf)
	end

	return sdf
end

function field.sampleNormal(x, y, z, context)
	local e = 0.65
	local dx = field.sampleSdf(x + e, y, z, context) - field.sampleSdf(x - e, y, z, context)
	local dy = field.sampleSdf(x, y + e, z, context) - field.sampleSdf(x, y - e, z, context)
	local dz = field.sampleSdf(x, y, z + e, context) - field.sampleSdf(x, y, z - e, context)
	local len = math.sqrt(dx * dx + dy * dy + dz * dz)
	if len <= 1e-8 then
		return { 0, 1, 0 }
	end
	return { dx / len, dy / len, dz / len }
end

function field.sampleColorAtWorld(x, y, z, context)
	local params = context.params
	local surface = field.sampleSurfaceHeight(x, z, context)
	local depthBelowSurface = surface - y
	if surface <= params.waterLevel + 1.0 then
		return { 0.08, 0.19, 0.34 }
	end
	if depthBelowSurface > 14 then
		return { 0.31, 0.26, 0.23 }
	end

	local n = valueNoise2(x * 0.012, z * 0.012, params.seed + 331) * 2 - 1
	local g = clamp(0.48 + n * 0.12, 0.12, 0.85)
	local r = clamp(0.26 + n * 0.08, 0.08, 0.68)
	local b = clamp(0.18 + n * 0.06, 0.06, 0.5)
	return { r, g, b }
end

return field
