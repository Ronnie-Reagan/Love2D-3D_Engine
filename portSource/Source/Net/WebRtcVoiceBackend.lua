local backend = {}

local function buildStub()
	return {
		available = false,
		lastError = "Radio backend unavailable in this runtime",
		channel = 1,
		transmitting = false,
		setChannel = function(self, channel)
			self.channel = math.max(1, math.floor(tonumber(channel) or self.channel or 1))
		end,
		setTransmitting = function(self, enabled)
			self.transmitting = enabled and true or false
		end,
		update = function()
		end,
		getStatusLabel = function(self)
			return self.available and "Radio: connected" or "Radio: stub"
		end
	}
end

function backend.create(_opts)
	return buildStub()
end

return backend
