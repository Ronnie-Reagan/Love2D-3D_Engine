local atmosphere = require "Source.Sim.Atmosphere"
local aeroModel = require "Source.Sim.AeroModel"
local propulsion = require "Source.Sim.PropulsionModel"
local integrator = require "Source.Sim.RigidBodyIntegrator"
local flightTrim = require "Source.Sim.FlightTrim"
local flightModelDefaults = require "Source.Sim.FlightModelDefaults"
local q = require "Source.Math.Quat"

local flight = {}
local loveLib = rawget(_G, "love")

local trimWorker = {
	available = false,
	thread = nil,
	requestChannel = nil,
	responseChannel = nil,
	requestId = 0,
	lastAppliedId = 0
}

local function supportsThreading()
	return type(loveLib) == "table" and
		type(loveLib.thread) == "table" and
		type(loveLib.thread.newThread) == "function" and
		type(loveLib.thread.getChannel) == "function"
end

local function resolvePortSourcePath(relativePath)
	if loveLib and loveLib.filesystem and loveLib.filesystem.getInfo and loveLib.filesystem.getInfo(relativePath) then
		return relativePath
	end
	return "portSource/" .. tostring(relativePath)
end

local function getPortSourceAbsoluteRoot()
	local sep = package.config and package.config:sub(1, 1) or "/"
	local source = (loveLib and loveLib.filesystem and loveLib.filesystem.getSource and loveLib.filesystem.getSource()) or "."
	local sourceBase = (loveLib and loveLib.filesystem and loveLib.filesystem.getSourceBaseDirectory and loveLib.filesystem.getSourceBaseDirectory()) or "."
	if loveLib and loveLib.filesystem and loveLib.filesystem.getInfo then
		if loveLib.filesystem.getInfo("Source", "directory") then
			return source
		end
		if loveLib.filesystem.getInfo("portSource/Source", "directory") then
			return tostring(source) .. sep .. "portSource"
		end
	end
	return sourceBase
end

local function resolvePortSourceThreadPath(relativePath)
	local sep = package.config and package.config:sub(1, 1) or "/"
	return tostring(getPortSourceAbsoluteRoot()) .. sep .. tostring(relativePath):gsub("/", sep)
end

local function getPortSourceRootPath()
	return getPortSourceAbsoluteRoot()
end

local function ensureTrimWorker()
	if trimWorker.thread and type(trimWorker.thread.getError) == "function" then
		local err = trimWorker.thread:getError()
		if err then
			trimWorker.available = false
			trimWorker.thread = nil
			trimWorker.requestChannel = nil
			trimWorker.responseChannel = nil
		end
	end

	if trimWorker.available then
		return true
	end
	if not supportsThreading() then
		return false
	end

	if trimWorker.thread then
		trimWorker.available = true
		return true
	end

	local workerId = tostring(os.time()) .. "_" .. tostring(math.random(100000, 999999))
	local requestName = "flight_trim_req_" .. workerId
	local responseName = "flight_trim_rsp_" .. workerId
	local scriptPath = resolvePortSourceThreadPath("Source/Systems/FlightTrimWorker.lua")

	local ok, threadOrErr = pcall(function()
		return loveLib.thread.newThread(scriptPath)
	end)
	if not ok or not threadOrErr then
		return false
	end

	local requestChannel = loveLib.thread.getChannel(requestName)
	local responseChannel = loveLib.thread.getChannel(responseName)
	local started = pcall(function()
		threadOrErr:start(requestName, responseName, getPortSourceRootPath())
	end)
	if not started then
		return false
	end

	trimWorker.thread = threadOrErr
	trimWorker.requestChannel = requestChannel
	trimWorker.responseChannel = responseChannel
	trimWorker.available = true
	return true
end

local function pollTrimWorkerResponses(simState)
	if not trimWorker.available or not trimWorker.responseChannel then
		return
	end

	while true do
		local msg = trimWorker.responseChannel:pop()
		if not msg then
			break
		end
		if type(msg) == "table" and msg.type == "trim_response" then
			local responseId = math.floor(tonumber(msg.requestId) or 0)
			if responseId >= trimWorker.lastAppliedId then
				simState.trimTarget = tonumber(msg.trimTarget) or simState.trimTarget or 0
				trimWorker.lastAppliedId = responseId
				if responseId >= math.max(0, math.floor(tonumber(simState.trimRequestId) or 0)) then
					simState.trimRequestInFlight = false
				end
			end
		end
	end
