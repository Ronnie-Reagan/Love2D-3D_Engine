local shadow = {}

local state = {
	enabled = false,
	size = 1024,
	maxDistance = 1200,
	softness = 1.6
}

function shadow.configure(config)
	config = config or {}
	if config.enabled ~= nil then
		state.enabled = config.enabled and true or false
	end
	if config.size ~= nil then
		state.size = math.max(256, math.floor(tonumber(config.size) or state.size))
	end
	if config.maxDistance ~= nil then
		state.maxDistance = math.max(100, tonumber(config.maxDistance) or state.maxDistance)
	end
	if config.softness ~= nil then
		state.softness = math.max(0.4, tonumber(config.softness) or state.softness)
	end
end

function shadow.getConfig()
	return {
		enabled = state.enabled,
		size = state.size,
		maxDistance = state.maxDistance,
		softness = state.softness
	}
end

-- Placeholder for future full shadow-map render pass wiring.
function shadow.begin()
	return state.enabled
end

function shadow.finish()
	return state.enabled
end

return shadow
