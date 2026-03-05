local atmosphere = require "Source.Sim.Atmosphere"
local flightTrim = require "Source.Sim.FlightTrim"

local requestChannelName, responseChannelName = ...

if type(love) ~= "table" or type(love.thread) ~= "table" then
	return
end

local requestChannel = love.thread.getChannel(requestChannelName)
local responseChannel = love.thread.getChannel(responseChannelName)

while true do
	local job = requestChannel:demand()
	if type(job) == "table" then
		if job.type == "quit" then
			break
		elseif job.type == "trim_request" then
			local config = type(job.config) == "table" and job.config or {}
			local altitudeMeters = tonumber(job.altitudeMeters) or 0
			local trueAirspeed = math.max(1.0, tonumber(job.trueAirspeed) or 35.0)
			local requestId = math.floor(tonumber(job.requestId) or 0)

			local ok, trimTarget, alpha = pcall(function()
				local atmo = atmosphere.sample(altitudeMeters)
				return flightTrim.estimateElevatorForLevelFlight(config, atmo, trueAirspeed)
			end)

			if ok then
				responseChannel:push({
					type = "trim_response",
					requestId = requestId,
					trimTarget = tonumber(trimTarget) or 0,
					alpha = tonumber(alpha) or 0
				})
			end
		end
	end
end
