local tunnels = {}

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

local function hash01(a, b, c, seed)
	local n = math.sin((a * 127.1) + (b * 311.7) + (c * 73.13) + (seed * 19.97)) * 43758.5453123
	return frac(n)
end

local function lerp(a, b, t)
	return a + (b - a) * t
end

local function distancePointToSegment(p, a, b)
	local abx = b[1] - a[1]
	local aby = b[2] - a[2]
	local abz = b[3] - a[3]
	local apx = p[1] - a[1]
	local apy = p[2] - a[2]
	local apz = p[3] - a[3]
	local abLen2 = abx * abx + aby * aby + abz * abz
	if abLen2 <= 1e-8 then
		local dx = p[1] - a[1]
		local dy = p[2] - a[2]
		local dz = p[3] - a[3]
		return math.sqrt(dx * dx + dy * dy + dz * dz)
	end
	local t = clamp((apx * abx + apy * aby + apz * abz) / abLen2, 0, 1)
	local cx = a[1] + abx * t
	local cy = a[2] + aby * t
	local cz = a[3] + abz * t
	local dx = p[1] - cx
	local dy = p[2] - cy
	local dz = p[3] - cz
	return math.sqrt(dx * dx + dy * dy + dz * dz)
end

function tunnels.buildTunnelSeeds(params)
	params = params or {}
	local seed = math.floor(tonumber(params.seed) or 1)
	local chunkSize = math.max(8, tonumber(params.chunkSize) or 64)
	local worldRadius = math.max(chunkSize * 4, tonumber(params.worldRadius) or 2048)
	local count = math.max(0, math.floor(tonumber(params.tunnelCount) or 10))
	local minRadius = math.max(3, tonumber(params.tunnelRadiusMin) or 10)
	local maxRadius = math.max(minRadius, tonumber(params.tunnelRadiusMax) or 18)
	local minY = tonumber(params.minY) or -180
	local maxY = tonumber(params.maxY) or 280
	local minLength = math.max(chunkSize * 2, tonumber(params.tunnelLengthMin) or 220)
	local maxLength = math.max(minLength, tonumber(params.tunnelLengthMax) or 520)
	local segmentLength = math.max(8, tonumber(params.tunnelSegmentLength) or 18)

	local out = {}
	for i = 1, count do
		local idx = i * 13
		local sx = lerp(-worldRadius, worldRadius, hash01(idx, 1, 3, seed))
		local sy = lerp(minY, maxY, hash01(idx, 4, 8, seed))
		local sz = lerp(-worldRadius, worldRadius, hash01(idx, 9, 15, seed))
		local heading = lerp(0, math.pi * 2, hash01(idx, 16, 22, seed))
		local pitch = lerp(-0.2, 0.2, hash01(idx, 23, 31, seed) * 2 - 1)
		local length = lerp(minLength, maxLength, hash01(idx, 37, 41, seed))
		local radius = lerp(minRadius, maxRadius, hash01(idx, 43, 47, seed))
		local wobbleAmp = lerp(5, 22, hash01(idx, 51, 59, seed))
		local wobbleFreq = lerp(0.01, 0.045, hash01(idx, 61, 67, seed))
		local yawJitter = lerp(-0.12, 0.12, hash01(idx, 71, 73, seed) * 2 - 1)

		local dir = {
			math.cos(pitch) * math.cos(heading),
			math.sin(pitch),
			math.cos(pitch) * math.sin(heading)
		}
		local points = {}
		local steps = math.max(3, math.floor(length / segmentLength))
		for step = 0, steps do
			local t = step / steps
			local dist = t * length
			local bendHeading = heading + math.sin(dist * wobbleFreq + i) * yawJitter
			local bendPitch = pitch + math.cos(dist * wobbleFreq * 0.7 + i * 1.7) * 0.08
			local bendDir = {
				math.cos(bendPitch) * math.cos(bendHeading),
				math.sin(bendPitch),
				math.cos(bendPitch) * math.sin(bendHeading)
			}
			local wx = math.sin(dist * wobbleFreq + i * 0.73) * wobbleAmp
			local wy = math.sin(dist * wobbleFreq * 0.63 + i * 1.19) * (wobbleAmp * 0.42)
			local wz = math.cos(dist * wobbleFreq + i * 0.37) * wobbleAmp
			points[#points + 1] = {
				sx + bendDir[1] * dist + wx,
				sy + bendDir[2] * dist + wy,
				sz + bendDir[3] * dist + wz
			}
		end

		out[#out + 1] = {
			start = { sx, sy, sz },
			dir = dir,
			radius = radius,
			points = points
		}
	end
	return out
end

function tunnels.sampleDistance(x, y, z, tunnelSeeds)
	if type(tunnelSeeds) ~= "table" or #tunnelSeeds == 0 then
		return math.huge
	end
	local p = { x, y, z }
	local minDistance = math.huge
	for _, tunnel in ipairs(tunnelSeeds) do
		local points = tunnel.points
		local radius = tonumber(tunnel.radius) or 10
		for i = 1, #points - 1 do
			local d = distancePointToSegment(p, points[i], points[i + 1]) - radius
			if d < minDistance then
				minDistance = d
			end
		end
	end
	return minDistance
end

return tunnels
