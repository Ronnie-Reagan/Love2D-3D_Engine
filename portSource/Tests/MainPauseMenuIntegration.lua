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
local clientMainSource = readFile(root .. sep .. "Source" .. sep .. "App" .. sep .. "ClientMain.lua")
local pauseSource = readFile(root .. sep .. "Source" .. sep .. "Ui" .. sep .. "PauseMenuSystem.lua")

assertTrue(
	not clientMainSource:find("adjustPauseItem%s*%("),
	"ClientMain.lua should not call PauseMenuSystem.adjustPauseItem directly"
)
assertTrue(
	clientMainSource:find("adjustSelectedPauseItem%s*=%s*pauseExports%.adjustSelectedPauseItem"),
	"ClientMain.lua should bind adjustSelectedPauseItem from pauseExports"
)
assertTrue(
	clientMainSource:find("adjustSelectedPauseItem%s*%("),
	"ClientMain.lua should route pause wheel changes through adjustSelectedPauseItem"
)
assertTrue(
	pauseSource:find("adjustSelectedPauseItem%s*=%s*adjustSelectedPauseItem"),
	"PauseMenuSystem should export adjustSelectedPauseItem"
)

print("Main pause-menu integration tests passed")