end

local function submitTrimWorkerRequest(simState, config, altitudeMeters, trueAirspeed)
	if not ensureTrimWorker() or not trimWorker.requestChannel then
		return false
	end
	trimWorker.requestId = trimWorker.requestId + 1
	local requestId = trimWorker.requestId
	local requestOk = pcall(function()
		trimWorker.requestChannel:push({
			type = "trim_request",
			requestId = requestId,
			altitudeMeters = tonumber(altitudeMeters) or 0,
			trueAirspeed = tonumber(trueAirspeed) or 35,
			config = {
				massKg = config.massKg,
				wingArea = config.wingArea,
				CL0 = config.CL0,
				CLalpha = config.CLalpha,
				Cm0 = config.Cm0,
				CmAlpha = config.CmAlpha,
				CmElevator = config.CmElevator,
				maxElevatorDeflectionRad = config.maxElevatorDeflectionRad
			}
		})
	end)
	if requestOk then
		simState.trimRequestId = requestId
		simState.trimRequestInFlight = true
		simState.trimLastRequestTick = math.max(0, math.floor(tonumber(simState.tick) or 0))
		return true
	end
	trimWorker.available = false
	return false
end

local defaultConfig = flightModelDefaults.create()

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function axis(positive, negative)
	local value = 0
	if positive then
		value = value + 1
	end
	if negative then
		value = value - 1
	end
	return value
end

local function dot(a, b)
	return (a[1] or 0) * (b[1] or 0) + (a[2] or 0) * (b[2] or 0) + (a[3] or 0) * (b[3] or 0)
end

local function scale(v, s)
	return { (v[1] or 0) * s, (v[2] or 0) * s, (v[3] or 0) * s }
end

local function add(a, b)
	return { (a[1] or 0) + (b[1] or 0), (a[2] or 0) + (b[2] or 0), (a[3] or 0) + (b[3] or 0) }
end

local function length(v)
	return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

local function normalize(v)
	local len = length(v)
	if len <= 1e-8 then
		return { 0, 1, 0 }
	end
	return { v[1] / len, v[2] / len, v[3] / len }
end

local function isFiniteNumber(value)
	return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function sanitizeNumber(value, fallback)
	local n = tonumber(value)
	if isFiniteNumber(n) then
		return n
	end
	return fallback
end

local function clampVecMagnitude(v, maxMagnitude)
	local x = sanitizeNumber(v and v[1], 0)
	local y = sanitizeNumber(v and v[2], 0)
	local z = sanitizeNumber(v and v[3], 0)
	local len = math.sqrt(x * x + y * y + z * z)
	if len <= maxMagnitude or len <= 1e-8 then
		return { x, y, z }
	end
	local s = maxMagnitude / len
	return { x * s, y * s, z * s }
end

local function sanitizeRotation(rot)
	if type(rot) ~= "table" then
		return q.identity()
	end
	local out = {
		w = sanitizeNumber(rot.w, 1),
		x = sanitizeNumber(rot.x, 0),
		y = sanitizeNumber(rot.y, 0),
		z = sanitizeNumber(rot.z, 0)
	}
	return q.normalize(out)
end

local function applyNumericalGuards(camera, config)
	camera.pos = camera.pos or { 0, 0, 0 }
	camera.flightVel = camera.flightVel or { 0, 0, 0 }
	camera.flightAngVel = camera.flightAngVel or { 0, 0, 0 }
	camera.rot = sanitizeRotation(camera.rot)

	local maxPosAbs = math.max(1000, tonumber(config.maxPositionAbs) or 500000)
	camera.pos[1] = clamp(sanitizeNumber(camera.pos[1], 0), -maxPosAbs, maxPosAbs)
	camera.pos[2] = clamp(sanitizeNumber(camera.pos[2], 0), -maxPosAbs, maxPosAbs)
	camera.pos[3] = clamp(sanitizeNumber(camera.pos[3], 0), -maxPosAbs, maxPosAbs)

	local maxLinear = math.max(20, tonumber(config.maxLinearSpeed) or 180)
	local maxAngular = math.max(0.2, tonumber(config.maxAngularRateRad) or math.rad(240))
	camera.flightVel = clampVecMagnitude(camera.flightVel, maxLinear)
	camera.flightAngVel = clampVecMagnitude(camera.flightAngVel, maxAngular)
