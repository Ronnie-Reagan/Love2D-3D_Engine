local function normalizeNetworkMode(value)
	local mode = tostring(value or "client"):lower()
	if mode == "host" or mode == "listen" then
		return "host"
	end
	if mode == "offline" or mode == "none" then
		return "offline"
	end
	if mode == "dedicated" or mode == "server" then
		return "dedicated"
	end
	return "client"
end

if not netMode then
	netMode = normalizeNetworkMode(os.getenv("L2D3D_NET_MODE") or "client")
end
if netMode == "dedicated" then
	require("Source.Net.RelayServer")
else
	require("Source.App.ClientMain")
end
