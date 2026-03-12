local wire = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function formatFloat(value)
	return string.format("%.4f", tonumber(value) or 0)
end

local function splitToken(value, delimiter)
	local out = {}
	if type(value) ~= "string" or value == "" then
		return out
	end
	for token in value:gmatch("[^" .. delimiter .. "]+") do
		out[#out + 1] = token
	end
	return out
end

function wire.encodeHeightDeltas(values)
	if type(values) ~= "table" or #values <= 0 then
		return ""
	end
	local parts = {}
	for i = 1, #values do
		parts[i] = formatFloat(values[i])
	end
	return table.concat(parts, ",")
end

function wire.decodeHeightDeltas(value)
	local out = {}
	for _, token in ipairs(splitToken(value, ",")) do
		out[#out + 1] = tonumber(token) or 0
	end
	return out
end

function wire.encodeVolumetricOverrides(overrides)
	if type(overrides) ~= "table" or #overrides <= 0 then
		return ""
	end
	local parts = {}
	for i = 1, #overrides do
		local override = overrides[i]
		if type(override) == "table" then
			parts[#parts + 1] = table.concat({
				tostring(override.kind or "sphere"),
				formatFloat(override.x),
				formatFloat(override.y),
				formatFloat(override.z),
				formatFloat(override.radius)
			}, ",")
		end
	end
	return table.concat(parts, ";")
end

function wire.decodeVolumetricOverrides(value)
	local out = {}
	for _, entry in ipairs(splitToken(value, ";")) do
		local kind, x, y, z, radius = entry:match("^([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$")
		if kind and x and y and z and radius then
			out[#out + 1] = {
				kind = kind,
				x = tonumber(x) or 0,
				y = tonumber(y) or 0,
				z = tonumber(z) or 0,
				radius = math.max(0.1, tonumber(radius) or 1.0)
			}
		end
	end
	return out
end

function wire.encodeTunnelSeeds(seeds)
	if type(seeds) ~= "table" or #seeds <= 0 then
		return ""
	end
	local out = {}
	for i = 1, #seeds do
		local seed = seeds[i]
		if type(seed) == "table" and type(seed.points) == "table" and #seed.points > 0 then
			local pointParts = {}
			for j = 1, #seed.points do
				local point = seed.points[j]
				if type(point) == "table" then
					pointParts[#pointParts + 1] = table.concat({
						formatFloat(point[1]),
						formatFloat(point[2]),
						formatFloat(point[3])
					}, ",")
				end
			end
			out[#out + 1] = table.concat({
				formatFloat(seed.radius),
				(seed.hillAttached and 1 or 0),
				table.concat(pointParts, "~")
			}, ":")
		end
	end
	return table.concat(out, ";")
end

function wire.decodeTunnelSeeds(value)
	local out = {}
	for _, entry in ipairs(splitToken(value, ";")) do
		local radiusToken, hillToken, pointsToken = entry:match("^([^:]+):([^:]+):(.+)$")
		if radiusToken and hillToken and pointsToken then
			local points = {}
			for _, pointToken in ipairs(splitToken(pointsToken, "~")) do
				local x, y, z = pointToken:match("^([^,]+),([^,]+),([^,]+)$")
				if x and y and z then
					points[#points + 1] = {
						tonumber(x) or 0,
						tonumber(y) or 0,
						tonumber(z) or 0
					}
				end
			end
			if #points > 0 then
				out[#out + 1] = {
					radius = math.max(0.1, tonumber(radiusToken) or 1.0),
					hillAttached = (tonumber(hillToken) or 0) ~= 0,
					points = points
				}
			end
		end
	end
	return out
end

function wire.buildChunkStateFields(chunk)
	chunk = chunk or {}
	return {
		{ "cx", math.floor(tonumber(chunk.cx) or 0) },
		{ "cz", math.floor(tonumber(chunk.cz) or 0) },
		{ "resolution", clamp(math.floor(tonumber(chunk.resolution) or 16), 4, 64) },
		{ "revision", math.max(0, math.floor(tonumber(chunk.revision) or 0)) },
		{ "materialRevision", math.max(0, math.floor(tonumber(chunk.materialRevision) or 0)) },
		{ "heightDeltas", wire.encodeHeightDeltas(chunk.heightDeltas) },
		{ "volumetricOverrides", wire.encodeVolumetricOverrides(chunk.volumetricOverrides) }
	}
end

function wire.decodeChunkStateFields(kv)
	if type(kv) ~= "table" then
		return nil
	end
	return {
		cx = math.floor(tonumber(kv.cx) or 0),
		cz = math.floor(tonumber(kv.cz) or 0),
		resolution = clamp(math.floor(tonumber(kv.resolution) or 16), 4, 64),
		revision = math.max(0, math.floor(tonumber(kv.revision) or 0)),
		materialRevision = math.max(0, math.floor(tonumber(kv.materialRevision) or 0)),
		heightDeltas = wire.decodeHeightDeltas(kv.heightDeltas),
		volumetricOverrides = wire.decodeVolumetricOverrides(kv.volumetricOverrides)
	}
end

return wire
