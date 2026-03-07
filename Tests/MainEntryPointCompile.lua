local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local sep = package.config and package.config:sub(1, 1) or "/"
local root = love.filesystem.getSourceBaseDirectory() or "."
local mainPath = root .. sep .. "main.lua"

local chunk, err = loadfile(mainPath)
assertTrue(type(chunk) == "function", "main.lua should compile: " .. tostring(err))

print("Main entrypoint compile tests passed")
