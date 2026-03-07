local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function readFile(path)
	local handle, err = io.open(path, "rb")
	assertTrue(handle ~= nil, "failed to open " .. tostring(path) .. ": " .. tostring(err))
	local data = handle:read("*a")
	handle:close()
	return data
end

local sep = package.config and package.config:sub(1, 1) or "/"
local root = love.filesystem.getSourceBaseDirectory() or "."
local mainSource = readFile(root .. sep .. "main.lua")
local pauseSource = readFile(root .. sep .. "Source" .. sep .. "Ui" .. sep .. "PauseMenuSystem.lua")

assertTrue(
	not mainSource:find("adjustPauseItem%s*%("),
	"main.lua should not call PauseMenuSystem.adjustPauseItem directly"
)
assertTrue(
	mainSource:find("adjustSelectedPauseItem%s*=%s*pauseExports%.adjustSelectedPauseItem"),
	"main.lua should bind adjustSelectedPauseItem from pauseExports"
)
assertTrue(
	mainSource:find("adjustSelectedPauseItem%s*%("),
	"main.lua should route pause wheel changes through adjustSelectedPauseItem"
)
assertTrue(
	pauseSource:find("adjustSelectedPauseItem%s*=%s*adjustSelectedPauseItem"),
	"PauseMenuSystem should export adjustSelectedPauseItem"
)

print("Main pause-menu integration tests passed")
