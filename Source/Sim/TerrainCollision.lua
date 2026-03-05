local collision = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function length(v)
	return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

local function normalize(v)
	local len = length(v)
	if len <= 1e-8 then
		return { 0, -1, 0 }
	end
	return { (v[1] or 0) / len, (v[2] or 0) / len, (v[3] or 0) / len }
end

function collision.raycast(origin, direction, maxDist, sampleSdf, sampleNormal)
	if type(sampleSdf) ~= "function" then
		return nil
	end

	local dir = normalize(direction or { 0, -1, 0 })
	local maxDistance = math.max(0.1, tonumber(maxDist) or 1000)
	local t = 0
	local epsilon = 0.18
	local maxSteps = 220

	for _ = 1, maxSteps do
		if t > maxDistance then
			break
		end

		local px = (origin[1] or 0) + dir[1] * t
		local py = (origin[2] or 0) + dir[2] * t
		local pz = (origin[3] or 0) + dir[3] * t
		local d = tonumber(sampleSdf(px, py, pz))
		if not d then
			return nil
		end

		if d <= epsilon then
			local n = { 0, 1, 0 }
			if type(sampleNormal) == "function" then
				n = sampleNormal(px, py, pz) or n
			end
			return {
				distance = t,
				point = { px, py, pz },
				normal = n
			}
		end

		t = t + clamp(d * 0.9, 0.22, 28.0)
	end

	return nil
end

function collision.queryGroundHeight(x, z, sampleSdf, sampleNormal, opts)
	opts = opts or {}
	local minY = tonumber(opts.minY) or -200
	local maxY = tonumber(opts.maxY) or 1200
	local hit = collision.raycast(
		{ x, maxY, z },
		{ 0, -1, 0 },
		maxY - minY,
		sampleSdf,
		sampleNormal
	)
	if not hit then
		return nil
	end
	return hit.point[2]
end

return collision