end

local function mergeConfig(userConfig)
	local out = {}
	for key, value in pairs(defaultConfig) do
		out[key] = value
	end
	if type(userConfig) == "table" then
		for key, value in pairs(userConfig) do
			out[key] = value
		end
	end
	return out
end

local function ensureCameraFlightState(camera, config)
	camera.flightVel = camera.flightVel or { 0, 0, 0 }
	camera.flightRotVel = camera.flightRotVel or { pitch = 0, yaw = 0, roll = 0 }
	camera.flightAngVel = camera.flightAngVel or { 0, 0, 0 } -- body rates in engine axes: +X right, +Y up, +Z forward.
	camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
	camera.throttle = clamp(camera.throttle or 0, 0, 1)
	camera.flightSimState = camera.flightSimState or {
		accumulator = 0,
		tick = 0,
		engine = { throttle = camera.throttle or 0 },
		elevatorTrim = 0,
		lastAtmosphere = atmosphere.sample(0),
		lastAero = nil
	}
	local simState = camera.flightSimState
	if type(simState.engine) ~= "table" then
		simState.engine = { throttle = camera.throttle or 0 }
	end
	simState.engine.throttle = clamp(tonumber(simState.engine.throttle) or camera.throttle or 0, 0, 1)
	simState.accumulator = clamp(tonumber(simState.accumulator) or 0, 0, 1)
	simState.tick = math.max(0, math.floor(tonumber(simState.tick) or 0))
	simState.lastAtmosphere = simState.lastAtmosphere or atmosphere.sample(0)
	simState.elevatorTrim = clamp(tonumber(simState.elevatorTrim) or 0, -config.maxElevatorDeflectionRad, config.maxElevatorDeflectionRad)
	simState.trimTarget = tonumber(simState.trimTarget) or simState.elevatorTrim
	simState.trimRequestId = math.max(0, math.floor(tonumber(simState.trimRequestId) or 0))
	simState.trimRequestInFlight = simState.trimRequestInFlight and true or false
	simState.trimLastRequestTick = math.max(0, math.floor(tonumber(simState.trimLastRequestTick) or 0))
	simState.trimNextRequestTick = math.max(0, math.floor(tonumber(simState.trimNextRequestTick) or 0))
	if simState.lastCrashTick == nil then
		simState.lastCrashTick = -1000000
	else
		simState.lastCrashTick = math.floor(tonumber(simState.lastCrashTick) or -1000000)
	end
	simState.pendingCrash = simState.pendingCrash
	simState.bootstrapDone = simState.bootstrapDone and true or false
	return simState
end

local function updatePilotInputs(camera, dt, inputState)
	local throttleAxis = axis(inputState.flightThrottleUp, inputState.flightThrottleDown)
	local throttleAccel = camera.throttleAccel or 0.55
	camera.throttle = clamp((camera.throttle or 0) + throttleAxis * throttleAccel * dt, 0, 1)
	if inputState.flightAirBrakes then
		local brakeStrength = camera.airBrakeStrength or 1.2
		camera.throttle = clamp((camera.throttle or 0) - brakeStrength * dt, 0, 1)
	end

	local pitchAxis = axis(inputState.flightPitchDown, inputState.flightPitchUp)
	local yawAxis = axis(inputState.flightYawRight, inputState.flightYawLeft)
	local rollAxis = axis(inputState.flightRollLeft, inputState.flightRollRight)

	local yoke = camera.yoke
	local yokeKeyboardRate = camera.yokeKeyboardRate or 1.8
	local yokeAutoCenterRate = camera.yokeAutoCenterRate or 2.8
	local centerAlpha = clamp(yokeAutoCenterRate * dt, 0, 1)
	local now = (loveLib and loveLib.timer and loveLib.timer.getTime and loveLib.timer.getTime()) or 0
	local yokeMouseHoldDurationSec = math.max(0.0, tonumber(camera.yokeMouseHoldDurationSec) or 0.22)
	local lastMouseInputAt = tonumber(camera.yokeLastMouseInputAt) or -math.huge
	local holdMouseYoke = (now - lastMouseInputAt) <= yokeMouseHoldDurationSec

	yoke.pitch = clamp((yoke.pitch or 0) + pitchAxis * yokeKeyboardRate * dt, -1, 1)
	yoke.yaw = clamp((yoke.yaw or 0) + yawAxis * yokeKeyboardRate * dt, -1, 1)
	yoke.roll = clamp((yoke.roll or 0) + rollAxis * yokeKeyboardRate * dt, -1, 1)

	if not holdMouseYoke and pitchAxis == 0 then
		yoke.pitch = yoke.pitch + (0 - yoke.pitch) * centerAlpha
	end
	if not holdMouseYoke and yawAxis == 0 then
		yoke.yaw = yoke.yaw + (0 - yoke.yaw) * centerAlpha
	end
	if not holdMouseYoke and rollAxis == 0 then
		yoke.roll = yoke.roll + (0 - yoke.roll) * centerAlpha
	end
