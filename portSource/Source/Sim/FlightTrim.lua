local trim = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

function trim.estimateLevelAlpha(config, atmosphereSample, trueAirspeed)
	local rho = math.max(0.02, tonumber(atmosphereSample and atmosphereSample.densityKgM3) or 1.225)
	local speed = math.max(5.0, tonumber(trueAirspeed) or 35.0)
	local mass = math.max(0.1, tonumber(config and config.massKg) or 1150)
	local wingArea = math.max(0.1, tonumber(config and config.wingArea) or 16.2)
	local gravity = math.abs(tonumber(atmosphereSample and atmosphereSample.gravity) or 9.80665)

	local cl0 = tonumber(config and config.CL0) or 0.25
	local clAlpha = tonumber(config and config.CLalpha) or 5.5
	local clRequired = (2.0 * mass * gravity) / (rho * speed * speed * wingArea)
	return (clRequired - cl0) / math.max(1e-4, clAlpha)
end

function trim.estimateElevatorForLevelFlight(config, atmosphereSample, trueAirspeed)
	local alpha = -trim.estimateLevelAlpha(config, atmosphereSample, trueAirspeed)
	local cm0 = tonumber(config and config.Cm0) or 0.04
	local cmAlpha = tonumber(config and config.CmAlpha) or -1.2
	local cmElevator = tonumber(config and config.CmElevator) or -1.35
	local elevator = -((cm0 + cmAlpha * alpha) / math.max(1e-4, cmElevator))
	local maxElevator = tonumber(config and config.maxElevatorDeflectionRad) or math.rad(25)
	return clamp(elevator, -maxElevator, maxElevator), alpha
end

return trim
