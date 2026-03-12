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
local viewMath = require "Source.Math.ViewMath"

if type(love) ~= "table" or type(love.thread) ~= "table" or type(love.image) ~= "table" then
	return
end

local requestChannel = love.thread.getChannel(requestChannelName)
local responseChannel = love.thread.getChannel(responseChannelName)

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

while true do
	local request = requestChannel:demand()
	if type(request) == "table" and request.type == "build_map" then
		local targetRes = math.max(96, math.floor(tonumber(request.targetRes) or 256))
		local halfRes = targetRes * 0.5
		local centerX = tonumber(request.centerX) or 0
		local centerZ = tonumber(request.centerZ) or 0
		local extent = math.max(1, tonumber(request.extent) or 400)
		local worldPerPixel = (extent * 2) / targetRes
		local heading = tonumber(request.heading) or 0
		local orientationMode = request.orientationMode == "north_up" and "north_up" or "heading_up"
		local params = type(request.params) == "table" and request.params or {}
		local context = sdfField.createContext(params)
		local imageData = love.image.newImageData(targetRes, targetRes)

		for y = 0, targetRes - 1 do
			for x = 0, targetRes - 1 do
				local mapX = (x + 0.5 - halfRes) * worldPerPixel
				local mapZ = (halfRes - (y + 0.5)) * worldPerPixel
				local worldDX, worldDZ = viewMath.mapToWorldDelta(mapX, mapZ, heading, orientationMode)
				local worldX = centerX + worldDX
				local worldZ = centerZ + worldDZ
				local worldY = sdfField.sampleSurfaceHeight(worldX, worldZ, context) or 0
				local c = sdfField.sampleColorAtWorld(worldX, worldY, worldZ, context)
				local edgeX = math.abs((x + 0.5) / targetRes * 2 - 1)
				local edgeY = math.abs((y + 0.5) / targetRes * 2 - 1)
				local vignette = 1 - (math.max(edgeX, edgeY) * 0.09)
				vignette = clamp(vignette, 0.78, 1.0)
				imageData:setPixel(
					x,
					y,
					clamp(c[1] * vignette, 0, 1),
					clamp(c[2] * vignette, 0, 1),
					clamp(c[3] * vignette, 0, 1),
					0.98
				)
			end
		end

		responseChannel:push({
			type = "build_map_done",
			requestId = request.requestId,
			targetRes = targetRes,
			groundSig = request.groundSig,
			centerX = centerX,
			centerZ = centerZ,
			heading = heading,
			extent = extent,
			orientationMode = orientationMode,
			imageData = imageData
		})
	end
end