end

local function bodyToWorld(rot, v)
	return q.rotateVector(rot, v)
end

local function worldToBody(rot, v)
	return q.rotateVector(q.conjugate(rot), v)
end

local function resolveCollisionRadius(camera, environment)
	local override = tonumber(environment and environment.collisionRadius)
	if override and override > 0 then
		return override
	end
	local half = camera and camera.box and camera.box.halfSize
	if half then
		return math.max(0.2, tonumber(half.x) or 0, tonumber(half.y) or 0, tonumber(half.z) or 0)
	end
	return 1.0
end

local function applyTerrainContact(camera, environment, config, dt, simState)
	local pos = camera.pos
	local vel = camera.flightVel
	local radius = resolveCollisionRadius(camera, environment)
	local onGround = false
	local stepDt = math.max(0, tonumber(dt) or 0)

	if environment and type(environment.sampleSdf) == "function" then
		local shouldCheckSdf = true
		if environment and type(environment.groundHeightAt) == "function" then
			local approxGround = tonumber(environment.groundHeightAt(pos[1], pos[3]))
			local verticalSpeed = math.abs(tonumber(vel[2]) or 0)
			local contactBand = math.max(8.0, math.min(40.0, 10.0 + verticalSpeed * 0.35))
			if approxGround and pos[2] > (approxGround + radius + contactBand) then
				shouldCheckSdf = false
			end
		end

		local dist = shouldCheckSdf and tonumber(environment.sampleSdf(pos[1], pos[2], pos[3])) or nil
		if dist and dist < radius then
			local normal = { 0, 1, 0 }
			if type(environment.sampleNormal) == "function" then
				local sampledNormal = environment.sampleNormal(pos[1], pos[2], pos[3])
				if type(sampledNormal) == "table" then
					normal = normalize(sampledNormal)
				end
			end

			local vn = dot(vel, normal)
			if simState and config.crashEnabled ~= false and vn < 0 then
				local impactNormalSpeed = -vn
				local totalSpeed = length(vel)
				local angularSpeed = length(camera.flightAngVel or { 0, 0, 0 })
				local crashNormalSpeed = math.max(0.1, tonumber(config.crashNormalSpeed) or 13.0)
				local crashTotalSpeed = math.max(0.1, tonumber(config.crashTotalSpeed) or 32.0)
				local crashAngularSpeed = math.max(0.05, tonumber(config.crashAngularSpeed) or math.rad(160))
				local shouldCrash = (impactNormalSpeed >= crashNormalSpeed) or
					(totalSpeed >= crashTotalSpeed) or
					(angularSpeed >= crashAngularSpeed)
				if shouldCrash and not simState.pendingCrash then
					local physicsHz = math.max(1, tonumber(config.physicsHz) or 120)
					local cooldownTicks = math.max(
						1,
						math.floor((math.max(0.1, tonumber(config.crashCooldownSec) or 0.9) * physicsHz) + 0.5)
					)
					local lastTick = math.floor(tonumber(simState.lastCrashTick) or -1000000)
					if simState.tick >= (lastTick + cooldownTicks) then
						simState.pendingCrash = {
							position = {
								(pos[1] or 0) - normal[1] * radius,
								(pos[2] or 0) - normal[2] * radius,
								(pos[3] or 0) - normal[3] * radius
							},
							normal = { normal[1], normal[2], normal[3] },
							radius = radius,
							impactNormalSpeed = impactNormalSpeed,
							totalSpeed = totalSpeed,
							angularSpeed = angularSpeed,
							tick = simState.tick
						}
						simState.lastCrashTick = simState.tick
					end
				end
			end

			local penetration = radius - dist
			pos = add(pos, scale(normal, penetration + 1e-4))

			if vn < 0 then
				vel = add(vel, scale(normal, -vn))
			end

			local tangent = add(vel, scale(normal, -dot(vel, normal)))
			local friction = clamp(tonumber(config.groundFriction) or 0.92, 0, 1)
			local tangentRetention = clamp(1.0 - friction * stepDt, 0, 1)
			vel = add(scale(normal, dot(vel, normal)), scale(tangent, tangentRetention))
			onGround = true
		end
	elseif environment and type(environment.groundHeightAt) == "function" then
		local groundHeight = tonumber(environment.groundHeightAt(pos[1], pos[3]))
		if groundHeight and pos[2] <= (groundHeight + radius) then
			pos[2] = groundHeight + radius
			if vel[2] < 0 then
				vel[2] = 0
			end
			local friction = clamp(tonumber(config.groundFriction) or 0.92, 0, 1)
			local tangentRetention = clamp(1.0 - friction * stepDt, 0, 1)
			vel[1] = vel[1] * tangentRetention
			vel[3] = vel[3] * tangentRetention
			onGround = true
		end
	end

	camera.pos = pos
	camera.flightVel = vel
	camera.onGround = onGround
	if onGround then
		local damp = clamp(tonumber(config.groundAngularDamping) or 0.82, 0, 1)
		local angularRetention = clamp(1.0 - damp * stepDt, 0, 1)
		camera.flightAngVel[1] = camera.flightAngVel[1] * angularRetention
		camera.flightAngVel[2] = camera.flightAngVel[2] * angularRetention
		camera.flightAngVel[3] = camera.flightAngVel[3] * angularRetention
	end
