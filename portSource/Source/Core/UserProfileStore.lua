local store = {}

local PROFILE_FILE = "user_profile.lua"

local function isArray(tbl)
	local count = 0
	for key in pairs(tbl) do
		if type(key) ~= "number" then
			return false
		end
		if key <= 0 or key ~= math.floor(key) then
			return false
		end
		if key > count then
			count = key
		end
	end
	return true, count
end

local function serializeValue(value, depth)
	depth = depth or 0
	if depth > 16 then
		return "nil"
	end
	local valueType = type(value)
	if valueType == "nil" then
		return "nil"
	end
	if valueType == "number" then
		if value ~= value or value == math.huge or value == -math.huge then
			return "0"
		end
		return tostring(value)
	end
	if valueType == "boolean" then
		return value and "true" or "false"
	end
	if valueType == "string" then
		return string.format("%q", value)
	end
	if valueType ~= "table" then
		return "nil"
	end

	local parts = {}
	local isList, maxIndex = isArray(value)
	if isList then
		for i = 1, maxIndex do
			parts[#parts + 1] = serializeValue(value[i], depth + 1)
		end
	else
		for key, entry in pairs(value) do
			local keyType = type(key)
			if keyType == "string" or keyType == "number" then
				local keyToken
				if keyType == "string" and key:match("^[_%a][_%w]*$") then
					keyToken = key
				else
					keyToken = "[" .. serializeValue(key, depth + 1) .. "]"
				end
				parts[#parts + 1] = keyToken .. "=" .. serializeValue(entry, depth + 1)
			end
		end
	end
	return "{" .. table.concat(parts, ",") .. "}"
end

function store.save(payload, path)
	local loveLib = rawget(_G, "love")
	if not loveLib or not loveLib.filesystem then
		return false, "love filesystem unavailable"
	end
	if type(payload) ~= "table" then
		return false, "payload must be a table"
	end
	local filePath = (type(path) == "string" and path ~= "") and path or PROFILE_FILE
	local content = "return " .. serializeValue(payload)
	local ok, err = pcall(function()
		loveLib.filesystem.write(filePath, content)
	end)
	if not ok then
		return false, tostring(err or "write failed")
	end
	return true
end

function store.load(path)
	local loveLib = rawget(_G, "love")
	if not loveLib or not loveLib.filesystem then
		return nil, "love filesystem unavailable"
	end
	local filePath = (type(path) == "string" and path ~= "") and path or PROFILE_FILE
	local raw, readErr = loveLib.filesystem.read(filePath)
	if type(raw) ~= "string" or raw == "" then
		return nil, readErr or "profile not found"
	end

	local loader
	local loadErr
	if type(load) == "function" then
		local okModern, modernResult = pcall(function()
			return load(raw, "@" .. filePath, "t", {})
		end)
		if okModern then
			loader = modernResult
		else
			local okLegacy, legacyResult = pcall(function()
				return load(raw, "@" .. filePath)
			end)
			if okLegacy then
				loader = legacyResult
			else
				loadErr = legacyResult
			end
		end
	elseif type(loadstring) == "function" then
		loader, loadErr = loadstring(raw, "@" .. filePath)
	end
	if not loader then
		return nil, tostring(loadErr or "profile parse failed")
	end
	if type(setfenv) == "function" then
		setfenv(loader, {})
	end
	local ok, payload = pcall(loader)
	if not ok or type(payload) ~= "table" then
		return nil, tostring(payload or "profile eval failed")
	end
	return payload
end

function store.filePath()
	return PROFILE_FILE
end

return store
