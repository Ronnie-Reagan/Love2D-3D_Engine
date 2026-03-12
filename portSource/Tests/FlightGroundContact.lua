local flight = require("Source.Systems.FlightDynamicsSystem")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function length3(v)
	return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

local function normalize(v)
	local len = length3(v)
	if len <= 1e-8 then
		return { 0, 1, 0 }
	end
	return { (v[1] or 0) / len, (v[2] or 0) / len, (v[3] or 0) / len }
end

local function makeCamera(pos, vel)
	local startPos = pos or { 0, 3, 0 }
	local startVel = vel or { 0, 0, 25 }
	return {
		pos = { startPos[1], startPos[2], startPos[3] },
		rot = q.identity(),
		flightVel = { startVel[1], startVel[2], startVel[3] },
		vel = { startVel[1], startVel[2], startVel[3] },
		throttle = 0.0,
		yoke = { pitch = 0, yaw = 0, roll = 0 },
		flightRotVel = { pitch = 0, yaw = 0, roll = 0 },
		flightAngVel = { 0, 0, 0 },
		box = {
			halfSize = { x = 0.8, y = 1.2, z = 1.4 },
			pos = { startPos[1], startPos[2], startPos[3] }
		}
	}
end

local function runLowSpeedTouchdown()
	local camera = makeCamera({ 0, 2.8, 0 }, { 0, -2.5, 22 })
	local cfg = flight.defaultConfig()
	cfg.enableAutoTrim = false
	cfg.autoTrimUseWorker = false
	cfg.maxThrustSeaLevel = 0

	local env = {
		wind = { 0, 0, 0 },
		groundHeightAt = function()
			return 0
		end,
		sampleSdf = function(_, y, _)
			return y
		end,
		sampleNormal = function()
			return { 0, 1, 0 }
		end
	}

	local dt = 1 / 120
	for _ = 1, 500 do
		camera = flight.step(camera, dt, {}, env, cfg)
		if camera.onGround then
			break
		end
	end

	assertTrue(camera.onGround == true, "low-speed touchdown should settle on ground")
	assertTrue(camera.flightCrashEvent == nil, "low-speed touchdown should not trigger crash")
	assertTrue(camera.pos[2] >= 1.0 and camera.pos[2] <= 2.6, "camera should rest near collision radius above ground")
end

local function runHighSpeedImpactCrash()
	local camera = makeCamera({ 0, 8.5, 0 }, { 0, -45, 55 })
	local cfg = flight.defaultConfig()
	cfg.enableAutoTrim = false
	cfg.autoTrimUseWorker = false
	cfg.maxThrustSeaLevel = 0

	local env = {
		wind = { 0, 0, 0 },
		groundHeightAt = function()
			return 0
		end,
		sampleSdf = function(_, y, _)
			return y
		end,
		sampleNormal = function()
			return { 0, 1, 0 }
		end
	}

	local dt = 1 / 120
	for _ = 1, 360 do
		camera = flight.step(camera, dt, {}, env, cfg)
		if camera.flightCrashEvent then
			break
		end
	end

	assertTrue(type(camera.flightCrashEvent) == "table", "high-speed impact should generate crash event")
	assertTrue(length3(camera.flightVel) <= 1e-6, "post-crash linear velocity should be zeroed")
	assertTrue(length3(camera.flightAngVel) <= 1e-6, "post-crash angular velocity should be zeroed")
end

local function runSlopeContactNormal()
	local slope = 0.12
	local slopeNormal = normalize({ -slope, 1, 0 })
	local cfg = flight.defaultConfig()
	cfg.enableAutoTrim = false
	cfg.autoTrimUseWorker = false
	cfg.maxThrustSeaLevel = 0
	cfg.crashEnabled = false

	local camera = makeCamera({ 10, 1.0, 0 }, { 0, 0, 0 })
	local env = {
		wind = { 0, 0, 0 },
		groundHeightAt = function(x)
			return slope * x
		end,
		sampleSdf = function(x, y, _)
			return y - (slope * x)
		end,
		sampleNormal = function()
			return { slopeNormal[1], slopeNormal[2], slopeNormal[3] }
		end
	}

	camera = flight.step(camera, 1 / 120, {}, env, cfg)

	local radius = math.max(camera.box.halfSize.x, camera.box.halfSize.y, camera.box.halfSize.z)
	local signedDistance = env.sampleSdf(camera.pos[1], camera.pos[2], camera.pos[3])
	assertTrue(signedDistance >= (radius - 0.05), "terrain contact should depenetrate to collision radius on slope")
	assertTrue(camera.onGround == true, "slope contact should set onGround")
end

local function run()
	runLowSpeedTouchdown()
	runHighSpeedImpactCrash()
	runSlopeContactNormal()
	print("Flight ground contact tests passed")
end

run()