end

function flight.defaultConfig()
	return mergeConfig(nil)
end

function flight.step(camera, dt, inputState, environment, userConfig)
	local config = mergeConfig(userConfig)
	inputState = inputState or {}
	dt = clamp(tonumber(dt) or 0, 0, config.maxFrameDt)
	if dt <= 0 then
		return camera
	end

	local simState = ensureCameraFlightState(camera, config)
	applyNumericalGuards(camera, config)
	pollTrimWorkerResponses(simState)
	updatePilotInputs(camera, dt, inputState)

	if not simState.bootstrapDone then
		local metersPerUnit = math.max(0.001, tonumber(config.metersPerUnit) or 1.0)
		local altitudeMeters = (camera.pos[2] or 0) * metersPerUnit
		local atmo = atmosphere.sample(altitudeMeters)
		local wind = (environment and environment.wind) or { 0, 0, 0 }
		local airVelWorld = {
			(camera.flightVel[1] or 0) - (wind[1] or 0),
			(camera.flightVel[2] or 0) - (wind[2] or 0),
			(camera.flightVel[3] or 0) - (wind[3] or 0)
		}
		local airVelBody = worldToBody(camera.rot, airVelWorld)
		local trueAirspeed = math.max(20, length(airVelBody))
		local trimTarget = simState.elevatorTrim or 0
		if config.enableAutoTrim then
			trimTarget = flightTrim.estimateElevatorForLevelFlight(config, atmo, trueAirspeed)
		end
		simState.elevatorTrim = clamp(tonumber(trimTarget) or 0, -config.maxElevatorDeflectionRad, config.maxElevatorDeflectionRad)
		simState.trimTarget = simState.elevatorTrim
		simState.lastAtmosphere = atmo
		simState.bootstrapDone = true
	end

	local fixedDt = 1 / math.max(1, tonumber(config.physicsHz) or 120)
	simState.accumulator = simState.accumulator + dt
	local substeps = 0

	while simState.accumulator >= fixedDt and substeps < config.maxSubsteps do
		substeps = substeps + 1
		simState.accumulator = simState.accumulator - fixedDt
		simState.tick = simState.tick + 1

		local metersPerUnit = math.max(0.001, tonumber(config.metersPerUnit) or 1.0)
		local altitudeMeters = (camera.pos[2] or 0) * metersPerUnit
		local atmo = atmosphere.sample(altitudeMeters)
		simState.lastAtmosphere = atmo

		local wind = (environment and environment.wind) or { 0, 0, 0 }
		local airVelWorld = {
			(camera.flightVel[1] or 0) - (wind[1] or 0),
			(camera.flightVel[2] or 0) - (wind[2] or 0),
			(camera.flightVel[3] or 0) - (wind[3] or 0)
		}
		local airVelBody = worldToBody(camera.rot, airVelWorld)
		local trueAirspeed = length(airVelBody)

		simState.engine = propulsion.updateEngineState(simState.engine, camera.throttle, config, fixedDt)
		local thrustData = propulsion.computeThrust(simState.engine, atmo, trueAirspeed, config)
		simState.lastThrustNewton = thrustData.thrustNewton or 0

		if config.enableAutoTrim then
			local physicsHz = math.max(1, tonumber(config.physicsHz) or 120)
			local trimUpdateHz = math.max(0.5, tonumber(config.autoTrimUpdateHz) or 6.0)
			local trimIntervalTicks = math.max(1, math.floor((physicsHz / trimUpdateHz) + 0.5))
			local trimBlend = clamp(fixedDt * 0.6, 0, 1)
			local trimAirspeed = math.max(20, trueAirspeed)
			local useWorkerTrim = config.autoTrimUseWorker ~= false

			if useWorkerTrim then
				pollTrimWorkerResponses(simState)

				if simState.trimRequestInFlight then
					local timeoutTicks = math.max(
						2,
						math.floor((math.max(0.05, tonumber(config.autoTrimWorkerTimeoutSec) or 0.35) * physicsHz) + 0.5)
					)
					if (simState.tick - (simState.trimLastRequestTick or 0)) > timeoutTicks then
						simState.trimRequestInFlight = false
					end
				end

				if (not simState.trimRequestInFlight) and simState.tick >= (simState.trimNextRequestTick or 0) then
					local sent = submitTrimWorkerRequest(simState, config, altitudeMeters, trimAirspeed)
					simState.trimNextRequestTick = simState.tick + trimIntervalTicks
					if not sent then
						local trimTarget = flightTrim.estimateElevatorForLevelFlight(config, atmo, trimAirspeed)
						simState.trimTarget = trimTarget
					end
				end
			else
				if simState.tick >= (simState.trimNextRequestTick or 0) then
					local trimTarget = flightTrim.estimateElevatorForLevelFlight(config, atmo, trimAirspeed)
					simState.trimTarget = trimTarget
					simState.trimNextRequestTick = simState.tick + trimIntervalTicks
				end
			end

			if simState.trimTarget == nil then
				simState.trimTarget = simState.elevatorTrim
			end
			simState.elevatorTrim = simState.elevatorTrim + (simState.trimTarget - simState.elevatorTrim) * trimBlend
		end

		local controls = {
			elevator = clamp(
				((camera.yoke.pitch or 0) *
					clamp(trueAirspeed / math.max(5.0, tonumber(config.controlAuthoritySpeed) or 34.0),
						clamp(tonumber(config.minControlAuthority) or 0.28, 0.05, 1.0),
						1.0) *
					math.max(0.05, tonumber(config.pitchControlScale) or 0.78) *
					config.maxElevatorDeflectionRad) +
				(simState.elevatorTrim or 0),
				-config.maxElevatorDeflectionRad,
				config.maxElevatorDeflectionRad
			),
			aileron = clamp(
				(camera.yoke.roll or 0) *
				clamp(trueAirspeed / math.max(5.0, tonumber(config.controlAuthoritySpeed) or 34.0),
					clamp(tonumber(config.minControlAuthority) or 0.28, 0.05, 1.0),
					1.0) *
				math.max(0.05, tonumber(config.rollControlScale) or 0.72) *
				config.maxAileronDeflectionRad,
				-config.maxAileronDeflectionRad,
				config.maxAileronDeflectionRad
			),
			rudder = clamp(
				-(camera.yoke.yaw or 0) *
				clamp(trueAirspeed / math.max(5.0, tonumber(config.controlAuthoritySpeed) or 34.0),
					clamp(tonumber(config.minControlAuthority) or 0.28, 0.05, 1.0),
					1.0) *
				math.max(0.05, tonumber(config.yawControlScale) or 0.65) *
				config.maxRudderDeflectionRad,
				-config.maxRudderDeflectionRad,
				config.maxRudderDeflectionRad
			)
		}

		local aeroState = aeroModel.compute(airVelBody, camera.flightAngVel, controls, atmo, config)
		simState.lastAero = aeroState

		local forceBody = {
			aeroState.forceBody[1] or 0,
			aeroState.forceBody[2] or 0,
			(aeroState.forceBody[3] or 0) + (thrustData.thrustNewton or 0)
		}
		forceBody = clampVecMagnitude(forceBody, math.max(1000, tonumber(config.maxForceNewton) or 70000))
		local forceWorld = bodyToWorld(camera.rot, forceBody)
		local momentBody = {
			(aeroState.momentBody and aeroState.momentBody[1]) or 0,
			(aeroState.momentBody and aeroState.momentBody[2]) or 0,
			(aeroState.momentBody and aeroState.momentBody[3]) or 0
		}
		if config.stabilityAugmentation ~= false then
			local pitchDamp = math.max(0, tonumber(config.pitchRateDampingMoment) or 2600)
			local yawDamp = math.max(0, tonumber(config.yawRateDampingMoment) or 1700)
			local rollDamp = math.max(0, tonumber(config.rollRateDampingMoment) or 2100)
			momentBody[1] = momentBody[1] - (tonumber(camera.flightAngVel[1]) or 0) * pitchDamp
			momentBody[2] = momentBody[2] - (tonumber(camera.flightAngVel[2]) or 0) * yawDamp
			momentBody[3] = momentBody[3] - (tonumber(camera.flightAngVel[3]) or 0) * rollDamp
		end
		momentBody = clampVecMagnitude(
			momentBody,
			math.max(1000, tonumber(config.maxMomentNewtonMeter) or 120000)
		)

		local rigidBodyState = {
			pos = { camera.pos[1], camera.pos[2], camera.pos[3] },
			vel = { camera.flightVel[1], camera.flightVel[2], camera.flightVel[3] },
			angVel = { camera.flightAngVel[1], camera.flightAngVel[2], camera.flightAngVel[3] },
			rot = camera.rot
		}
		integrator.step(rigidBodyState, config, forceWorld, momentBody, atmo.gravity, fixedDt)

		camera.pos = rigidBodyState.pos
		camera.flightVel = rigidBodyState.vel
		camera.flightAngVel = rigidBodyState.angVel
		camera.rot = rigidBodyState.rot

		applyTerrainContact(camera, environment, config, fixedDt, simState)
		applyNumericalGuards(camera, config)
		if simState.pendingCrash then
			break
		end
	end

	if simState.pendingCrash then
		camera.flightCrashEvent = simState.pendingCrash
		simState.pendingCrash = nil
		camera.flightVel = { 0, 0, 0 }
		camera.flightAngVel = { 0, 0, 0 }
		camera.vel = { 0, 0, 0 }
	end

	camera.vel = { camera.flightVel[1], camera.flightVel[2], camera.flightVel[3] }
	camera.flightRotVel.pitch = camera.flightAngVel[1] or 0
	camera.flightRotVel.yaw = camera.flightAngVel[2] or 0
	camera.flightRotVel.roll = camera.flightAngVel[3] or 0

	if camera.box and camera.box.pos then
		camera.box.pos = { camera.pos[1], camera.pos[2], camera.pos[3] }
	end

	camera.flightDebug = camera.flightDebug or {}
	local dbg = camera.flightDebug
	dbg.tick = simState.tick
	dbg.substeps = substeps
	dbg.airDensity = simState.lastAtmosphere and simState.lastAtmosphere.densityKgM3 or 1.225
	dbg.alpha = simState.lastAero and simState.lastAero.alpha or 0
	dbg.beta = simState.lastAero and simState.lastAero.beta or 0
	dbg.qbar = simState.lastAero and simState.lastAero.dynamicPressure or 0
	dbg.thrust = simState.lastThrustNewton or 0
	dbg.throttle = simState.engine and simState.engine.throttle or camera.throttle

	return camera
end

return flight
