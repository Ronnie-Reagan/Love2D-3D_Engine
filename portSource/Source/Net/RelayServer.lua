local hostedServer = require("Source.Net.HostedServer")
local love = require("love")

local port = math.floor(tonumber(os.getenv("L2D3D_NET_PORT")) or 1988)
local worldName = tostring(os.getenv("L2D3D_WORLD_NAME") or "default")
local server, err = hostedServer.create({
	port = port,
	bindAddress = "*",
	maxPeers = 512,
	worldName = worldName,
	createWorld = tostring(os.getenv("L2D3D_WORLD_CREATE") or "1") ~= "0",
	log = function(message)
		print(tostring(message))
	end
})

if not server then
	error("Failed to start relay server: " .. tostring(err), 0)
end

print(string.format("Relay running on port %d world=%s", port, worldName))

function love.update(_dt)
	server:update(10)
end

function love.quit()
	server:stop()
end
