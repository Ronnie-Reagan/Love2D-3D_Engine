local cloud = {}
local viewMath = require "Source.Math.ViewMath"

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function randomRange(minValue, maxValue)
	return minValue + (maxValue - minValue) * math.random()
end

function cloud.pickNextWindTarget(windState)
	windState.targetAngle = randomRange(0, math.pi * 2)
	windState.targetSpeed = randomRange(4.5, 12.0)
	windState.retargetIn = randomRange(7.0, 15.0)
end

function cloud.updateWind(windState, dt)
	windState.retargetIn = windState.retargetIn - dt
	if windState.retargetIn <= 0 then
		cloud.pickNextWindTarget(windState)
	end

	local blend = clamp(dt * 0.6, 0, 1)
	windState.angle = viewMath.wrapAngle(
		windState.angle + viewMath.shortestAngleDelta(windState.angle, windState.targetAngle) * blend
	)
	windState.speed = windState.speed + (windState.targetSpeed - windState.speed) * blend
end

function cloud.getWindVectorXZ(windState)
	return math.cos(windState.angle) * windState.speed, math.sin(windState.angle) * windState.speed
end

function cloud.getWindVector3(windState)
	local windX, windZ = cloud.getWindVectorXZ(windState)
	return { windX, 0, windZ }
end

function cloud.clearCloudObjects(objects)
	for i = #objects, 1, -1 do
		if objects[i].isCloud then
			table.remove(objects, i)
		end
	end
end

local function chooseCloudCenter(spawnUpwind, camera, windState, cloudState)
	local refX = (camera and camera.pos and camera.pos[1]) or 0
	local refZ = (camera and camera.pos and camera.pos[3]) or 0
	local angle
	if spawnUpwind then
		angle = viewMath.wrapAngle(windState.angle + math.pi + randomRange(-0.8, 0.8))
	else
		angle = randomRange(0, math.pi * 2)
	end

	local distance = randomRange(cloudState.spawnRadius * 0.45, cloudState.spawnRadius)
	local x = refX + math.cos(angle) * distance
	local y = randomRange(cloudState.minAltitude, cloudState.maxAltitude)
	local z = refZ + math.sin(angle) * distance
	return x, y, z
end

function cloud.updateCloudGroupVisuals(group, now)
	for _, puff in ipairs(group.puffs) do
		local bob = math.sin(now * 0.2 + puff.cloudBobPhase) * puff.cloudBobAmplitude
		puff.pos[1] = group.center[1] + puff.cloudOffset[1]
		puff.pos[2] = group.center[2] + puff.cloudOffset[2] + bob
		puff.pos[3] = group.center[3] + puff.cloudOffset[3]
	end
end

function cloud.randomizeCloudGroup(group, spawnUpwind, camera, windState, cloudState, now, q)
	local cx, cy, cz = chooseCloudCenter(spawnUpwind, camera, windState, cloudState)
	group.center[1], group.center[2], group.center[3] = cx, cy, cz
	group.radius = randomRange(cloudState.minGroupSize, cloudState.maxGroupSize)
	group.windDrift = randomRange(0.85, 1.25)

	local maxOffset = group.radius
	for _, puff in ipairs(group.puffs) do
		local angle = randomRange(0, math.pi * 2)
		local radial = randomRange(maxOffset * 0.2, maxOffset)
		puff.cloudOffset[1] = math.cos(angle) * radial
		puff.cloudOffset[2] = randomRange(-maxOffset * 0.2, maxOffset * 0.25)
		puff.cloudOffset[3] = math.sin(angle) * radial
		puff.cloudBobPhase = randomRange(0, math.pi * 2)
		puff.cloudBobAmplitude = randomRange(0.2, 1.2)

		local puffSize = randomRange(group.radius * 0.22, group.radius * 0.48)
		puff.scale[1] = puffSize
		puff.scale[2] = puffSize * randomRange(0.55, 0.78)
		puff.scale[3] = puffSize
		puff.rot = q.fromAxisAngle({ 0, 1, 0 }, randomRange(0, math.pi * 2))

		local shade = randomRange(0.88, 0.99)
		puff.color[1] = shade
		puff.color[2] = shade
		puff.color[3] = math.min(1.0, shade + randomRange(0.01, 0.03))
		puff.color[4] = randomRange(0.17, 0.35)
	end

	cloud.updateCloudGroupVisuals(group, now)
end

function cloud.spawnCloudField(cloudState, objects, camera, windState, cloudPuffModel, q, groupCountOverride, now)
	cloud.clearCloudObjects(objects)
	cloudState.groups = {}
	cloudState.groupCount = math.max(1, math.floor(groupCountOverride or cloudState.groupCount or 1))
	cloudState.minPuffs = math.max(1, math.floor(cloudState.minPuffs or 1))
	cloudState.maxPuffs = math.max(cloudState.minPuffs, math.floor(cloudState.maxPuffs or cloudState.minPuffs))

	now = now or 0
	for _ = 1, cloudState.groupCount do
		local puffCount = math.random(cloudState.minPuffs, cloudState.maxPuffs)
		local group = {
			center = { 0, 0, 0 },
			radius = randomRange(cloudState.minGroupSize, cloudState.maxGroupSize),
			windDrift = randomRange(0.85, 1.25),
			puffs = {}
		}

		for _ = 1, puffCount do
			local puff = {
				model = cloudPuffModel,
				pos = { 0, 0, 0 },
				rot = q.identity(),
				scale = { 1, 1, 1 },
				color = { 0.95, 0.95, 0.97, math.random(0.1, 0.4) },
				isSolid = false,
				isCloud = true,
				cloudOffset = { 0, 0, 0 },
				cloudBobPhase = 0,
				cloudBobAmplitude = 0.5
			}
			group.puffs[#group.puffs + 1] = puff
			objects[#objects + 1] = puff
		end

		cloud.randomizeCloudGroup(group, false, camera, windState, cloudState, now, q)
		cloudState.groups[#cloudState.groups + 1] = group
	end
end

function cloud.updateClouds(dt, cloudState, cloudNetState, camera, windState, now, q)
	if #cloudState.groups == 0 or not camera then
		return
	end

	local isAuthoritySimulation = cloudNetState.role ~= "follower"
	if isAuthoritySimulation then
		cloud.updateWind(windState, dt)
	end

	local windX, windZ = cloud.getWindVectorXZ(windState)
	local maxDistSq = cloudState.despawnRadius * cloudState.despawnRadius

	for _, group in ipairs(cloudState.groups) do
		group.center[1] = group.center[1] + windX * group.windDrift * dt
		group.center[3] = group.center[3] + windZ * group.windDrift * dt

		local dx = group.center[1] - camera.pos[1]
		local dz = group.center[3] - camera.pos[3]
		if isAuthoritySimulation and (dx * dx + dz * dz) > maxDistSq then
			cloud.randomizeCloudGroup(group, true, camera, windState, cloudState, now, q)
		else
			cloud.updateCloudGroupVisuals(group, now)
		end
	end
end

return cloud
