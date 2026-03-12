local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local sep = package.config and package.config:sub(1, 1) or "/"
local root = love.filesystem.getSourceBaseDirectory() or "."
local mainPath = root .. sep .. "dedicatedServerLauncher" .. sep .. "main.lua"
local confPath = root .. sep .. "dedicatedServerLauncher" .. sep .. "conf.lua"

local mainChunk, mainErr = loadfile(mainPath)
assertTrue(type(mainChunk) == "function", "dedicated launcher main.lua should compile: " .. tostring(mainErr))

local confChunk, confErr = loadfile(confPath)
assertTrue(type(confChunk) == "function", "dedicated launcher conf.lua should compile: " .. tostring(confErr))

print("Dedicated launcher compile tests passed")
