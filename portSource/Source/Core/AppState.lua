local M = {}

function M.create(initialValues)
	local state = {
		values = {}
	}

	if type(initialValues) == "table" then
		for key, value in pairs(initialValues) do
			state.values[key] = value
		end
	end

	function state:get(key, fallback)
		local value = self.values[key]
		if value == nil then
			return fallback
		end
		return value
	end

	function state:set(key, value)
		self.values[key] = value
		return value
	end

	function state:merge(values)
		if type(values) ~= "table" then
			return
		end
		for key, value in pairs(values) do
			self.values[key] = value
		end
	end

	return state
end

return M
