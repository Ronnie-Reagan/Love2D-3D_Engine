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
	ridgeAmplitude = 38,
	ridgeFrequency = 0.0042,
	ridgeSharpness = 2.1,
	macroWarpAmplitude = 90,
	macroWarpFrequency = 0.00055,
	terraceStrength = 0.16,
	terraceStep = 18,
	waterLevel = -12,
	shorelineBand = 5,
	waterWaveAmplitude = 1.6,
	waterWaveFrequency = 0.014,
	biomeFrequency = 0.0009,
	snowLine = 140,
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
	tunnelSegmentLength = 18,
	craterHistoryLimit = 64,
	dynamicCraters = {}
}

local function sanitizeCraterList(list, historyLimit)
	local out = {}
	local limit = math.max(0, math.floor(tonumber(historyLimit) or 0))
	if type(list) ~= "table" then
		return out
	end

	for _, crater in ipairs(list) do
		if type(crater) == "table" then
			local radius = math.max(1.0, tonumber(crater.radius) or tonumber(crater.r) or 8.0)
			local depth = math.max(0.4, tonumber(crater.depth) or (radius * 0.45))
			local rim = clamp(tonumber(crater.rim) or 0.12, 0.0, 0.75)
			out[#out + 1] = {
				x = tonumber(crater.x) or 0,
				y = tonumber(crater.y) or 0,
				z = tonumber(crater.z) or 0,
				radius = radius,
				depth = depth,
				rim = rim
			}
		end
	end

	if limit > 0 and #out > limit then
		local first = #out - limit + 1
		local trimmed = {}
		for i = first, #out do
			trimmed[#trimmed + 1] = out[i]
		end
		return trimmed
	end
	return out
end

local function applyDynamicCratersToSurfaceHeight(x, z, height, params)
	local craters = params and params.dynamicCraters
	if type(craters) ~= "table" or #craters <= 0 then
		return height
	end

	local surface = height
	for i = 1, #craters do
		local crater = craters[i]
		local radius = math.max(1.0, tonumber(crater and crater.radius) or 0)
		if radius > 0 then
			local dx = x - (tonumber(crater.x) or 0)
			local dz = z - (tonumber(crater.z) or 0)
			local dist = math.sqrt(dx * dx + dz * dz)
			local t = dist / radius
			if t < 1.0 then
				-- Smooth bowl profile that tapers to the outer rim.
				local bowl = 1.0 - (t * t)
				local depth = math.max(0.2, tonumber(crater.depth) or (radius * 0.45))
				surface = surface - depth * bowl * bowl
			elseif t < 1.24 then
				-- Slight uplift ring to make the impact shape readable at distance.
				local rimT = (t - 1.0) / 0.24
				local rimAmount = math.max(0.0, tonumber(crater.rim) or 0.12)
				local rim = (1.0 - rimT)
				surface = surface + (radius * rimAmount * rim * rim)
			end
		end
	end
	return surface
end

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
	base.ridgeAmplitude = math.max(0, tonumber(base.ridgeAmplitude) or 38)
	base.ridgeFrequency = math.max(0.00001, tonumber(base.ridgeFrequency) or 0.0042)
	base.ridgeSharpness = math.max(0.3, tonumber(base.ridgeSharpness) or 2.1)
	base.macroWarpAmplitude = math.max(0, tonumber(base.macroWarpAmplitude) or 90)
	base.macroWarpFrequency = math.max(0.00001, tonumber(base.macroWarpFrequency) or 0.00055)
	base.terraceStrength = clamp(tonumber(base.terraceStrength) or 0.16, 0, 1)
	base.terraceStep = math.max(1, tonumber(base.terraceStep) or 18)
	base.waterLevel = tonumber(base.waterLevel) or -12
	base.shorelineBand = math.max(0.1, tonumber(base.shorelineBand) or 5)
	base.waterWaveAmplitude = math.max(0, tonumber(base.waterWaveAmplitude) or 1.6)
	base.waterWaveFrequency = math.max(0.0001, tonumber(base.waterWaveFrequency) or 0.014)
	base.biomeFrequency = math.max(0.00001, tonumber(base.biomeFrequency) or 0.0009)
	base.snowLine = tonumber(base.snowLine) or 140
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
	base.craterHistoryLimit = math.max(0, math.floor(tonumber(base.craterHistoryLimit) or 64))
	base.dynamicCraters = sanitizeCraterList(base.dynamicCraters, base.craterHistoryLimit)
	return base
end

function field.createContext(params)
	local normalized = field.normalizeParams(params)
	local tunnelSeeds = type(normalized.tunnelSeeds) == "table" and normalized.tunnelSeeds or
		tunnelGenerator.buildTunnelSeeds(normalized)
	local worldStore = normalized.worldStore
	return {
		params = normalized,
		tunnelSeeds = tunnelSeeds,
		worldStore = worldStore
	}
end

function field.sampleWaterHeight(x, z, context)
	local params = context.params
	local waveFreq = params.waterWaveFrequency
	local waveAmp = params.waterWaveAmplitude
	if waveAmp <= 0 then
		return params.waterLevel
	end

	local n1 = valueNoise2(x * waveFreq, z * waveFreq, params.seed + 700) * 2 - 1
	local n2 = valueNoise2(x * waveFreq * 1.9, z * waveFreq * 1.9, params.seed + 937) * 2 - 1
	local n = n1 * 0.7 + n2 * 0.3
	return params.waterLevel + n * waveAmp
end

function field.sampleSurfaceHeight(x, z, context)
	local params = context.params
	local warpN1 = valueNoise2(x * params.macroWarpFrequency, z * params.macroWarpFrequency, params.seed + 211) * 2 - 1
	local warpN2 = valueNoise2(x * params.macroWarpFrequency, z * params.macroWarpFrequency, params.seed + 347) * 2 - 1
	local warpX = warpN1 * params.macroWarpAmplitude
	local warpZ = warpN2 * params.macroWarpAmplitude
	local wx = x + warpX
	local wz = z + warpZ

	local nx = wx * params.heightFrequency
	local nz = wz * params.heightFrequency
	local heightNoise = fbm2(nx, nz, params.heightOctaves, params.heightLacunarity, params.heightGain, params.seed) * 2 - 1
	local detailNoise = valueNoise2(wx * params.surfaceDetailFrequency, wz * params.surfaceDetailFrequency, params.seed + 907) * 2
		- 1
	local ridge = fbm2(wx * params.ridgeFrequency, wz * params.ridgeFrequency, 4, 2.03, 0.53, params.seed + 503) * 2 - 1
	ridge = 1.0 - math.abs(ridge)
	ridge = ridge ^ params.ridgeSharpness
	local ridgeSigned = (ridge * 2.0) - 1.0

	local baseSurface = params.baseHeight +
		(heightNoise * params.heightAmplitude) +
		(detailNoise * params.surfaceDetailAmplitude) +
		(ridgeSigned * params.ridgeAmplitude)
	if params.terraceStrength > 0 then
		local stepped = math.floor((baseSurface / params.terraceStep) + 0.5) * params.terraceStep
		baseSurface = baseSurface * (1.0 - params.terraceStrength) + stepped * params.terraceStrength
	end
	baseSurface = applyDynamicCratersToSurfaceHeight(x, z, baseSurface, params)
	if context.worldStore and type(context.worldStore.sampleHeightDelta) == "function" then
		baseSurface = baseSurface + (tonumber(context.worldStore:sampleHeightDelta(x, z)) or 0)
	end
	return baseSurface
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

	if context.worldStore and type(context.worldStore.sampleVolumetricOverrideSdf) == "function" then
		local overrideSdf = context.worldStore:sampleVolumetricOverrideSdf(x, y, z)
		if overrideSdf ~= math.huge then
			sdf = math.max(sdf, -overrideSdf)
		end
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
	local waterHeight = field.sampleWaterHeight(x, z, context)
	if y <= (waterHeight + 0.25) then
		local foam = clamp((params.waterLevel + 0.8 - surface) / math.max(0.1, params.shorelineBand), 0, 1)
		local waveTint = valueNoise2(x * 0.021, z * 0.021, params.seed + 1431) * 2 - 1
		local waterBase = {
			clamp((params.waterColor and params.waterColor[1]) or 0.08, 0, 1),
			clamp((params.waterColor and params.waterColor[2]) or 0.19, 0, 1),
			clamp((params.waterColor and params.waterColor[3]) or 0.34, 0, 1)
		}
		return {
			clamp(waterBase[1] + waveTint * 0.02 + foam * 0.12, 0, 1),
			clamp(waterBase[2] + waveTint * 0.04 + foam * 0.16, 0, 1),
			clamp(waterBase[3] + waveTint * 0.06 + foam * 0.18, 0, 1)
		}
	end
	if depthBelowSurface > 14 then
		return { 0.30, 0.26, 0.23 }
	end

	local hL = field.sampleSurfaceHeight(x - 1.5, z, context)
	local hR = field.sampleSurfaceHeight(x + 1.5, z, context)
	local hD = field.sampleSurfaceHeight(x, z - 1.5, context)
	local hU = field.sampleSurfaceHeight(x, z + 1.5, context)
	local slope = clamp(math.sqrt((hR - hL) * (hR - hL) + (hU - hD) * (hU - hD)) * 0.18, 0, 1)

	local biomeNoise = fbm2(
		x * params.biomeFrequency,
		z * params.biomeFrequency,
		4,
		2.0,
		0.54,
		params.seed + 1201
	) * 2 - 1
	local elevation01 = clamp((surface - params.baseHeight + params.heightAmplitude) / math.max(1.0, params.heightAmplitude * 2), 0,
		1)
	local wetness = clamp((params.waterLevel + params.shorelineBand - surface) / math.max(0.1, params.shorelineBand), 0, 1)
	local snow = clamp((surface - params.snowLine) / 90, 0, 1)

	local grass = { 0.24, 0.48, 0.25 }
	local forest = { 0.16, 0.33, 0.20 }
	local sand = { 0.70, 0.64, 0.47 }
	local rock = { 0.47, 0.45, 0.43 }
	local snowColor = { 0.88, 0.89, 0.92 }

	local biomeBlend = clamp((biomeNoise + 1) * 0.5 + elevation01 * 0.16, 0, 1)
	local baseR = grass[1] + (forest[1] - grass[1]) * biomeBlend
	local baseG = grass[2] + (forest[2] - grass[2]) * biomeBlend
	local baseB = grass[3] + (forest[3] - grass[3]) * biomeBlend

	baseR = baseR + (sand[1] - baseR) * wetness
	baseG = baseG + (sand[2] - baseG) * wetness
	baseB = baseB + (sand[3] - baseB) * wetness

	local rockMix = clamp((slope - 0.12) * 1.5, 0, 1)
	baseR = baseR + (rock[1] - baseR) * rockMix
	baseG = baseG + (rock[2] - baseG) * rockMix
	baseB = baseB + (rock[3] - baseB) * rockMix

	baseR = baseR + (snowColor[1] - baseR) * snow
	baseG = baseG + (snowColor[2] - baseG) * snow
	baseB = baseB + (snowColor[3] - baseB) * snow

	local micro = valueNoise2(x * 0.013, z * 0.013, params.seed + 331) * 2 - 1
	baseR = clamp(baseR + micro * 0.05, 0, 1)
	baseG = clamp(baseG + micro * 0.06, 0, 1)
	baseB = clamp(baseB + micro * 0.04, 0, 1)
	return { baseR, baseG, baseB }
end

return field
