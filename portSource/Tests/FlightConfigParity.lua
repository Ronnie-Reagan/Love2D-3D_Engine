local flight = require("Source.Systems.FlightDynamicsSystem")
local gameDefaults = require("Source.Core.GameDefaults")
local objectDefs = require("Source.Core.ObjectDefs")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function assertNear(actual, expected, epsilon, message)
	local diff = math.abs((actual or 0) - (expected or 0))
	if diff > (epsilon or 1e-9) then
		error((message or "values differ") .. string.format(" (actual=%f expected=%f)", actual, expected), 2)
	end
end

local function valueTypeName(v)
	local t = type(v)
	if t == "nil" then
		return "nil"
	end
	return t
end

local function run()
	local runtimeDefaults = gameDefaults.create(objectDefs.cubeModel, q)
	local runtimeModel = runtimeDefaults.flightModel
	local simModel = flight.defaultConfig()

	assertTrue(type(runtimeModel) == "table", "runtime flight model defaults must be a table")
	assertTrue(type(simModel) == "table", "sim flight defaults must be a table")
	assertTrue(runtimeModel ~= simModel, "default tables should not alias each other")

	local visited = {}
	for key, simValue in pairs(simModel) do
		visited[key] = true
		local runtimeValue = runtimeModel[key]
		assertTrue(runtimeValue ~= nil, "runtime defaults missing key: " .. tostring(key))
		assertTrue(
			type(simValue) == type(runtimeValue),
			string.format(
				"type mismatch for key '%s': sim=%s runtime=%s",
				tostring(key),
				valueTypeName(simValue),
				valueTypeName(runtimeValue)
			)
		)

		if type(simValue) == "number" then
			assertNear(runtimeValue, simValue, 1e-9, "numeric mismatch for key '" .. tostring(key) .. "'")
		else
			assertTrue(runtimeValue == simValue, "value mismatch for key '" .. tostring(key) .. "'")
		end
	end

	for key, _ in pairs(runtimeModel) do
		assertTrue(visited[key], "runtime defaults contain extra key not present in sim defaults: " .. tostring(key))
	end

	print("Flight config parity tests passed")
end

run()
