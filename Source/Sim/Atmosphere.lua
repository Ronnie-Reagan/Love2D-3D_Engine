local atmosphere = {}

local SEA_LEVEL_TEMP_K = 288.15
local SEA_LEVEL_PRESSURE_PA = 101325.0
local LAPSE_RATE_K_PER_M = 0.0065
local GAS_CONSTANT_AIR = 287.05
local GRAVITY = 9.80665
local GAMMA_AIR = 1.4
local TROPOPAUSE_ALTITUDE_M = 11000.0

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

function atmosphere.sample(altitudeMeters)
	local altitude = tonumber(altitudeMeters) or 0
	altitude = clamp(altitude, -2000, 32000)

	local temperature
	local pressure
	if altitude <= TROPOPAUSE_ALTITUDE_M then
		temperature = SEA_LEVEL_TEMP_K - (LAPSE_RATE_K_PER_M * altitude)
		local ratio = temperature / SEA_LEVEL_TEMP_K
		pressure = SEA_LEVEL_PRESSURE_PA * (ratio ^ (GRAVITY / (GAS_CONSTANT_AIR * LAPSE_RATE_K_PER_M)))
	else
		-- Isothermal layer above tropopause (simple extension for stability).
		temperature = SEA_LEVEL_TEMP_K - (LAPSE_RATE_K_PER_M * TROPOPAUSE_ALTITUDE_M)
		local ratio = temperature / SEA_LEVEL_TEMP_K
		local pressureAtTropopause = SEA_LEVEL_PRESSURE_PA * (ratio ^ (GRAVITY / (GAS_CONSTANT_AIR * LAPSE_RATE_K_PER_M)))
		local h = altitude - TROPOPAUSE_ALTITUDE_M
		pressure = pressureAtTropopause * math.exp((-GRAVITY * h) / (GAS_CONSTANT_AIR * temperature))
	end

	local density = pressure / (GAS_CONSTANT_AIR * temperature)
	local sigma = density / 1.225
	local speedOfSound = math.sqrt(GAMMA_AIR * GAS_CONSTANT_AIR * temperature)

	return {
		altitude = altitude,
		temperatureK = temperature,
		pressurePa = pressure,
		densityKgM3 = density,
		sigma = sigma,
		speedOfSound = speedOfSound,
		gravity = GRAVITY
	}
end

return atmosphere
