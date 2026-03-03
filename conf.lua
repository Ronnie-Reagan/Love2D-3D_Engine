local function trim(s)
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function parseCsv(value)
	local out = {}
	if type(value) ~= "string" then
		return out
	end
	for token in string.gmatch(value, "([^,]+)") do
		token = trim(token)
		if token ~= "" then
			out[#out + 1] = token
		end
	end
	return out
end

local function normalizeGraphicsApiPreference(value)
	if type(value) ~= "string" then
		return "auto"
	end
	local lowered = value:lower()
	if lowered == "vulkan" then
		return "vulkan"
	end
	if lowered == "opengl" or lowered == "gl" then
		return "opengl"
	end
	return "auto"
end

local function getRendererOrderForPreference(preference)
	local pref = normalizeGraphicsApiPreference(preference)
	if pref == "vulkan" then
		return { "vulkan", "opengl", "opengles", "metal" }
	end
	if pref == "opengl" then
		return { "opengl", "vulkan", "opengles", "metal" }
	end
	return { "vulkan", "metal", "opengl", "opengles" }
end

local function getRestartGraphicsApiPreference()
	if type(love) ~= "table" or type(love.restart) ~= "table" then
		return "auto"
	end
	if type(love.restart.graphicsApiPreference) == "string" then
		return love.restart.graphicsApiPreference
	end
	if type(love.restart.graphics) == "table" and type(love.restart.graphics.apiPreference) == "string" then
		return love.restart.graphics.apiPreference
	end
	return "auto"
end

local function hasCliOption(name)
	if type(arg) ~= "table" then
		return false
	end
	for i = 1, #arg do
		local value = tostring(arg[i] or "")
		if value == name then
			return true
		end
		if value:sub(1, #name + 1) == (name .. "=") then
			return true
		end
	end
	return false
end

function love.conf(t)
	t.graphics = t.graphics or {}

	-- Keep LOVE's native CLI backend flags authoritative when supplied.
	if hasCliOption("--renderers") or hasCliOption("--excluderenderers") then
		return
	end

	-- Default to allowing all major LOVE 12 backends, including Vulkan.
	t.graphics.renderers = getRendererOrderForPreference("auto")

	-- Optional overrides for local launcher scripts.
	local envRenderers = os.getenv("L2D3D_RENDERERS")
	local envExcludeRenderers = os.getenv("L2D3D_EXCLUDE_RENDERERS")
	local preferred = parseCsv(envRenderers)
	local excluded = parseCsv(envExcludeRenderers)
	if #preferred > 0 then
		t.graphics.renderers = preferred
	end
	if #excluded > 0 then
		t.graphics.excluderenderers = excluded
	end

	-- In-game API switches use love.event.restart with a restart payload.
	local restartPreference = getRestartGraphicsApiPreference()
	t.graphics.renderers = getRendererOrderForPreference(restartPreference)
end
