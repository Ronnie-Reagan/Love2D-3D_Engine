local propulsion = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

function propulsion.updateEngineState(engineState, throttleInput, config, dt)
	engineState = engineState or {}
	local throttle = clamp(tonumber(throttleInput) or 0, 0, 1)
	local tau = math.max(0.05, tonumber(config and config.throttleTimeConstant) or 0.8)
	local alpha = clamp((tonumber(dt) or 0) / tau, 0, 1)

	engineState.throttle = (tonumber(engineState.throttle) or 0) +
		(throttle - (tonumber(engineState.throttle) or 0)) * alpha
	return engineState
end

function propulsion.computeThrust(engineState, atmosphereSample, trueAirspeed, config)
	local throttle = clamp(tonumber(engineState and engineState.throttle) or 0, 0, 1)
	local sigma = math.max(0.05, tonumber(atmosphereSample and atmosphereSample.sigma) or 1.0)
	local maxSeaLevel = math.max(0, tonumber(config and config.maxThrustSeaLevel) or 3200)
	local speed = math.max(0, tonumber(trueAirspeed) or 0)
	local effectiveSpeed = math.max(40, tonumber(config and config.maxEffectivePropSpeed) or 95)

	-- Simple prop efficiency roll-off with speed.
	local speedFactor = clamp(1.0 - (speed / effectiveSpeed), 0.18, 1.0)
	local thrust = maxSeaLevel * sigma * throttle * speedFactor

	return {
		throttle = throttle,
		thrustNewton = thrust
	}
end

return propulsion
