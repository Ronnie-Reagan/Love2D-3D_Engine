local hostedServer = require("Source.Net.HostedServer")
local love = require("love")

local port = math.floor(tonumber(os.getenv("L2D3D_NET_PORT")) or 1988)
local server, err = hostedServer.create({
	port = port,
	bindAddress = "*",
	maxPeers = 128,
	log = function(message)
		print(tostring(message))
	end
})

if not server then
	error("Failed to start relay server: " .. tostring(err), 0)
end

print(string.format("Relay running on port %d", port))

function love.update(_dt)
	server:update(10)
end

function love.quit()
	server:stop()
end
